#include "UHI/JsonReporter.h"
#include "UHI/PathEncoding.h"
#include "UHI/HotkeyCategory.h"

#include <fstream>
#include <stdexcept>

namespace
{
    std::string EscapeJson(const std::string_view value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        constexpr char hex[] = "0123456789ABCDEF";
        for (const unsigned char ch : value) {
            switch (ch) {
            case '\"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (ch < 0x20) {
                    escaped += "\\u00";
                    escaped += hex[(ch >> 4) & 0xF];
                    escaped += hex[ch & 0xF];
                } else {
                    escaped += static_cast<char>(ch);
                }
            }
        }
        return escaped;
    }

    std::string_view StageName(const UHI::ScanStage value)
    {
        switch (value) {
        case UHI::ScanStage::configuration: return "configuration";
        case UHI::ScanStage::scripts: return "scripts";
        case UHI::ScanStage::nativePlugins: return "native_plugins";
        case UHI::ScanStage::runtime: return "runtime";
        }
        return "configuration";
    }

    std::string_view ConfidenceName(const UHI::Confidence value)
    {
        switch (value) {
        case UHI::Confidence::confirmed: return "confirmed";
        case UHI::Confidence::inferred: return "inferred";
        case UHI::Confidence::candidate: return "candidate";
        }
        return "candidate";
    }

    std::string_view ContextConfidenceName(const UHI::ContextConfidence value)
    {
        switch (value) {
        case UHI::ContextConfidence::confirmed: return "confirmed";
        case UHI::ContextConfidence::inferred: return "inferred";
        default: return "unknown";
        }
    }
}

namespace UHI
{
    void JsonReporter::Write(const std::filesystem::path& outputPath, const std::span<const HotkeyRecord> records) const
    {
        std::filesystem::create_directories(outputPath.parent_path());
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Unable to create UHI JSON report");
        }

        output << "{\n  \"schemaVersion\": 6,\n  \"records\": [";
        for (std::size_t index = 0; index < records.size(); ++index) {
            const auto& record = records[index];
            output << (index == 0 ? "\n" : ",\n")
                   << "    {\"owner\": \"" << EscapeJson(record.owner)
                   << "\", \"action\": \"" << EscapeJson(record.action)
                   << "\", \"binding\": \"" << EscapeJson(record.binding)
                   << "\", \"displayBinding\": \"" << EscapeJson(CompactBindingLabel(record.binding))
                   << "\", \"category\": \"" << HotkeyCategoryName(ClassifyHotkey(record))
                   << "\", \"rawBinding\": \"" << EscapeJson(record.rawBinding)
                   << "\", \"settingName\": \"" << EscapeJson(record.settingName)
                   << "\", \"settingSection\": \"" << EscapeJson(record.settingSection)
                   << "\", \"codeSystem\": \"" << EscapeJson(record.codeSystem)
                   << "\", \"device\": \"" << EscapeJson(record.device)
                   << "\", \"detector\": \"" << EscapeJson(record.detector)
                   << "\", \"categoryHint\": \"" << EscapeJson(record.categoryHint)
                   << "\", \"confidence\": \"" << ConfidenceName(record.confidence)
                   << "\", \"stage\": \"" << StageName(record.stage)
                   << "\", \"activationContext\": \"" << EscapeJson(ActivationContextLabel(record.contextMask))
                   << "\", \"contextConfidence\": \"" << ContextConfidenceName(record.contextConfidence)
                   << "\", \"editable\": " << (record.editable ? "true" : "false")
                   << ", \"runtimeActive\": " << (record.runtimeActive ? "true" : "false")
                   << ", \"conflictEligible\": " << (record.conflictEligible ? "true" : "false")
                   << ", \"uiLocalOnly\": " << (record.uiLocalOnly ? "true" : "false")
                   << ", \"evidencePath\": \"" << EscapeJson(UHI::PathToUtf8(record.evidencePath))
                   << "\", \"evidenceLine\": " << record.evidenceLine << '}';
        }
        output << (records.empty() ? "" : "\n") << "  ]\n}\n";
    }
}
