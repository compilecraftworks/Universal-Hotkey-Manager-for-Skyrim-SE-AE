#include "../extras/UniversalHotkeyManagerAPI.h"

namespace ExampleUhmConsumer
{
    bool OpenAndDisableNativeHotkey()
    {
        const auto api = UniversalHotkeyManagerAPI::Resolve();
        if (!api) return false;

        api.setHotkeyEnabled(false);
        return api.open();
    }

    void CloseAndRestoreNativeHotkey()
    {
        const auto api = UniversalHotkeyManagerAPI::Resolve();
        if (!api) return;

        api.close();
        api.setHotkeyEnabled(true);
    }

    bool IsOpen()
    {
        const auto api = UniversalHotkeyManagerAPI::Resolve();
        return api && api.isMenuOpen();
    }
}
