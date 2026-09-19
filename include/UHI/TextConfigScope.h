#pragma once
#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

namespace UHI
{
    class TextConfigScope
    {
    public:
        struct Line { std::size_t begin, end, scope; std::string_view section; };
        explicit TextConfigScope(std::string_view text, bool yaml = false) : text_(text)
        {
            struct Parent { std::size_t indent, scope; };
            std::vector<Parent> parents;
            std::string_view section;
            std::size_t scope{};
            for (std::size_t begin = 0; begin < text.size();) {
                auto end = text.find('\n', begin);
                if (end == std::string_view::npos) end = text.size();
                auto line = text.substr(begin, end - begin);
                if (begin == 0 && line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
                const auto indent = line.find_first_not_of(" \t\r");
                auto trimmed = Trim(line);
                if (!yaml && trimmed.starts_with('[')) {
                    const bool arrayTable = trimmed.starts_with("[[");
                    const auto close = trimmed.find(arrayTable ? "]]" : "]");
                    if (close != std::string_view::npos) {
                        auto tail = Trim(trimmed.substr(close + (arrayTable ? 2 : 1)));
                        if (tail.empty() || tail.starts_with(';') || tail.starts_with('#')) {
                            const auto start = arrayTable ? 2U : 1U;
                            section = Trim(trimmed.substr(start, close - start));
                            scope = begin + 1;
                        }
                    }
                }
                if (yaml && indent != std::string_view::npos && !trimmed.starts_with('#')) {
                    while (!parents.empty() && parents.back().indent >= indent) parents.pop_back();
                    scope = parents.empty() ? 0 : parents.back().scope;
                    if (trimmed.starts_with("- ")) {
                        scope = begin + 1;
                        parents.push_back({indent, scope});
                    } else if (const auto colon = trimmed.find(':'); colon != std::string_view::npos) {
                        const auto tail = Trim(trimmed.substr(colon + 1));
                        if (tail.empty() || tail.starts_with('#')) parents.push_back({indent, begin + 1});
                    }
                }
                lines_.push_back({begin, end, scope, section});
                begin = end + 1;
            }
        }
        const Line* At(std::size_t offset) const
        {
            const auto next = std::upper_bound(lines_.begin(), lines_.end(), offset,
                [](std::size_t at, const Line& line) { return at < line.begin; });
            return next == lines_.begin() ? nullptr : &*std::prev(next);
        }
        std::optional<std::size_t> FindSettingLine(std::string_view name, std::string_view section) const
        {
            std::optional<std::size_t> result;
            for (std::size_t i = 0; i < lines_.size(); ++i) {
                const auto& line = lines_[i];
                if (line.section != section) continue;
                auto text = Trim(text_.substr(line.begin, line.end - line.begin));
                if (line.begin == 0 && text.starts_with("\xEF\xBB\xBF")) text = Trim(text.substr(3));
                if (text.starts_with('"') || text.starts_with('\'')) text.remove_prefix(1);
                if (!text.starts_with(name)) continue;
                text.remove_prefix(name.size());
                if (text.starts_with('"') || text.starts_with('\'')) text.remove_prefix(1);
                text = Trim(text);
                if (!text.starts_with('=') && !text.starts_with(':')) continue;
                if (result) return std::nullopt;
                result = i + 1;
            }
            return result;
        }
        static std::string_view Trim(std::string_view value)
        {
            const auto begin = value.find_first_not_of(" \t\r");
            if (begin == std::string_view::npos) return {};
            return value.substr(begin, value.find_last_not_of(" \t\r") - begin + 1);
        }
    private:
        std::string_view text_;
        std::vector<Line> lines_;
    };
}
