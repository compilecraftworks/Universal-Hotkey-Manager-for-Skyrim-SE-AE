#include "UHI/writers/ConfigFileWriter.h"
#include "UHI/JsonConfigDocument.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    bool IdentifierCharacter(const unsigned char value)
    {
        return std::isalnum(value) != 0 || value == '_' || value == '-' || value == '.';
    }

    std::string_view Trim(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    }

    bool ValidReplacement(const std::string_view value)
    {
        if (value.empty() || value.size() > 512U) return false;
        return std::ranges::none_of(value, [](const char character) {
            return character == '\0' || character == '\r' || character == '\n';
        });
    }

    std::size_t MatchingContainerEnd(const std::string& content, const std::size_t start,
        const std::size_t limit)
    {
        const char open = content[start];
        const char close = open == '[' ? ']' : '}';
        std::size_t depth{};
        bool quoted{};
        char quote{};
        bool escaped{};
        for (auto cursor = start; cursor < limit; ++cursor) {
            const char character = content[cursor];
            if (quoted) {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == quote) quoted = false;
                continue;
            }
            if (character == '"' || character == '\'') {
                quoted = true;
                quote = character;
            } else if (character == open) {
                ++depth;
            } else if (character == close && --depth == 0) {
                return cursor + 1U;
            }
        }
        return std::string::npos;
    }

    bool ReplaceValue(std::string& content, const std::size_t lineNumber,
        const std::string_view settingName, const std::string_view expectedRaw,
        const std::string_view newRaw, const bool jsonSyntax, const std::string_view settingSection,
        std::string* readValue = nullptr)
    {
        if (lineNumber == 0 || settingName.empty()) return false;
        if (jsonSyntax) {
            const UHI::JsonConfigDocument document(content);
            const auto locator = settingSection.starts_with("json:") ? settingSection.substr(5) : std::string_view{};
            const auto* member = document.Find(settingName, lineNumber, locator);
            if (!member) return false;
            const auto current = Trim(std::string_view(content).substr(member->begin, member->end - member->begin));
            if (readValue) { *readValue = current; return true; }
            if (current != Trim(expectedRaw)) return false;
            // Validate the result as well, before touching the original file.
            auto replacement = content;
            replacement.replace(member->begin, member->end - member->begin, newRaw);
            if (!UHI::JsonConfigDocument(replacement).Valid()) return false;
            content = std::move(replacement);
            return true;
        }
        std::size_t lineStart{};
        for (std::size_t line = 1; line < lineNumber; ++line) {
            lineStart = content.find('\n', lineStart);
            if (lineStart == std::string::npos) return false;
            ++lineStart;
        }
        auto lineEnd = content.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = content.size();

        // INI/TOML/YAML assignments begin at the first non-space token, not
        // inside a comment or the string value of an unrelated setting.
        auto first = content.find_first_not_of(" \t\r", lineStart);
        if (first == 0 && content.starts_with("\xEF\xBB\xBF")) first = content.find_first_not_of(" \t\r", 3);
        if (first >= lineEnd) return false;
        if (content.compare(first, 2, "- ") == 0) first = content.find_first_not_of(" \t", first + 2);
        if (first >= lineEnd || content[first] == ';' || content[first] == '#' ||
            content.compare(first, 2, "//") == 0) return false;
        if (content[first] == '"' || content[first] == '\'') ++first;
        if (content.compare(first, settingName.size(), settingName) != 0) return false;

        auto namePosition = lineStart;
        while ((namePosition = content.find(settingName, namePosition)) != std::string::npos &&
               namePosition < lineEnd) {
            if (namePosition != first) return false;
            const auto nameEnd = namePosition + settingName.size();
            const bool leftBoundary = namePosition == lineStart ||
                !IdentifierCharacter(static_cast<unsigned char>(content[namePosition - 1U]));
            const bool rightBoundary = nameEnd >= lineEnd ||
                !IdentifierCharacter(static_cast<unsigned char>(content[nameEnd]));
            if (!leftBoundary || !rightBoundary) {
                namePosition = nameEnd;
                continue;
            }
            auto cursor = nameEnd;
            if (cursor < lineEnd && (content[cursor] == '"' || content[cursor] == '\'')) ++cursor;
            while (cursor < lineEnd && std::isspace(static_cast<unsigned char>(content[cursor]))) ++cursor;
            if (cursor >= lineEnd || (content[cursor] != '=' && content[cursor] != ':')) {
                namePosition = nameEnd;
                continue;
            }
            ++cursor;
            while (cursor < lineEnd && std::isspace(static_cast<unsigned char>(content[cursor]))) ++cursor;
            if (cursor >= lineEnd) return false;

            std::size_t valueStart = cursor;
            std::size_t valueEnd{};
            const char quote = content[cursor];
            if (quote == '"' || quote == '\'') {
                valueStart = ++cursor;
                bool escaped{};
                while (cursor < lineEnd) {
                    const auto character = content[cursor];
                    if (escaped) escaped = false;
                    else if (character == '\\') escaped = true;
                    else if (character == quote) break;
                    ++cursor;
                }
                if (cursor >= lineEnd || newRaw.find(quote) != std::string_view::npos) return false;
                valueEnd = cursor;
            } else if (quote == '[' || quote == '{') {
                valueEnd = MatchingContainerEnd(content, cursor, lineEnd);
                if (valueEnd == std::string::npos) return false;
            } else {
                valueEnd = lineEnd;
                const auto extensionLikeJson = jsonSyntax && lineEnd > cursor &&
                    (content.find(',', cursor) < lineEnd || content.find('}', cursor) < lineEnd);
                if (extensionLikeJson) {
                    valueEnd = (std::min)(content.find(',', cursor), content.find('}', cursor));
                } else {
                    const auto comment = content.find_first_of(";#", cursor);
                    if (comment < valueEnd) valueEnd = comment;
                }
                while (valueEnd > valueStart &&
                    std::isspace(static_cast<unsigned char>(content[valueEnd - 1U]))) --valueEnd;
            }
            if (readValue) {
                // Refuse an ambiguous one-line object with repeated names.
                if (content.find(std::string("\"") + std::string(settingName) + "\"", nameEnd) < lineEnd)
                    return false;
                *readValue = Trim(std::string_view(content).substr(valueStart, valueEnd - valueStart));
                return true;
            }
            if (Trim(std::string_view(content).substr(valueStart, valueEnd - valueStart)) != Trim(expectedRaw)) {
                return false;
            }
            content.replace(valueStart, valueEnd - valueStart, newRaw);
            return true;
        }
        return false;
    }
}

