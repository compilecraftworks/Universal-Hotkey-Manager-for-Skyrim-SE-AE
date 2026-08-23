#pragma once

#include <filesystem>
#include <string>

namespace UHI::Writers
{
    class ControlMapWriter
    {
    public:
        // Rewrites one keyboard/mouse/gamepad binding after creating the first
        // .uhi.bak copy. Keyboard uses DirectInput scan codes, mouse uses its
        // zero-based button IDs and gamepad uses Skyrim/XInput masks. Decimal
        // and hex input are validated and saved as canonical hexadecimal.
        [[nodiscard]] bool SetBinding(const std::filesystem::path& path,
            std::size_t lineNumber, const std::string& device,
            const std::string& newBinding) const;
    };
}
