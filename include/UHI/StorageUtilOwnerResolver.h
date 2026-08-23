#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <vector>

namespace UHI
{
    // StorageUtilData is a shared PapyrusUtil directory, not the name of the
    // mod that owns a setting.  Resolve its namespace against active plugin,
    // MCM and native-plugin identities after every scan stage has completed.
    void ResolveStorageUtilOwners(const std::filesystem::path& gameRoot,
        std::vector<HotkeyRecord>& records) noexcept;
}
