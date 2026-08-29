#include "UHI/EscapeCloseGuard.h"

#include <cassert>

int main()
{
    using Guard = UHI::EscapeCloseGuard;
    Guard guard;

    assert(!guard.IsSuppressed(100));
    guard.Own(100);
    assert(guard.IsSuppressed(101));
    assert(guard.CurrentPhase() == Guard::Phase::childPressed);

    // Key-up alone must not expose the parent close path.
    guard.Release();
    assert(guard.CurrentPhase() == Guard::Phase::childReleased);
    assert(guard.IsSuppressed(102));

    // Popup teardown render, then a clean render: both remain suppressed.
    guard.CompleteRender(false);
    assert(guard.CurrentPhase() == Guard::Phase::childCooldown);
    assert(guard.IsSuppressed(103));
    guard.CompleteRender(true);
    assert(guard.IsSuppressed(104));
    guard.CompleteRender(false);
    assert(!guard.IsSuppressed(105));
    assert(guard.CurrentPhase() == Guard::Phase::idle);

    // Missing release cannot leave Escape permanently captured.
    guard.Own(200, 50);
    assert(guard.IsSuppressed(249));
    assert(!guard.IsSuppressed(250));
    assert(guard.CurrentPhase() == Guard::Phase::idle);
}
