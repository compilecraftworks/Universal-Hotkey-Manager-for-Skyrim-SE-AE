#pragma once
#include "UHI/HotkeyRecord.h"
#include "UHI/PathEncoding.h"

namespace UHI
{
    // A binding's value and JSON formatting are not part of its identity.
    // Non-JSON records retain their line to distinguish repeated assignments.
    inline std::string BindingSourceIdentity(const HotkeyRecord& record)
    {
        const bool located = record.settingSection.starts_with("json:");
        return PathToUtf8(record.evidencePath.lexically_normal()) + '\x1F' +
            (located ? std::string{} : std::to_string(record.evidenceLine)) + '\x1F' +
            record.detector + '\x1F' + record.settingSection + '\x1F' + record.settingName;
    }

    inline std::string MigrateActionIdentity(std::string value)
    {
        // 1.1.0 stored path/line/detector/section/setting/value. Preserve those
        // aliases when loading, then save the value-independent five fields.
        std::vector<std::size_t> separators;
        for (std::size_t i = 0; i < value.size(); ++i) if (value[i] == '\x1F') separators.push_back(i);
        if (separators.size() == 5) {
            value.resize(separators.back());
            const auto section = std::string_view(value).substr(separators[2] + 1,
                separators[3] - separators[2] - 1);
            if (section.starts_with("json:")) value.erase(separators[0] + 1, separators[1] - separators[0] - 1);
        }
        return value;
    }
}