namespace UHI::Writers
{
    std::optional<std::string> ConfigFileWriter::ReadBinding(const std::filesystem::path& path,
        const std::size_t lineNumber, const std::string_view settingName, const std::string_view settingSection) const
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error ||
            std::filesystem::file_size(path, error) > 16U * 1024U * 1024U || error) return std::nullopt;
        std::ifstream input(path, std::ios::binary);
        if (!input) return std::nullopt;
        std::string content((std::istreambuf_iterator<char>(input)), {}), result;
        auto syntaxPath = path;
        if (syntaxPath.extension() == ".bak") { syntaxPath.replace_extension(); syntaxPath.replace_extension(); }
        auto extension = syntaxPath.extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (content.find('\0') != std::string::npos || !ReplaceValue(content, lineNumber, settingName,
                {}, {}, extension == ".json" || extension == ".jsonc", settingSection, &result)) return std::nullopt;
        return ValidReplacement(result) ? std::optional<std::string>(result) : std::nullopt;
    }
    bool ConfigFileWriter::SetBinding(const std::filesystem::path& path,
        const std::size_t lineNumber, const std::string_view settingName,
        const std::string_view expectedRaw, const std::string_view newRaw, const std::string_view settingSection) const
    {
        if (!ValidReplacement(newRaw)) return false;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error ||
            std::filesystem::file_size(path, error) > 16U * 1024U * 1024U || error) return false;
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        std::string content((std::istreambuf_iterator<char>(input)), {});
        input.close();
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        const bool jsonSyntax = extension == ".json" || extension == ".jsonc";
        if (content.find('\0') != std::string::npos ||
            !ReplaceValue(content, lineNumber, settingName, expectedRaw, newRaw, jsonSyntax, settingSection)) return false;

        auto backup = path;
        backup += ".uhi.bak";
        if (!std::filesystem::exists(backup, error)) {
            error.clear();
            if (!std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, error) || error) return false;
        }
        auto temporary = path;
        temporary += ".uhi.tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
        output.close();
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
            return false;
        }
#else
        std::filesystem::rename(temporary, path, error);
        if (error) return false;
#endif
        return true;
    }
}
