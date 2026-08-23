#include "UHI/SexLabDependency.h"
#include "UHI/PathEncoding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr std::size_t kTes4HeaderBytes = 24;
    constexpr std::uint32_t kMaximumTes4DataBytes = 16U * 1024U * 1024U;
    constexpr std::size_t kMaximumPluginFiles = 8192;

    struct PluginInfo
    {
        std::string file;
        std::string stem;
        std::vector<std::string> masters;
    };

    std::string LowerAscii(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::string NormalizeIdentity(const std::string_view value)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (const unsigned char ch : value) {
            if (std::isalnum(ch)) normalized += static_cast<char>(std::tolower(ch));
        }
        return normalized;
    }

    bool IsPlugin(const std::filesystem::path& path)
    {
        const auto extension = LowerAscii(UHI::PathToUtf8(path.extension()));
        return extension == ".esp" || extension == ".esm" || extension == ".esl";
    }

    std::uint16_t ReadU16(const char* value) noexcept
    {
        std::uint16_t result{};
        std::memcpy(&result, value, sizeof(result));
        return result;
    }

    std::uint32_t ReadU32(const char* value) noexcept
    {
        std::uint32_t result{};
        std::memcpy(&result, value, sizeof(result));
        return result;
    }

    std::optional<PluginInfo> ReadPluginHeader(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        std::array<char, kTes4HeaderBytes> header{};
        input.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (!input || std::string_view(header.data(), 4) != "TES4") return std::nullopt;
        const auto dataSize = ReadU32(header.data() + 4);
        if (dataSize == 0 || dataSize > kMaximumTes4DataBytes) return std::nullopt;

        PluginInfo result;
        result.file = LowerAscii(UHI::PathToUtf8(path.filename()));
        result.stem = NormalizeIdentity(UHI::PathToUtf8(path.stem()));
        std::uint32_t remaining = dataSize;
        std::uint32_t extendedSize{};
        while (remaining >= 6) {
            std::array<char, 6> subrecord{};
            input.read(subrecord.data(), static_cast<std::streamsize>(subrecord.size()));
            if (!input) return std::nullopt;
            remaining -= 6;
            const std::string_view type(subrecord.data(), 4);
            const auto smallSize = ReadU16(subrecord.data() + 4);
            if (type == "XXXX") {
                std::array<char, 4> value{};
                if (smallSize != 4 || remaining < 4) return std::nullopt;
                input.read(value.data(), static_cast<std::streamsize>(value.size()));
                if (!input) return std::nullopt;
                remaining -= 4;
                extendedSize = ReadU32(value.data());
                continue;
            }
            const auto size = extendedSize != 0 ? extendedSize : static_cast<std::uint32_t>(smallSize);
            extendedSize = 0;
            if (size > remaining) return std::nullopt;
            if (type == "MAST" && size != 0) {
                if (size > 4096) return std::nullopt;
                std::string master(size, '\0');
                input.read(master.data(), static_cast<std::streamsize>(master.size()));
                if (!input) return std::nullopt;
                if (const auto zero = master.find('\0'); zero != std::string::npos) master.resize(zero);
                if (!master.empty()) result.masters.push_back(LowerAscii(std::move(master)));
            } else {
                input.seekg(static_cast<std::streamoff>(size), std::ios::cur);
                if (!input) return std::nullopt;
            }
            remaining -= size;
        }
        return result;
    }

    void AddIdentityToken(std::unordered_set<std::string>& tokens, std::string token)
    {
        if (token.size() < 5) return;
        const auto stripSuffix = [](std::string value) {
            constexpr std::array suffixes{ std::string_view("sse"), std::string_view("se"),
                std::string_view("ae") };
            for (const auto suffix : suffixes) {
                if (value.size() > suffix.size() + 4 && value.ends_with(suffix)) {
                    value.resize(value.size() - suffix.size());
                    break;
                }
            }
            return value;
        };
        token = stripSuffix(std::move(token));
        if (token.size() >= 5) tokens.insert(token);
        if (token.starts_with("sexlab") && token.size() > 11) tokens.insert(stripSuffix(token.substr(6)));
        if (token.starts_with("sl") && token.size() > 7) tokens.insert(stripSuffix(token.substr(2)));
    }

    bool RelatedToToken(const std::string& owner, const std::string& evidence, const std::string& token)
    {
        if (token.size() < 5) return false;
        if (!evidence.empty() && evidence.find(token) != std::string::npos) return true;
        if (owner == token) return true;
        return owner.size() >= 6 && (owner.find(token) != std::string::npos || token.find(owner) != std::string::npos);
    }

    bool IsSexLabEcosystemIdentity(const std::string& owner, const std::string& evidence)
    {
        const auto contains = [&](const std::string_view token) {
            return owner.find(token) != std::string::npos || evidence.find(token) != std::string::npos;
        };
        if (contains("sexlab")) return true;

        // ESP-less framework patches and MCM replacements commonly keep the
        // canonical ssl* script names, so plugin-master traversal cannot
        // identify them. Restrict this prefix rule to script evidence rather
        // than treating every generic "SL" abbreviation as SexLab.
        if (owner.starts_with("ssl") || evidence.find("scriptsssl") != std::string::npos) return true;

        // The transitive SexLab.esm master graph is the primary signal. These
        // identifiers cover well-known ESP-less script replacements and data
        // namespaces which cannot appear in that graph (for example Utility
        // Plus replacing the canonical ssl* scripts in place).
        static constexpr std::array ecosystemTokens{
            std::string_view("slal"), std::string_view("slanimloader"),
            std::string_view("slso"), std::string_view("separateorgasm"),
            std::string_view("sltools"), std::string_view("sexlabtools"),
            std::string_view("slutilityplus"), std::string_view("sexlabutilityplus"),
            std::string_view("sltriggers"), std::string_view("sltrigger"),
            std::string_view("slate"), std::string_view("slmatchmaker"),
            std::string_view("sexlabmatchmaker"), std::string_view("sexlabdefeat"),
            std::string_view("sexlabscaler"), std::string_view("arousedcreatures"),
            std::string_view("sexlabaroused"), std::string_view("oslaroused"),
            std::string_view("olsaroused"), std::string_view("deviousdevices"),
            std::string_view("privateneeds"), std::string_view("privateneedsorgasm"),
            std::string_view("sexdispatcher"), std::string_view("zazanimationpack"),
            std::string_view("apropos")
        };
        return owner == "dd" || std::ranges::any_of(ecosystemTokens, contains);
    }

    bool IsSceneOnlySexLabControl(const std::string& owner, const std::string& evidence)
    {
        // The canonical framework/Utility Plus MCM hotkeys live on
        // sslSystemConfig and are gated by the active SexLab animation event.
        // Do not apply this to every SexLab-dependent mod: scene starters such
        // as MatchMaker or Defeat can still expose real gameplay hotkeys.
        return owner.find("sexlabframework") != std::string::npos ||
            owner.find("sexlabutility") != std::string::npos ||
            owner.find("slutilityplus") != std::string::npos ||
            owner.find("slso") != std::string::npos ||
            owner.find("separateorgasm") != std::string::npos ||
            evidence.find("slso") != std::string::npos ||
            evidence.find("separateorgasm") != std::string::npos ||
            evidence.find("sslsystemconfig") != std::string::npos;
    }
}

