#include "UHI/NativeMenuLifecycle.h"
#include <iostream>

namespace
{
    // Use distinct values: production dispatch is by the engine enum's names.
    enum class Message { kShow = 10, kReshow, kHide, kForceHide, unrelated };
    using Lifecycle = UHI::NativeMenuLifecycle;
    int failures{};
    void Check(bool result, const char* label)
    {
        if (!result) { std::cerr << label << '\n'; ++failures; }
    }
    void Show(Lifecycle& menu)
    {
        menu.Request(true);
        Check(menu.IsOpen() && !menu.CanRender(), "Opening request waits for the engine");
        Check(menu.ProcessMessage(Message::kShow) == Lifecycle::Change::shown, "Show dispatch");
        Check(menu.CanRender(), "Shown menu owns its rendered input");
    }
}

int main()
{
    for (const auto hide : { Message::kHide, Message::kForceHide }) {
        Lifecycle menu;
        Show(menu);
        const auto queuedCursorShow = menu.Revision();
        menu.Request(false);
        // Regression: render may run between a close request and kHide.
        // It must not resurrect visibility or schedule another cursor show.
        for (int frame = 0; frame < 3; ++frame)
            Check(!menu.CanRender(), "Closing frame must not render or capture wheel input");
        Check(!menu.IsCurrent(queuedCursorShow), "Cancel cursor show queued before close");
        Check(menu.ProcessMessage(hide) == Lifecycle::Change::hidden, "Both hide paths dispatch cleanup");
        Check(!menu.IsOpen() && !menu.CanRender(), "Closed menu releases wheel and popup input");
        const auto queuedCursorHide = menu.Revision();
        menu.ProcessMessage(hide);
        menu.Closed(); // destructor after hide
        Check(menu.IsCurrent(queuedCursorHide), "Repeated cleanup must not cancel pending cursor release");
        Show(menu);
        Check(!menu.IsCurrent(queuedCursorHide), "Old cursor hide must not affect a reopened menu");
        menu.Closed(); // destruction without kHide
        Check(!menu.IsOpen(), "Destruction releases input without a preceding hide message");
    }

    Lifecycle menu;
    Show(menu);
    menu.ProcessMessage(Message::kForceHide); // no UHM close request, popup may still be flagged
    Check(!menu.IsOpen(), "Engine-driven hide revokes stale popup/capture ownership");
    Check(menu.ProcessMessage(Message::unrelated) == Lifecycle::Change::none && !menu.IsOpen(),
        "Unrelated events cannot reopen the UI");

    const auto opening = menu.Request(true);
    const auto cancellation = menu.Request(false);
    Check(!menu.IsCurrent(opening) && menu.IsCurrent(cancellation) && !menu.IsOpen(),
        "Close cancels an opening task before it reaches Skyrim");
    menu.ProcessMessage(Message::kShow); // already-posted show, before cancellation could run
    Check(!menu.CanRender(), "Late engine show cannot revive a cancelled render");
    menu.ProcessMessage(Message::kHide); // host schedules this for the stale show
    Check(!menu.IsOpen(), "Late show drains without leaving a hidden input owner");

    Show(menu);
    const auto closing = menu.Request(false);
    const auto reopening = menu.Request(true);
    Check(!menu.IsCurrent(closing) && menu.IsCurrent(reopening) && menu.CanRender(),
        "Reopen supersedes an unposted close task");
    menu.ProcessMessage(Message::kReshow);
    Check(menu.CanRender(), "Reshow retains a current opening request");
    for (int i = 0; i < 100; ++i) {
        menu.Request(false);
        menu.ProcessMessage(Message::kHide);
        Check(!menu.IsOpen(), "Repeated close releases wheel capture");
        Show(menu);
    }
    return failures ? 1 : 0;
}
