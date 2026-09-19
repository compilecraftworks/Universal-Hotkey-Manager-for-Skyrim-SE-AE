#include "UHI/EscapeCloseGuard.h"

#include <iostream>

int main()
{
    using Guard = UHI::EscapeCloseGuard;
    Guard guard;
    int failures{};
    const auto check = [&](bool passed) { if (!passed) ++failures; };

    check(!guard.IsSuppressed(100));
    guard.Own(100);
    check(guard.IsSuppressed(101));
    check(guard.CurrentPhase() == Guard::Phase::childPressed);

    // Key-up alone must not expose the parent close path.
    guard.Release();
    check(guard.CurrentPhase() == Guard::Phase::childReleased);
    check(guard.IsSuppressed(102));

    // Popup teardown render, then a clean render: both remain suppressed.
    guard.CompleteRender(false);
    check(guard.CurrentPhase() == Guard::Phase::childCooldown);
    check(guard.IsSuppressed(103));
    guard.CompleteRender(true);
    check(guard.IsSuppressed(104));
    guard.CompleteRender(false);
    check(!guard.IsSuppressed(105));
    check(guard.CurrentPhase() == Guard::Phase::idle);

    // Missing release cannot leave Escape permanently captured.
    guard.Own(200, 50);
    check(guard.IsSuppressed(249));
    check(!guard.IsSuppressed(250));
    check(guard.CurrentPhase() == Guard::Phase::idle);
    if (failures) std::cerr << failures << " Escape guard checks failed\n";
    return failures ? 1 : 0;
}
