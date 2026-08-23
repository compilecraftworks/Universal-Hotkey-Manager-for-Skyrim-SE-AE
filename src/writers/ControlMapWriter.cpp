#include "UHI/writers/ControlMapWriter.h"

#include <fstream>
#include <charconv>
#include <cctype>
#include <optional>
#include <regex>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    std::string Trim(std::string value)
    {
        const auto space = [](const unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
        return value;
    }

    std::optional<std::uint32_t> ParseCode(std::string value)
    {
        value = Trim(std::move(value));
        const bool hex = value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
        const auto* first = value.data() + (hex ? 2 : 0);
        std::uint32_t result{};
        const auto [last, error] = std::from_chars(first, value.data() + value.size(), result, hex ? 16 : 10);
        if (first == value.data() + value.size() || error != std::errc{} || last != value.data() + value.size()) return std::nullopt;
        return result;
    }

    std::string Hex(const std::uint32_t value, const std::size_t width)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result(std::max<std::size_t>(width, 1), '0');
        auto number = value;
        std::size_t position = result.size();
        do {
            if (position == 0) result.insert(result.begin(), '0'), ++position;
            result[--position] = digits[number & 0xF];
            number >>= 4;
        } while (number != 0);
        return "0x" + result;
    }

    std::optional<std::string> NormalizeControlMapBinding(const std::string& input, const std::string& device)
    {
        std::string output;
        std::size_t alternativeStart{};
        while (alternativeStart <= input.size()) {
            const auto alternativeEnd = input.find(',', alternativeStart);
            const auto alternative = input.substr(alternativeStart,
                alternativeEnd == std::string::npos ? std::string::npos : alternativeEnd - alternativeStart);
            if (!output.empty()) output += ',';
            std::size_t componentStart{};
            bool wroteComponent = false;
            while (componentStart <= alternative.size()) {
                const auto componentEnd = alternative.find('+', componentStart);
                const auto raw = alternative.substr(componentStart,
                    componentEnd == std::string::npos ? std::string::npos : componentEnd - componentStart);
                const auto code = ParseCode(raw);
                if (!code) return std::nullopt;
                const bool valid = *code == 0xFF ||
                    (device == "keyboard" && *code < 0xFF) ||
                    (device == "mouse" && *code <= 0x0A) ||
                    (device == "gamepad" && *code <= 0xFFFF);
                if (!valid || (*code == 0xFF && (componentEnd != std::string::npos || alternativeStart != 0 ||
                    alternativeEnd != std::string::npos))) return std::nullopt;
                if (wroteComponent) output += '+';
                output += Hex(*code, device == "gamepad" ? 4 : device == "keyboard" ? 2 : 1);
                wroteComponent = true;
                if (componentEnd == std::string::npos) break;
                componentStart = componentEnd + 1;
            }
            if (!wroteComponent) return std::nullopt;
            if (alternativeEnd == std::string::npos) break;
            alternativeStart = alternativeEnd + 1;
        }
        return output;
    }
}

namespace UHI::Writers
{
    bool ControlMapWriter::SetBinding(const std::filesystem::path& path,
        const std::size_t lineNumber, const std::string& device, const std::string& newBinding) const
    {
        if (lineNumber == 0 || !std::filesystem::exists(path)) {
            return false;
        }
        std::ifstream input(path);
        if (!input) {
            return false;
        }
        std::vector<std::string> lines;
        for (std::string line; std::getline(input, line);) {
            lines.push_back(std::move(line));
        }
        // Windows cannot atomically replace a file while our own read handle is
        // still open without delete sharing.
        input.close();
        if (lineNumber > lines.size()) {
            return false;
        }

        const auto normalized = NormalizeControlMapBinding(newBinding, device);
        if (!normalized) return false;

        auto& line = lines[lineNumber - 1];
        const std::size_t deviceIndex = device == "keyboard" ? 0 : device == "mouse" ? 1 : device == "gamepad" ? 2 : 3;
        if (deviceIndex == 3) {
            return false;
        }
        static const std::regex row(
            R"(^\s*(.+?)\s+(\S+)\s+(\S+)\s+(\S+)\s+([01])\s+([01])\s+([01])(?:\s+(\S+))?\s*$)");
        std::smatch match;
        if (!std::regex_match(line, match, row)) return false;
        const auto group = 2 + deviceIndex;
        line.replace(static_cast<std::size_t>(match.position(group)),
            static_cast<std::size_t>(match.length(group)), *normalized);

        auto backup = path;
        backup += ".uhi.bak";
        std::error_code error;
        if (!std::filesystem::exists(backup) &&
            !std::filesystem::copy_file(path, backup, std::filesystem::copy_options::none, error)) {
            return false;
        }
        auto temporary = path;
        temporary += ".uhi.tmp";
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            return false;
        }
        for (const auto& updated : lines) {
            output << updated << '\n';
        }
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
