#include "UHI/StorageUtilOwnerResolver.h"

#include "UHI/PathEncoding.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace
{
    struct Identity
    {
        std::string display;
        std::string compact;
        std::string acronym;
        int priority{};
    };

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string Compact(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const auto character : value) {
            if (std::isalnum(static_cast<unsigned char>(character))) {
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
        }
        return result;
    }

    std::string Acronym(const std::string_view value)
    {
        std::string result;
        bool atWordStart = true;
        unsigned char previous{};
        for (const auto raw : value) {
            const auto character = static_cast<unsigned char>(raw);
            if (!std::isalnum(character)) {
                atWordStart = true;
                previous = character;
                continue;
            }
            const bool camelStart = std::isupper(character) && previous != 0 && std::islower(previous);
            if (atWordStart || camelStart) {
                result.push_back(static_cast<char>(std::tolower(character)));
            }
            atWordStart = false;
            previous = character;
        }
        return result;
    }

    std::string StorageNamespace(const std::filesystem::path& path)
    {
        std::vector<std::filesystem::path> parts;
        for (const auto& part : path) parts.push_back(part);
        for (std::size_t index = 0; index < parts.size(); ++index) {
            if (Lower(UHI::PathToUtf8(parts[index])) != "storageutildata") continue;
            if (index + 1U >= parts.size()) return {};
            const auto& next = parts[index + 1U];
            if (index + 1U == parts.size() - 1U || next.has_extension()) {
                return UHI::PathToUtf8(next.stem());
            }
            return UHI::PathToUtf8(next);
        }
        return {};
    }

    bool IsGenericIdentity(const std::string_view value)
    {
        static const std::unordered_set<std::string> generic{
            "", "config", "settings", "setting", "default", "defaults", "preset", "presets",
            "storageutil", "storageutildata", "papyrusutil", "skse", "plugins", "scripts"
        };
        return generic.contains(Compact(value));
    }

    void AddIdentity(std::vector<Identity>& identities, std::unordered_map<std::string, std::size_t>& byDisplay,
        std::string display, const int priority)
    {
        if (IsGenericIdentity(display)) return;
        const auto key = Lower(display);
        if (const auto found = byDisplay.find(key); found != byDisplay.end()) {
            identities[found->second].priority = (std::max)(identities[found->second].priority, priority);
            return;
        }
        Identity identity{
            .display = std::move(display),
            .priority = priority
        };
        identity.compact = Compact(identity.display);
        identity.acronym = Acronym(identity.display);
        byDisplay.emplace(key, identities.size());
        identities.push_back(std::move(identity));
    }

    void AddDirectoryNames(const std::filesystem::path& root, std::vector<Identity>& identities,
        std::unordered_map<std::string, std::size_t>& byDisplay, const int priority)
    {
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(root,
                 std::filesystem::directory_options::skip_permission_denied, error), end;
             iterator != end; iterator.increment(error)) {
            if (error) { error.clear(); continue; }
            std::error_code entryError;
            if (!iterator->is_directory(entryError) || entryError) continue;
            AddIdentity(identities, byDisplay, UHI::PathToUtf8(iterator->path().filename()), priority);
        }
    }

    void AddFileStems(const std::filesystem::path& root, const std::unordered_set<std::string>& extensions,
        std::vector<Identity>& identities, std::unordered_map<std::string, std::size_t>& byDisplay,
        const int priority)
    {
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(root,
                 std::filesystem::directory_options::skip_permission_denied, error), end;
             iterator != end; iterator.increment(error)) {
            if (error) { error.clear(); continue; }
            std::error_code entryError;
            if (!iterator->is_regular_file(entryError) || entryError) continue;
            const auto extension = Lower(UHI::PathToUtf8(iterator->path().extension()));
            if (!extensions.contains(extension)) continue;
            AddIdentity(identities, byDisplay, UHI::PathToUtf8(iterator->path().stem()), priority);
        }
    }

    int MatchScore(const std::string& name, const Identity& candidate)
    {
        const auto compact = Compact(name);
        if (compact.empty()) return 0;
        if (compact == candidate.compact) return 10000 + candidate.priority;
        if (compact.size() >= 4U && candidate.compact.find(compact) != std::string::npos) {
            return 8000 - static_cast<int>(candidate.compact.size() - compact.size()) + candidate.priority;
        }
        if (candidate.compact.size() >= 4U && compact.find(candidate.compact) != std::string::npos) {
            return 7600 - static_cast<int>(compact.size() - candidate.compact.size()) + candidate.priority;
        }
        if (compact.size() >= 2U && compact == candidate.acronym) return 7000 + candidate.priority;
        return 0;
    }

    std::string BestIdentity(const std::string& storageNamespace, const std::vector<Identity>& identities)
    {
        const Identity* best{};
        int bestScore{};
        bool ambiguous{};
        for (const auto& candidate : identities) {
            const auto score = MatchScore(storageNamespace, candidate);
            if (score > bestScore) {
                best = &candidate;
                bestScore = score;
                ambiguous = false;
            } else if (score == bestScore && score != 0 && best &&
                candidate.compact != best->compact) {
                ambiguous = true;
            }
        }
        // Never turn a short shared namespace into a confidently wrong mod.
        // In an ambiguous case the namespace itself is still more useful than
        // the old generic "StorageUtilData" owner.
        return best && !ambiguous ? best->display : storageNamespace;
    }
}

namespace UHI
{
    void ResolveStorageUtilOwners(const std::filesystem::path& gameRoot,
        std::vector<HotkeyRecord>& records) noexcept
    {
        try {
            std::vector<Identity> identities;
            std::unordered_map<std::string, std::size_t> byDisplay;
            const auto data = gameRoot / "Data";

            // Active load-order plugins are the strongest stable identities
            // available through the game's virtual filesystem.
            AddFileStems(data, { ".esm", ".esp", ".esl" }, identities, byDisplay, 300);
            AddDirectoryNames(data / "MCM" / "Config", identities, byDisplay, 250);
            AddFileStems(data / "MCM" / "Settings", { ".ini", ".json" }, identities, byDisplay, 240);
            AddFileStems(data / "SKSE" / "Plugins", { ".dll" }, identities, byDisplay, 220);

            // Other scanners often know a useful script, MCM or native owner.
            // They are supporting evidence, but lower priority than a plugin.
            for (const auto& record : records) {
                if (StorageNamespace(record.evidencePath).empty()) {
                    AddIdentity(identities, byDisplay, record.owner, 100);
                }
            }

            for (auto& record : records) {
                const auto storageNamespace = StorageNamespace(record.evidencePath);
                if (storageNamespace.empty()) continue;
                record.owner = BestIdentity(storageNamespace, identities);
            }
        } catch (...) {
            // Attribution is advisory. A malformed or disappearing VFS path
            // must never abort an otherwise valid scan.
        }
    }
}
