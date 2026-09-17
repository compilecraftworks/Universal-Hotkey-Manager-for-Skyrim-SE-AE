#include "UHI/BindingHistory.h"
#include "UHI/LastScanStore.h"
#include "UHI/writers/ConfigFileWriter.h"
#include <fstream>
#include <mutex>
#include <regex>

namespace UHI
{
    bool SameBindingSource(const HotkeyRecord& left, const HotkeyRecord& right)
    {
        return left.evidencePath == right.evidencePath && left.evidenceLine == right.evidenceLine &&
            left.settingName == right.settingName && left.settingSection == right.settingSection &&
            left.device == right.device && left.detector == right.detector && left.action == right.action;
    }

    std::vector<BindingChange> LoadBindingHistory(const std::filesystem::path& path)
    {
        std::vector<BindingChange> result;
        const auto records = LastScanStore{}.Load(path, false);
        if (!records || records->size() % 2U != 0U || records->size() > 512U) return result;
        for (std::size_t i = 0; i < records->size(); i += 2U) {
            if (!SameBindingSource((*records)[i], (*records)[i + 1U])) return {};
            result.push_back({(*records)[i], (*records)[i + 1U]});
        }
        return result;
    }

    bool SaveBindingChange(const std::filesystem::path& path,
        const HotkeyRecord& before, const std::string_view newRaw)
    {
        static std::mutex mutex;
        std::scoped_lock lock(mutex);
        auto history = LoadBindingHistory(path);
        std::vector<HotkeyRecord> records;
        const auto start = history.size() >= 256U ? history.size() - 255U : 0U;
        for (std::size_t i = start; i < history.size(); ++i) {
            records.push_back(std::move(history[i].before));
            records.push_back(std::move(history[i].after));
        }
        records.push_back(before);
        auto after = before;
        after.rawBinding = newRaw;
        records.push_back(std::move(after));
        return LastScanStore{}.Save(path, records);
    }

    std::optional<std::string> OriginalBackupValue(const HotkeyRecord& record)
    {
        if (!record.editable || record.detector == "PapyrusRuntimeProperty" ||
            record.evidenceLine == 0U) return std::nullopt;
        auto backup = record.evidencePath;
        backup += ".uhi.bak";
        if (record.detector != "ControlMapScanner")
            return Writers::ConfigFileWriter{}.ReadBinding(backup, record.evidenceLine, record.settingName);
        std::ifstream input(backup);
        std::string line;
        for (std::size_t i = 0; i < record.evidenceLine; ++i)
            if (!std::getline(input, line)) return std::nullopt;
        static const std::regex row(R"(^\s*(.+?)\s+(\S+)\s+(\S+)\s+(\S+)\s+([01])\s+([01])\s+([01])(?:\s+(\S+))?\s*$)");
        std::smatch match;
        if (!std::regex_match(line, match, row) || match[1].str() != record.action) return std::nullopt;
        const auto index = record.device == "keyboard" ? 2U : record.device == "mouse" ? 3U : 4U;
        return match[index].str();
    }
}