namespace UHI
{
    bool IsSexLabInstalled(const std::filesystem::path& gameRoot) noexcept
    {
        try {
            const auto data = gameRoot / "Data";
            std::error_code error;
            return std::filesystem::is_regular_file(data / "SexLab.esm", error) && !error;
        } catch (...) {
            return false;
        }
    }

    SexLabDependencyIndex DiscoverSexLabDependencies(const std::filesystem::path& gameRoot,
        const DependencyProgressCallback& progress, const CancelCallback& cancel) noexcept
    {
        SexLabDependencyIndex result;
        result.installed = IsSexLabInstalled(gameRoot);
        if (!result.installed) return result;
        try {
            const auto data = gameRoot / "Data";
            std::vector<std::filesystem::path> candidates;
            std::error_code error;
            for (std::filesystem::directory_iterator iterator(data,
                std::filesystem::directory_options::skip_permission_denied, error), end;
                iterator != end && candidates.size() < kMaximumPluginFiles;) {
                try {
                    if (iterator->is_regular_file(error) && !error && IsPlugin(iterator->path())) {
                        candidates.push_back(iterator->path());
                    }
                } catch (...) {}
                error.clear();
                iterator.increment(error);
                if (error) error.clear();
            }
            std::ranges::sort(candidates);
            std::vector<PluginInfo> plugins;
            plugins.reserve(candidates.size());
            for (std::size_t index = 0; index < candidates.size(); ++index) {
                if (ScanCancelled(cancel)) return result;
                if (progress) progress(candidates[index], index, candidates.size());
                try {
                    if (auto plugin = ReadPluginHeader(candidates[index])) plugins.push_back(std::move(*plugin));
                } catch (...) {}
            }
            if (progress) progress(data, candidates.size(), candidates.size());

            result.pluginFiles.insert("sexlab.esm");
            bool changed = true;
            while (changed) {
                changed = false;
                for (const auto& plugin : plugins) {
                    if (result.pluginFiles.contains(plugin.file)) continue;
                    if (std::ranges::any_of(plugin.masters,
                        [&](const std::string& master) { return result.pluginFiles.contains(master); })) {
                        result.pluginFiles.insert(plugin.file);
                        changed = true;
                    }
                }
            }
            for (const auto& plugin : plugins) {
                if (result.pluginFiles.contains(plugin.file)) AddIdentityToken(result.identityTokens, plugin.stem);
            }
        } catch (...) {
            // Installation detection remains valid even if an individual
            // malformed or unreadable plugin prevents dependency indexing.
        }
        return result;
    }

    void ApplySexLabDependencyHints(const SexLabDependencyIndex& index,
        const std::span<HotkeyRecord> records) noexcept
    {
        if (!index.installed) return;
        try {
            for (auto& record : records) {
                const auto owner = NormalizeIdentity(record.owner);
                const auto evidence = NormalizeIdentity(UHI::PathToUtf8(record.evidencePath));
                if (IsSexLabEcosystemIdentity(owner, evidence) ||
                    std::ranges::any_of(index.identityTokens,
                    [&](const std::string& token) { return RelatedToToken(owner, evidence, token); })) {
                    record.categoryHint = "sexlab";
                    if (IsSceneOnlySexLabControl(owner, evidence)) {
                        record.contextMask = static_cast<std::uint32_t>(ActivationContext::sexLabScene);
                        record.contextConfidence = ContextConfidence::confirmed;
                    }
                }
            }
        } catch (...) {
        }
    }
}
