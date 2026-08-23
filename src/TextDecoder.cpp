#include "UHI/TextDecoder.h"

#include <cstdint>

namespace
{
    void AppendUtf8(std::string& output, const std::uint32_t codePoint)
    {
        if (codePoint <= 0x7F) {
            output += static_cast<char>(codePoint);
        } else if (codePoint <= 0x7FF) {
            output += static_cast<char>(0xC0 | (codePoint >> 6));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else if (codePoint <= 0xFFFF) {
            output += static_cast<char>(0xE0 | (codePoint >> 12));
            output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        } else {
            output += static_cast<char>(0xF0 | (codePoint >> 18));
            output += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            output += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            output += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    std::string DecodeUtf16(const std::span<const char> bytes, const bool bigEndian, const std::size_t offset)
    {
        std::string output;
        output.reserve(bytes.size());
        const auto readWord = [&](const std::size_t index) {
            const auto first = static_cast<std::uint8_t>(bytes[index]);
            const auto second = static_cast<std::uint8_t>(bytes[index + 1]);
            return static_cast<std::uint16_t>(bigEndian ? (first << 8) | second : first | (second << 8));
        };
        for (std::size_t index = offset; index + 1 < bytes.size(); index += 2) {
            const auto word = readWord(index);
            if (word >= 0xD800 && word <= 0xDBFF && index + 3 < bytes.size()) {
                const auto low = readWord(index + 2);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    AppendUtf8(output, 0x10000 + ((word - 0xD800) << 10) + (low - 0xDC00));
                    index += 2;
                    continue;
                }
            }
            if (word != 0) {
                AppendUtf8(output, word);
            }
        }
        return output;
    }
}

namespace UHI
{
    std::string DecodeText(const std::span<const char> bytes)
    {
        if (bytes.empty()) return {};
        if (bytes.size() >= 2) {
            const auto first = static_cast<std::uint8_t>(bytes[0]);
            const auto second = static_cast<std::uint8_t>(bytes[1]);
            if (first == 0xFF && second == 0xFE) return DecodeUtf16(bytes, false, 2);
            if (first == 0xFE && second == 0xFF) return DecodeUtf16(bytes, true, 2);
        }
        std::size_t oddNulls{};
        std::size_t evenNulls{};
        const auto sample = bytes.size() < 512 ? bytes.size() : 512;
        for (std::size_t index = 0; index < sample; ++index) {
            if (bytes[index] == 0) (index % 2 == 0 ? evenNulls : oddNulls)++;
        }
        if (oddNulls > sample / 8 && evenNulls == 0) return DecodeUtf16(bytes, false, 0);
        if (evenNulls > sample / 8 && oddNulls == 0) return DecodeUtf16(bytes, true, 0);

        std::size_t offset{};
        if (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xEF &&
            static_cast<std::uint8_t>(bytes[1]) == 0xBB && static_cast<std::uint8_t>(bytes[2]) == 0xBF) {
            offset = 3;
        }
        return std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
    }
}
