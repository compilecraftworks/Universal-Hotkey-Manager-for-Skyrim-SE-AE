#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace UHI
{
    // A source-preserving JSON/JSONC index. Values are never reserialized, so
    // comments, spacing and unrelated options survive an individual edit.
    class JsonConfigDocument
    {
    public:
        struct Member
        {
            std::string name, locator;
            std::size_t offset{}, begin{}, end{}, line{};
            bool quoted{};
        };

        explicit JsonConfigDocument(std::string_view text) : text_(text)
        {
            if (text_.starts_with("\xEF\xBB\xBF")) cursor_ = 3;
            valid_ = Value({}, 0) && Space() && cursor_ == text_.size();
            if (!valid_) members_.clear();
            std::ranges::sort(members_, {}, &Member::offset);
            std::size_t position{}, line = 1;
            for (auto& member : members_) {
                while (position < member.offset) if (text_[position++] == '\n') ++line;
                member.line = line;
            }
        }

        bool Valid() const noexcept { return valid_; }
        const Member* At(std::size_t offset, std::string_view name) const
        {
            const auto found = std::lower_bound(members_.begin(), members_.end(), offset,
                [](const Member& member, std::size_t value) { return member.offset < value; });
            return found != members_.end() && found->offset == offset && found->name == name ? &*found : nullptr;
        }
        const Member* Find(std::string_view name, std::size_t line, std::string_view locator = {}) const
        {
            const Member* found{};
            for (const auto& member : members_) {
                if (member.name != name || (locator.empty() ? member.line != line : member.locator != locator)) continue;
                if (found) return nullptr; // A duplicate JSON property has no unique meaning.
                found = &member;
            }
            return found;
        }

    private:
        static std::string Component(std::string_view name)
        {
            std::string result;
            for (char c : name) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
            return result;
        }
        bool Space()
        {
            for (;;) {
                while (cursor_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
                if (text_.substr(cursor_, 2) == "//") {
                    const auto end = text_.find('\n', cursor_ + 2);
                    cursor_ = end == std::string_view::npos ? text_.size() : end;
                } else if (text_.substr(cursor_, 2) == "/*") {
                    const auto end = text_.find("*/", cursor_ + 2);
                    if (end == std::string_view::npos) return false;
                    cursor_ = end + 2;
                } else return true;
            }
        }
        bool Hex(unsigned& value)
        {
            value = 0;
            for (int i = 0; i < 4; ++i) {
                if (cursor_ == text_.size()) return false;
                const auto c = text_[cursor_++];
                const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                    c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0) return false;
                value = value * 16 + static_cast<unsigned>(digit);
            }
            return true;
        }
        static void Utf8(std::string& result, unsigned code)
        {
            if (code < 0x80) result += static_cast<char>(code);
            else if (code < 0x800) {
                result += static_cast<char>(0xC0 | (code >> 6));
                result += static_cast<char>(0x80 | (code & 63));
            } else if (code < 0x10000) {
                result += static_cast<char>(0xE0 | (code >> 12));
                result += static_cast<char>(0x80 | ((code >> 6) & 63));
                result += static_cast<char>(0x80 | (code & 63));
            } else {
                result += static_cast<char>(0xF0 | (code >> 18));
                result += static_cast<char>(0x80 | ((code >> 12) & 63));
                result += static_cast<char>(0x80 | ((code >> 6) & 63));
                result += static_cast<char>(0x80 | (code & 63));
            }
        }
        bool String(std::string& result)
        {
            if (cursor_ == text_.size() || text_[cursor_++] != '"') return false;
            while (cursor_ < text_.size()) {
                const auto c = text_[cursor_++];
                if (c == '"') return true;
                if (static_cast<unsigned char>(c) < 32) return false;
                if (c != '\\') { result += c; continue; }
                if (cursor_ == text_.size()) return false;
                const auto escaped = text_[cursor_++];
                if (escaped == 'u') {
                    unsigned code{};
                    if (!Hex(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (text_.substr(cursor_, 2) != "\\u") return false;
                        cursor_ += 2;
                        unsigned low{};
                        if (!Hex(low) || low < 0xDC00 || low > 0xDFFF) return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                    } else if (code >= 0xDC00 && code <= 0xDFFF) return false;
                    Utf8(result, code);
                } else {
                    switch (escaped) {
                    case '"': case '\\': case '/': result += escaped; break;
                    case 'b': result += '\b'; break;
                    case 'f': result += '\f'; break;
                    case 'n': result += '\n'; break;
                    case 'r': result += '\r'; break;
                    case 't': result += '\t'; break;
                    default: return false;
                    }
                }
            }
            return false;
        }
        bool Value(const std::string& path, unsigned depth)
        {
            if (depth > 64 || !Space() || cursor_ == text_.size() || members_.size() > 100'000) return false;
            const char first = text_[cursor_];
            if (first == '{' || first == '[') {
                ++cursor_;
                const char close = first == '{' ? '}' : ']';
                std::size_t index{};
                if (!Space()) return false;
                while (cursor_ < text_.size() && text_[cursor_] != close) {
                    Member member;
                    member.offset = cursor_;
                    if (first == '{') {
                        if (!String(member.name) || !Space() || cursor_ == text_.size() || text_[cursor_++] != ':') return false;
                        member.locator = path + '/' + Component(member.name);
                    } else member.locator = path + '/' + std::to_string(index++);
                    if (!Space() || cursor_ == text_.size()) return false;
                    member.begin = cursor_;
                    member.quoted = text_[cursor_] == '"';
                    if (!Value(member.locator, depth + 1)) return false;
                    member.end = cursor_;
                    if (member.quoted) { ++member.begin; --member.end; }
                    if (first == '{') members_.push_back(std::move(member));
                    if (!Space() || cursor_ == text_.size()) return false;
                    if (text_[cursor_] == close) break;
                    if (text_[cursor_++] != ',' || !Space()) return false;
                }
                return cursor_ < text_.size() && text_[cursor_++] == close;
            }
            if (first == '"') { std::string ignored; return String(ignored); }
            const auto start = cursor_;
            while (cursor_ < text_.size() && text_[cursor_] != ',' && text_[cursor_] != '}' &&
                text_[cursor_] != ']' && text_[cursor_] != '/' &&
                !std::isspace(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
            const auto token = text_.substr(start, cursor_ - start);
            if (token == "true" || token == "false" || token == "null") return true;
            // Validate JSON numbers without converting or losing precision.
            std::size_t i{};
            if (i < token.size() && token[i] == '-') ++i;
            if (i == token.size()) return false;
            if (token[i] == '0') ++i;
            else {
                if (token[i] < '1' || token[i] > '9') return false;
                while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) ++i;
            }
            if (i < token.size() && token[i] == '.') {
                const auto digits = ++i;
                while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) ++i;
                if (i == digits) return false;
            }
            if (i < token.size() && (token[i] == 'e' || token[i] == 'E')) {
                ++i;
                if (i < token.size() && (token[i] == '+' || token[i] == '-')) ++i;
                const auto digits = i;
                while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) ++i;
                if (i == digits) return false;
            }
            return i == token.size();
        }

        std::string_view text_;
        std::size_t cursor_{};
        bool valid_{};
        std::vector<Member> members_;
    };
}
