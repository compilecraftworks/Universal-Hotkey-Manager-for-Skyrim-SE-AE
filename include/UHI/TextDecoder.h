#pragma once

#include <span>
#include <string>

namespace UHI
{
    // Decodes UTF-8/ASCII and UTF-16 LE/BE files to UTF-8. A large number of
    // older Skyrim tools write INI/XML files as UTF-16 even without a BOM.
    [[nodiscard]] std::string DecodeText(std::span<const char> bytes);
}
