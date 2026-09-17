#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace UHI
{
    inline bool IsDedicatedBindingName(std::string_view name)
    {
        std::string value;
        for (unsigned char c : name) if (std::isalnum(c)) value += static_cast<char>(std::tolower(c));
        return value == "hotkey" || value == "hotkeys" || value == "keybinds" ||
            value == "keybindings" || value == "bindings" || value == "shortcuts";
    }

    // Build a linear scope index once per document. Quoted strings and JSON
    // comments cannot create fake scopes. YAML uses indentation, not proximity
    // to a previous header, so sibling non-input settings are not captured.
    class ConfigBindingScope
    {
    public:
        ConfigBindingScope(std::string_view content, bool yaml)
        {
            if (yaml) {
                struct Parent { std::size_t indent; bool active; };
                std::vector<Parent> stack;
                for (std::size_t start = 0; start < content.size();) {
                    const auto end = content.find('\n', start);
                    const auto line = content.substr(start, end == std::string_view::npos ? content.size() - start : end - start);
                    const auto indent = line.find_first_not_of(" \t\r");
                    if (indent != std::string_view::npos && line[indent] != '#') {
                        while (!stack.empty() && stack.back().indent >= indent) stack.pop_back();
                        const bool active = !stack.empty() && stack.back().active;
                        events_.push_back({ start, active });
                        if (const auto colon = line.find(':', indent); colon != std::string_view::npos) {
                            const auto tail = line.find_first_not_of(" \t\r", colon + 1U);
                            if (tail == std::string_view::npos || line[tail] == '#') {
                                const auto name = line.substr(indent, colon - indent);
                                stack.push_back({ indent, active || IsDedicatedBindingName(name) });
                            }
                        }
                    }
                    if (end == std::string_view::npos) break;
                    start = end + 1U;
                }
                return;
            }
            std::vector<bool> stack;
            std::string_view property;
            for (std::size_t i = 0; i < content.size(); ++i) {
                const auto c = content[i];
                if (c == '/' && i + 1U < content.size()) {
                    if (content[i + 1U] == '/') { i = content.find('\n', i); if (i == std::string_view::npos) break; continue; }
                    if (content[i + 1U] == '*') { i = content.find("*/", i + 2U); if (i == std::string_view::npos) break; ++i; continue; }
                }
                if (c == '"') {
                    const auto start = ++i;
                    for (; i < content.size(); ++i) {
                        if (content[i] == '\\') { ++i; continue; }
                        if (content[i] == '"') break;
                    }
                    const auto next = content.find_first_not_of(" \r\n\t", i + 1U);
                    if (next != std::string_view::npos && content[next] == ':') property = content.substr(start, i - start);
                    continue;
                }
                if (c == '{' || c == '[') {
                    const bool active = (!stack.empty() && stack.back()) || IsDedicatedBindingName(property);
                    stack.push_back(active);
                    events_.push_back({ i + 1U, active });
                    property = {};
                } else if (c == '}' || c == ']') {
                    if (!stack.empty()) stack.pop_back();
                    events_.push_back({ i, !stack.empty() && stack.back() });
                    property = {};
                } else if (c == ',') property = {};
            }
        }
        [[nodiscard]] bool Contains(std::size_t offset) const
        {
            const auto next = std::upper_bound(events_.begin(), events_.end(), offset,
                [](std::size_t value, const auto& event) { return value < event.first; });
            return next != events_.begin() && std::prev(next)->second;
        }
    private:
        std::vector<std::pair<std::size_t, bool>> events_;
    };
}
