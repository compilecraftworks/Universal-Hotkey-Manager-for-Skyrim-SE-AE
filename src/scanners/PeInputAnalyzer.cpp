#include "UHI/scanners/PeInputAnalyzer.h"
#include "UHI/PathEncoding.h"

#include "UHI/ActivationContextInference.h"
#include "UHI/ConfigBindingParser.h"

#include <Zydis/Zydis.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr std::uint16_t kAmd64Machine = 0x8664U;
    constexpr std::uint16_t kPe32Plus = 0x020BU;
    constexpr std::uint32_t kExecutableSection = 0x20000000U;
    constexpr std::size_t kMaximumSections = 96U;
    constexpr std::size_t kMaximumImports = 100'000U;
    constexpr std::size_t kMaximumExecutableBytes = 64U * 1024U * 1024U;
    constexpr std::size_t kMaximumFocusedCalls = 100'000U;
    constexpr std::size_t kCallWindowBytes = 96U;
    constexpr std::size_t kMaximumProcessEventBytes = 64U * 1024U;
    constexpr std::size_t kMaximumProcessEventInstructions = 16'384U;

    class RandomReader
    {
    public:
        explicit RandomReader(const std::filesystem::path& path) : input_(path, std::ios::binary)
        {
            std::error_code error;
            size_ = std::filesystem::file_size(path, error);
            valid_ = input_.is_open() && !error;
        }

        template <class T>
        bool At(const std::uint64_t offset, T& value)
        {
            return Bytes(offset, &value, sizeof(value));
        }

        bool Bytes(const std::uint64_t offset, void* destination, const std::size_t count)
        {
            if (!valid_ || offset > size_ || count > size_ - offset) return false;
            input_.clear();
            input_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!input_) return false;
            input_.read(static_cast<char*>(destination), static_cast<std::streamsize>(count));
            return static_cast<bool>(input_);
        }

        bool VectorAt(const std::uint64_t offset, const std::size_t count, std::vector<std::uint8_t>& result)
        {
            result.resize(count);
            return Bytes(offset, result.data(), count);
        }

        std::string CStringAt(const std::uint64_t offset, const std::size_t maximum = 512U)
        {
            if (!valid_ || offset >= size_) return {};
            const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(maximum, size_ - offset));
            std::vector<char> bytes(count);
            if (!Bytes(offset, bytes.data(), bytes.size())) return {};
            const auto end = std::find(bytes.begin(), bytes.end(), '\0');
            return end == bytes.end() ? std::string{} : std::string(bytes.begin(), end);
        }

        std::uint64_t Size() const noexcept { return size_; }
        bool Valid() const noexcept { return valid_; }

    private:
        std::ifstream input_;
        std::uint64_t size_{};
        bool valid_{};
    };

    struct Section
    {
        std::uint32_t virtualSize{};
        std::uint32_t rva{};
        std::uint32_t rawSize{};
        std::uint32_t rawOffset{};
        std::uint32_t characteristics{};
    };

    class PeImage
    {
    public:
        explicit PeImage(const std::filesystem::path& path) : reader_(path) {}

        bool Parse()
        {
            std::uint16_t dos{};
            std::uint32_t peOffset{}, signature{};
            if (!reader_.Valid() || !reader_.At(0, dos) || dos != 0x5A4DU ||
                !reader_.At(0x3C, peOffset) || peOffset > reader_.Size() ||
                !reader_.At(peOffset, signature) || signature != 0x00004550U) return false;
            std::uint16_t machine{}, sectionCount{}, optionalSize{};
            if (!reader_.At(peOffset + 4U, machine) || machine != kAmd64Machine ||
                !reader_.At(peOffset + 6U, sectionCount) || sectionCount == 0 || sectionCount > kMaximumSections ||
                !reader_.At(peOffset + 20U, optionalSize) || optionalSize < 128U) return false;
            const auto optional = static_cast<std::uint64_t>(peOffset) + 24U;
            std::uint16_t magic{};
            std::uint32_t directoryCount{};
            if (!reader_.At(optional, magic) || magic != kPe32Plus || !reader_.At(optional + 24U, imageBase_) ||
                !reader_.At(optional + 60U, sizeOfHeaders_) || !reader_.At(optional + 108U, directoryCount)) return false;
            if (directoryCount > 1) {
                if (!reader_.At(optional + 120U, importRva_) || !reader_.At(optional + 124U, importSize_)) return false;
            }
            if (directoryCount > 3) {
                if (!reader_.At(optional + 136U, exceptionRva_) ||
                    !reader_.At(optional + 140U, exceptionSize_)) return false;
            }
            const auto sectionTable = optional + optionalSize;
            sections_.reserve(sectionCount);
            for (std::uint32_t index = 0; index < sectionCount; ++index) {
                const auto entry = sectionTable + index * 40U;
                Section section;
                if (!reader_.At(entry + 8U, section.virtualSize) || !reader_.At(entry + 12U, section.rva) ||
                    !reader_.At(entry + 16U, section.rawSize) || !reader_.At(entry + 20U, section.rawOffset) ||
                    !reader_.At(entry + 36U, section.characteristics) ||
                    section.rawOffset > reader_.Size() || section.rawSize > reader_.Size() - section.rawOffset) return false;
                sections_.push_back(section);
            }
            return true;
        }

        std::optional<std::uint64_t> OffsetForRva(const std::uint32_t rva) const
        {
            if (rva < sizeOfHeaders_ && rva < reader_.Size()) return rva;
            for (const auto& section : sections_) {
                const auto extent = (std::max)(section.virtualSize, section.rawSize);
                if (rva < section.rva || static_cast<std::uint64_t>(rva) - section.rva >= extent) continue;
                const auto delta = static_cast<std::uint64_t>(rva) - section.rva;
                if (delta >= section.rawSize || section.rawOffset + delta >= reader_.Size()) return std::nullopt;
                return section.rawOffset + delta;
            }
            return std::nullopt;
        }

        bool ReadImports(std::unordered_map<std::uint32_t, std::string>& iatNames)
        {
            if (!importRva_ || importSize_ < 20U) return true;
            const auto maximumDescriptors = (std::min<std::size_t>)(importSize_ / 20U, 4096U);
            std::size_t totalImports{};
            for (std::size_t descriptor = 0; descriptor < maximumDescriptors; ++descriptor) {
                const auto descriptorRva = importRva_ + static_cast<std::uint32_t>(descriptor * 20U);
                const auto descriptorOffset = OffsetForRva(descriptorRva);
                if (!descriptorOffset) return false;
                std::array<std::uint32_t, 5> fields{};
                if (!reader_.Bytes(*descriptorOffset, fields.data(), sizeof(fields))) return false;
                if (std::ranges::all_of(fields, [](const auto value) { return value == 0; })) break;
                const auto originalThunk = fields[0] ? fields[0] : fields[4];
                const auto firstThunk = fields[4];
                if (!originalThunk || !firstThunk) continue;
                for (std::size_t thunkIndex = 0; totalImports < kMaximumImports; ++thunkIndex, ++totalImports) {
                    const auto thunkOffset = OffsetForRva(originalThunk + static_cast<std::uint32_t>(thunkIndex * 8U));
                    if (!thunkOffset) break;
                    std::uint64_t thunk{};
                    if (!reader_.At(*thunkOffset, thunk) || thunk == 0) break;
                    if ((thunk & 0x8000000000000000ULL) != 0) continue;
                    const auto nameOffset = OffsetForRva(static_cast<std::uint32_t>(thunk));
                    if (!nameOffset || *nameOffset + 2U >= reader_.Size()) continue;
                    auto name = reader_.CStringAt(*nameOffset + 2U);
                    if (!name.empty()) iatNames[firstThunk + static_cast<std::uint32_t>(thunkIndex * 8U)] = std::move(name);
                }
            }
            return true;
        }

        std::optional<std::pair<std::uint32_t, std::uint32_t>> FunctionRangeForRva(
            const std::uint32_t rva)
        {
            // x64 .pdata entries are IMAGE_RUNTIME_FUNCTION_ENTRY records.
            // They give an exact upper bound even when ProcessEvent has several
            // return blocks, avoiding a linear scan into an adjacent function.
            if (exceptionRva_ && exceptionSize_ >= 12U) {
                const auto count = (std::min<std::size_t>)(exceptionSize_ / 12U, 1'000'000U);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto offset = OffsetForRva(exceptionRva_ + static_cast<std::uint32_t>(index * 12U));
                    if (!offset) break;
                    std::array<std::uint32_t, 3> entry{};
                    if (!reader_.Bytes(*offset, entry.data(), sizeof(entry))) break;
                    if (entry[0] <= rva && rva < entry[1] && entry[1] > entry[0]) {
                        return std::pair{ entry[0], entry[1] };
                    }
                }
            }
            for (const auto& section : sections_) {
                if ((section.characteristics & kExecutableSection) == 0 ||
                    rva < section.rva || rva >= section.rva + section.rawSize) continue;
                const auto available = section.rva + section.rawSize - rva;
                return std::pair{ rva, rva + static_cast<std::uint32_t>(
                    (std::min<std::size_t>)(available, kMaximumProcessEventBytes)) };
            }
            return std::nullopt;
        }

        std::vector<std::pair<std::uint32_t, std::uint32_t>> RuntimeFunctions()
        {
            std::vector<std::pair<std::uint32_t, std::uint32_t>> result;
            if (!exceptionRva_ || exceptionSize_ < 12U) return result;
            const auto count = (std::min<std::size_t>)(exceptionSize_ / 12U, 250'000U);
            result.reserve(count);
            for (std::size_t index = 0; index < count; ++index) {
                const auto offset = OffsetForRva(exceptionRva_ + static_cast<std::uint32_t>(index * 12U));
                if (!offset) break;
                std::array<std::uint32_t, 3> entry{};
                if (!reader_.Bytes(*offset, entry.data(), sizeof(entry))) break;
                if (entry[1] <= entry[0] || entry[1] - entry[0] > kMaximumProcessEventBytes) continue;
                result.emplace_back(entry[0], entry[1]);
            }
            return result;
        }

        bool ReadRvaBytes(const std::uint32_t rva, const std::size_t count,
            std::vector<std::uint8_t>& result)
        {
            const auto offset = OffsetForRva(rva);
            return offset && reader_.VectorAt(*offset, count, result);
        }

        std::string CStringAtRva(const std::uint32_t rva, const std::size_t maximum = 256U)
        {
            const auto offset = OffsetForRva(rva);
            return offset ? reader_.CStringAt(*offset, maximum) : std::string{};
        }

        RandomReader& Reader() noexcept { return reader_; }
        const std::vector<Section>& Sections() const noexcept { return sections_; }
        std::uint64_t ImageBase() const noexcept { return imageBase_; }

    private:
        RandomReader reader_;
        std::vector<Section> sections_;
        std::uint64_t imageBase_{};
        std::uint32_t sizeOfHeaders_{};
        std::uint32_t importRva_{};
        std::uint32_t importSize_{};
        std::uint32_t exceptionRva_{};
        std::uint32_t exceptionSize_{};
    };

    struct RegisterConstant
    {
        std::uint64_t value{};
        std::size_t stamp{};
        bool known{};
    };

    int ArgumentRegister(const ZydisRegister value)
    {
        const auto enclosing = ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, value);
        if (enclosing == ZYDIS_REGISTER_RCX) return 0;
        if (enclosing == ZYDIS_REGISTER_RDX) return 1;
        if (enclosing == ZYDIS_REGISTER_R8) return 2;
        if (enclosing == ZYDIS_REGISTER_R9) return 3;
        return -1;
    }

    std::optional<std::uint64_t> Recent(const std::array<RegisterConstant, 4>& constants,
        const std::size_t index, const std::size_t stamp)
    {
        if (index >= constants.size() || !constants[index].known || stamp - constants[index].stamp > 32U) return std::nullopt;
        return constants[index].value;
    }

    void Clear(std::array<RegisterConstant, 4>& constants)
    {
        for (auto& value : constants) value = {};
    }

    void TrackWrite(const ZydisDecodedInstruction& instruction,
        const ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT], const std::size_t stamp,
        std::array<RegisterConstant, 4>& constants)
    {
        if (instruction.operand_count_visible == 0 || operands[0].type != ZYDIS_OPERAND_TYPE_REGISTER ||
            (operands[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) == 0) return;
        const auto target = ArgumentRegister(operands[0].reg.value);
        if (target < 0) return;
        auto& state = constants[static_cast<std::size_t>(target)];
        state = {};
        if (instruction.mnemonic == ZYDIS_MNEMONIC_MOV && instruction.operand_count_visible >= 2 &&
            operands[1].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && !operands[1].imm.is_relative) {
            state = { .value = operands[1].imm.value.u, .stamp = stamp, .known = true };
        } else if (instruction.mnemonic == ZYDIS_MNEMONIC_XOR && instruction.operand_count_visible >= 2 &&
            operands[1].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            ArgumentRegister(operands[1].reg.value) == target) {
            state = { .value = 0, .stamp = stamp, .known = true };
        }
    }

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    void AddResolved(std::vector<UHI::HotkeyRecord>& records, std::unordered_set<std::string>& seen,
        const std::filesystem::path& path, const std::string& api, const std::string& action,
        const std::string& binding,
        const std::string& device, const std::uint32_t key, const std::uint32_t rva,
        const bool eligible)
    {
        const auto identity = api + '\x1F' + binding;
        if (!seen.insert(identity).second) return;
        std::ostringstream raw;
        raw << api << " VK=0x" << std::hex << std::uppercase << key << " RVA=0x" << rva;
        records.push_back({
            .owner = UHI::PathToUtf8(path.stem()), .action = action,
            .binding = binding, .rawBinding = raw.str(), .codeSystem = "Windows virtual-key code", .device = device,
            .detector = "DllDisassemblyScanner",
            .confidence = eligible ? UHI::Confidence::inferred : UHI::Confidence::candidate,
            .evidencePath = path, .evidenceLine = 0, .stage = UHI::ScanStage::nativePlugins,
            .conflictEligible = eligible
        });
    }

    void InspectImportedCall(const std::filesystem::path& path, const std::string& api,
        const std::uint32_t instructionRva, const std::size_t stamp,
        const std::array<RegisterConstant, 4>& constants, std::vector<UHI::HotkeyRecord>& records,
        std::unordered_set<std::string>& seen)
    {
        const auto lowered = Lower(api);
        if (lowered == "getasynckeystate" || lowered == "getkeystate") {
            const auto key = Recent(constants, 0, stamp);
            if (!key || *key > 0xFFU) return;
            const auto parsed = UHI::ParseVirtualKeyCode(static_cast<std::uint32_t>(*key));
            // A key-state query alone does not reveal whether the value is the
            // trigger key, a modifier check, or ordinary UI/input state. Keep
            // the decoded value visible as read-only evidence, but never let it
            // create a conflict until runtime context can prove the role.
            AddResolved(records, seen, path, api, "Native key-state query candidate via " + api,
                parsed.binding, parsed.device,
                static_cast<std::uint32_t>(*key), instructionRva, false);
            return;
        }
        if (lowered == "registerhotkey") {
            const auto modifiers = Recent(constants, 2, stamp);
            const auto key = Recent(constants, 3, stamp);
            if (!key || *key > 0xFFU) return;
            const auto parsed = UHI::ParseVirtualKeyCode(static_cast<std::uint32_t>(*key));
            std::string binding;
            if (modifiers) {
                if ((*modifiers & 0x0002U) != 0) binding += "Ctrl+";
                if ((*modifiers & 0x0004U) != 0) binding += "Shift+";
                if ((*modifiers & 0x0001U) != 0) binding += "Alt+";
                if ((*modifiers & 0x0008U) != 0) binding += "Win+";
            }
            binding += parsed.binding;
            // RegisterHotKey proves the physical chord but does not preserve a
            // human-facing action label in the import call. Use the owning mod
            // name instead of presenting implementation plumbing as a feature.
            AddResolved(records, seen, path, api, UHI::PathToUtf8(path.stem()),
                binding, parsed.device,
                static_cast<std::uint32_t>(*key), instructionRva, parsed.conflictEligible);
        }
    }

    std::vector<UHI::HotkeyRecord> Analyze(const std::filesystem::path& path, PeImage& image,
        const std::unordered_map<std::uint32_t, std::string>& imports, const UHI::CancelCallback& cancel)
    {
        std::vector<UHI::HotkeyRecord> records;
        std::unordered_set<std::string> seen;
        ZydisDecoder decoder;
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) return records;
        std::size_t totalBytes{}, focusedCalls{};
        for (const auto& section : image.Sections()) {
            if (UHI::ScanCancelled(cancel) || (section.characteristics & kExecutableSection) == 0 || section.rawSize == 0) continue;
            if (section.rawSize > kMaximumExecutableBytes || totalBytes > kMaximumExecutableBytes - section.rawSize) break;
            totalBytes += section.rawSize;
            std::vector<std::uint8_t> code;
            if (!image.Reader().VectorAt(section.rawOffset, section.rawSize, code)) continue;
            for (std::size_t offset = 0; offset + 6U <= code.size() && focusedCalls < kMaximumFocusedCalls; ++offset) {
                if ((offset & 0xFFFFU) == 0 && UHI::ScanCancelled(cancel)) return records;
                // x64 import calls are encoded as CALL qword ptr [RIP+rel32].
                // Find those six bytes first, then disassemble only the small
                // argument-setup window preceding a relevant input API call.
                if (code[offset] != 0xFFU || code[offset + 1U] != 0x15U) continue;
                std::int32_t displacement{};
                std::memcpy(&displacement, code.data() + offset + 2U, sizeof(displacement));
                const auto target = static_cast<std::int64_t>(section.rva) +
                    static_cast<std::int64_t>(offset + 6U) + displacement;
                if (target < 0 || target > UINT32_MAX) continue;
                const auto imported = imports.find(static_cast<std::uint32_t>(target));
                if (imported == imports.end()) continue;
                const auto lowered = Lower(imported->second);
                if (lowered != "getasynckeystate" && lowered != "getkeystate" && lowered != "registerhotkey") continue;
                ++focusedCalls;

                const auto windowStart = offset > kCallWindowBytes ? offset - kCallWindowBytes : 0U;
                std::array<RegisterConstant, 4> best{};
                std::size_t bestSpan{}, bestStamp{};
                // The first byte of the window can bisect an x86 instruction.
                // Try the small set of possible alignments and retain the
                // longest valid instruction stream ending exactly at the call.
                const auto alignmentEnd = (std::min)(offset, windowStart + 15U);
                for (std::size_t start = windowStart; start <= alignmentEnd; ++start) {
                    std::array<RegisterConstant, 4> constants{};
                    std::size_t position = start, stamp{};
                    bool valid = true;
                    while (position < offset) {
                        ZydisDecodedInstruction instruction{};
                        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
                        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, code.data() + position,
                                offset - position, &instruction, operands)) || instruction.length == 0 ||
                            position + instruction.length > offset) {
                            valid = false;
                            break;
                        }
                        ++stamp;
                        TrackWrite(instruction, operands, stamp, constants);
                        const auto category = instruction.meta.category;
                        if (instruction.mnemonic == ZYDIS_MNEMONIC_CALL || category == ZYDIS_CATEGORY_COND_BR ||
                            category == ZYDIS_CATEGORY_UNCOND_BR || category == ZYDIS_CATEGORY_RET) Clear(constants);
                        position += instruction.length;
                    }
                    if (valid && position == offset && offset - start > bestSpan) {
                        bestSpan = offset - start;
                        best = constants;
                        bestStamp = stamp;
                    }
                }
                if (bestSpan != 0) {
                    InspectImportedCall(path, imported->second,
                        section.rva + static_cast<std::uint32_t>(offset), bestStamp, best, records, seen);
                }
            }
        }
        return records;
    }

    enum class EventField : std::uint8_t
    {
        none,
        device,
        eventType,
        idCode,
        value,
        heldDuration
    };

    ZydisRegister FullRegister(const ZydisRegister value)
    {
        return value == ZYDIS_REGISTER_NONE ? value :
            ZydisRegisterGetLargestEnclosing(ZYDIS_MACHINE_MODE_LONG_64, value);
    }

    EventField FieldForDisplacement(const ZyanI64 displacement) noexcept
    {
        switch (displacement) {
        case 0x08: return EventField::device;
        case 0x0C: return EventField::eventType;
        case 0x20: return EventField::idCode;
        case 0x28: return EventField::value;
        case 0x2C: return EventField::heldDuration;
        default: return EventField::none;
        }
    }

    EventField OperandField(const ZydisDecodedOperand& operand,
        const std::unordered_map<ZydisRegister, EventField>& registerFields)
    {
        if (operand.type == ZYDIS_OPERAND_TYPE_REGISTER) {
            if (const auto found = registerFields.find(FullRegister(operand.reg.value));
                found != registerFields.end()) return found->second;
        }
        if (operand.type == ZYDIS_OPERAND_TYPE_MEMORY && operand.mem.disp.has_displacement) {
            return FieldForDisplacement(operand.mem.disp.value);
        }
        return EventField::none;
    }

    std::optional<std::uint64_t> ImmediateValue(const ZydisDecodedOperand& operand)
    {
        if (operand.type != ZYDIS_OPERAND_TYPE_IMMEDIATE || operand.imm.is_relative) return std::nullopt;
        return operand.imm.value.u;
    }

    bool IsPrintableEvidence(const std::string& value)
    {
        if (value.size() < 3U || value.size() > 160U) return false;
        std::size_t letters{};
        for (const unsigned char character : value) {
            if (character < 0x20U || character > 0x7EU) return false;
            if (std::isalnum(character) != 0) ++letters;
        }
        return letters >= 3U;
    }

    std::string JoinEvidence(const std::vector<std::string>& strings)
    {
        std::string result;
        for (const auto& value : strings) {
            if (!result.empty()) result.push_back(' ');
            result += value;
            if (result.size() >= 4096U) break;
        }
        return result;
    }

    std::string SuggestedAction(const std::filesystem::path& path,
        const std::vector<std::string>& strings)
    {
        static constexpr std::array<std::string_view, 20> implementationWords{
            "menu", "context", "controlmap", "inputevent", "buttonevent", "process",
            "commonlib", "skse", "error", "failed", "assert", "source", "\\", "/",
            ".cpp", ".h", "rtti", "class ", "struct ", "typeinfo"
        };
        for (const auto& candidate : strings) {
            const auto lowered = Lower(candidate);
            if (candidate.size() < 4U || candidate.size() > 72U ||
                std::ranges::any_of(implementationWords,
                    [&](const auto word) { return lowered.find(word) != std::string::npos; })) continue;
            const bool labelLike = std::ranges::all_of(candidate, [](const unsigned char character) {
                return std::isalnum(character) != 0 || character == ' ' || character == '_' ||
                    character == '-' || character == ':';
            });
            if (labelLike) return candidate;
        }
        return UHI::PathToUtf8(path.stem());
    }

    struct NumericEvidence
    {
        std::uint32_t value{};
        std::size_t stamp{};
    };

    std::optional<std::uint32_t> NearestDevice(const std::vector<NumericEvidence>& devices,
        const std::size_t stamp)
    {
        const NumericEvidence* best{};
        std::size_t distance = 129U;
        bool ambiguous{};
        for (const auto& device : devices) {
            const auto current = device.stamp > stamp ? device.stamp - stamp : stamp - device.stamp;
            if (current > 128U) continue;
            if (current < distance) {
                best = &device;
                distance = current;
                ambiguous = false;
            } else if (current == distance && best && best->value != device.value) {
                ambiguous = true;
            }
        }
        return best && !ambiguous ? std::optional<std::uint32_t>(best->value) : std::nullopt;
    }

    UHI::ParsedConfigBinding ParseButtonId(const std::uint32_t idCode,
        const std::optional<std::uint32_t> device)
    {
        if (!device) {
            return { .binding = "idCode " + std::to_string(idCode) + " (device unknown)",
                .device = "unknown", .codeSystem = "CommonLib ButtonEvent idCode",
                .conflictEligible = false };
        }
        if (*device == 0U) {
            return UHI::ParseConfigBinding("Keyboard", std::to_string(idCode),
                UHI::NumericCodeSpace::directInputScanCode);
        }
        if (*device == 1U) {
            return UHI::ParseControlMapInputCode(std::to_string(idCode), "mouse");
        }
        if (*device == 2U) {
            return UHI::ParseControlMapInputCode(std::to_string(idCode), "gamepad");
        }
        return { .binding = "idCode " + std::to_string(idCode) + " (device unknown)",
            .device = "unknown", .codeSystem = "CommonLib ButtonEvent idCode",
            .conflictEligible = false };
    }

    std::vector<UHI::HotkeyRecord> AnalyzeProcessEvent(const UHI::Scanners::ActiveInputSinkTarget& target,
        PeImage& image, const UHI::CancelCallback& cancel, const bool provenActive = true)
    {
        std::vector<UHI::HotkeyRecord> records;
        const auto range = target.processEventEndRva > target.processEventRva ?
            std::optional<std::pair<std::uint32_t, std::uint32_t>>{
                std::pair{ target.processEventRva, target.processEventEndRva } } :
            image.FunctionRangeForRva(target.processEventRva);
        if (!range || range->second <= range->first) return records;
        const auto byteCount = static_cast<std::size_t>(range->second - range->first);
        if (byteCount == 0 || byteCount > kMaximumProcessEventBytes) return records;
        std::vector<std::uint8_t> code;
        if (!image.ReadRvaBytes(range->first, byteCount, code)) return records;

        ZydisDecoder decoder;
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64,
                ZYDIS_STACK_WIDTH_64))) return records;
        std::unordered_map<ZydisRegister, EventField> registerFields;
        std::array<RegisterConstant, 4> argumentConstants{};
        std::vector<NumericEvidence> devices;
        std::vector<NumericEvidence> keys;
        std::vector<std::uint32_t> controlMapContexts;
        std::vector<std::string> strings;
        std::unordered_set<std::string> seenStrings;
        bool sawButtonEventType{};
        std::size_t offset{}, stamp{};
        while (offset < code.size() && stamp < kMaximumProcessEventInstructions) {
            if ((stamp & 0x3FFU) == 0 && UHI::ScanCancelled(cancel)) return {};
            ZydisDecodedInstruction instruction{};
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, code.data() + offset,
                    code.size() - offset, &instruction, operands)) || instruction.length == 0) break;
            ++stamp;
            const auto instructionRva = range->first + static_cast<std::uint32_t>(offset);

            for (std::uint8_t index = 0; index < instruction.operand_count_visible; ++index) {
                const auto& operand = operands[index];
                if (operand.type != ZYDIS_OPERAND_TYPE_MEMORY ||
                    operand.mem.base != ZYDIS_REGISTER_RIP || !operand.mem.disp.has_displacement) continue;
                const auto reference = static_cast<std::int64_t>(instructionRva) + instruction.length +
                    operand.mem.disp.value;
                if (reference < 0 || reference > UINT32_MAX) continue;
                auto value = image.CStringAtRva(static_cast<std::uint32_t>(reference));
                if (IsPrintableEvidence(value) && seenStrings.insert(value).second) strings.push_back(std::move(value));
            }

            if ((instruction.mnemonic == ZYDIS_MNEMONIC_CMP ||
                    instruction.mnemonic == ZYDIS_MNEMONIC_TEST) && instruction.operand_count_visible >= 2) {
                for (std::uint8_t fieldIndex = 0; fieldIndex < 2; ++fieldIndex) {
                    const auto field = OperandField(operands[fieldIndex], registerFields);
                    const auto immediate = ImmediateValue(operands[1U - fieldIndex]);
                    if (!immediate) continue;
                    if (field == EventField::device && *immediate <= 2U) {
                        devices.push_back({ static_cast<std::uint32_t>(*immediate), stamp });
                    } else if (field == EventField::eventType && *immediate == 0U) {
                        sawButtonEventType = true;
                    } else if (field == EventField::idCode && *immediate > 0U && *immediate <= 0xFFFFU) {
                        keys.push_back({ static_cast<std::uint32_t>(*immediate), stamp });
                    }
                }
            }

            TrackWrite(instruction, operands, stamp, argumentConstants);
            if (instruction.mnemonic == ZYDIS_MNEMONIC_CALL) {
                if (const auto context = Recent(argumentConstants, 3, stamp);
                    context && *context <= 18U) controlMapContexts.push_back(static_cast<std::uint32_t>(*context));
                Clear(argumentConstants);
            }

            if (instruction.operand_count_visible > 0 &&
                operands[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
                (operands[0].actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) != 0) {
                const auto destination = FullRegister(operands[0].reg.value);
                registerFields.erase(destination);
                if ((instruction.mnemonic == ZYDIS_MNEMONIC_MOV ||
                        instruction.mnemonic == ZYDIS_MNEMONIC_MOVZX ||
                        instruction.mnemonic == ZYDIS_MNEMONIC_MOVSX ||
                        instruction.mnemonic == ZYDIS_MNEMONIC_MOVSXD) &&
                    instruction.operand_count_visible >= 2) {
                    const auto sourceField = OperandField(operands[1], registerFields);
                    if (sourceField != EventField::none) registerFields[destination] = sourceField;
                }
            }
            offset += instruction.length;
        }
        if (keys.empty()) return records;

        auto contextEvidence = JoinEvidence(strings);
        const auto loweredEvidence = Lower(contextEvidence);
        const bool menuReference = loweredEvidence.find("menu") != std::string::npos;
        if (menuReference) contextEvidence = "IsMenuOpen " + contextEvidence;
        if (!controlMapContexts.empty() &&
            (loweredEvidence.find("controlmap") != std::string::npos ||
                loweredEvidence.find("inputcontext") != std::string::npos)) {
            for (const auto context : controlMapContexts) {
                static constexpr std::array<std::string_view, 19> contextNames{
                    "k Gameplay", "k MenuMode", "k Console", "k ItemMenu", "k Inventory",
                    "k Favorites", "k Map", "k Stats", "k Cursor", "k Book", "k DebugText",
                    "k Favorites", "k Journal", "k TFCMode", "k MapDebug", "k Lockpicking",
                    "k Creations menu", "k Favor", "k DebugOverlay"
                };
                if (context < contextNames.size()) {
                    contextEvidence += " inputContext ";
                    contextEvidence += contextNames[context];
                }
            }
        }
        auto context = UHI::InferActivationContext(contextEvidence,
            UHI::ContextEvidenceSource::nativeBinary);
        if (context.mask == 0) {
            // A currently registered handler with a reachable physical-key
            // comparison is an active mod input path. Unknown feature flags do
            // not make it an unknown input context; absent menu evidence, keep
            // it as a conservative global/conditional binding.
            context.mask = static_cast<std::uint32_t>(UHI::ActivationContext::global);
            context.confidence = UHI::ContextConfidence::inferred;
        }

        std::unordered_set<std::string> seen;
        for (const auto& key : keys) {
            const auto device = NearestDevice(devices, key.stamp);
            auto parsed = ParseButtonId(key.value, device);
            if (parsed.binding.empty()) continue;
            const auto identity = parsed.device + '\x1F' + parsed.binding;
            if (!seen.insert(identity).second) continue;
            std::ostringstream raw;
            raw << (provenActive ? "active InputEvent sink" : "static CommonLib ButtonEvent handler")
                << "; ProcessEvent RVA=0x" << std::hex << std::uppercase
                << target.processEventRva << "; idCode=0x" << key.value;
            if (device) raw << "; device=" << std::dec << *device;
            if (sawButtonEventType) raw << "; ButtonEvent type checked";
            if (!strings.empty()) raw << "; context evidence=" << JoinEvidence(strings);
            records.push_back({
                .owner = UHI::PathToUtf8(target.modulePath.stem()),
                .action = SuggestedAction(target.modulePath, strings),
                .binding = std::move(parsed.binding), .rawBinding = raw.str(),
                .codeSystem = std::move(parsed.codeSystem), .device = std::move(parsed.device),
                .detector = provenActive ? "ActiveInputSinkAnalyzer" : "StaticCommonLibInputHandler",
                .confidence = UHI::Confidence::inferred,
                .evidencePath = target.modulePath, .evidenceLine = target.processEventRva,
                .stage = UHI::ScanStage::nativePlugins, .editable = false, .runtimeActive = true,
                .conflictEligible = parsed.conflictEligible, .contextMask = context.mask,
                .contextConfidence = context.confidence
            });
        }
        return records;
    }
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> PeInputAnalyzer::Scan(const std::filesystem::path& dll,
        const CancelCallback& cancel) const noexcept
    {
        try {
            PeImage image(dll);
            std::unordered_map<std::uint32_t, std::string> imports;
            if (ScanCancelled(cancel) || !image.Parse() || !image.ReadImports(imports)) return {};
            return Analyze(dll, image, imports, cancel);
        } catch (...) {
            return {};
        }
    }

    std::vector<HotkeyRecord> PeInputAnalyzer::ScanStaticInputHandlers(
        const std::filesystem::path& dll, const CancelCallback& cancel) const noexcept
    {
        try {
            PeImage image(dll);
            if (ScanCancelled(cancel) || !image.Parse()) return {};
            std::vector<HotkeyRecord> records;
            std::unordered_set<std::string> seen;
            for (const auto& range : image.RuntimeFunctions()) {
                if (ScanCancelled(cancel)) return records;
                ActiveInputSinkTarget target{ .modulePath = dll,
                    .processEventRva = range.first, .processEventEndRva = range.second };
                auto found = AnalyzeProcessEvent(target, image, cancel, false);
                std::erase_if(found, [](const HotkeyRecord& record) {
                    // Static code can contain unrelated comparisons. Requiring
                    // the exact ButtonEvent discriminator keeps this deep pass
                    // conservative while still finding hard-coded CommonLib
                    // shortcuts such as Typing Mode's default key.
                    return record.rawBinding.find("ButtonEvent type checked") == std::string::npos;
                });
                for (auto& record : found) {
                    const auto identity = record.device + '\x1F' + NormalizeBinding(record.binding);
                    if (!seen.insert(identity).second) continue;
                    records.push_back(std::move(record));
                    if (records.size() >= kMaximumCollectedRecords) return records;
                }
                if (records.size() >= kMaximumCollectedRecords) break;
            }
            return records;
        } catch (...) {
            return {};
        }
    }

    std::vector<HotkeyRecord> PeInputAnalyzer::ScanActiveInputSinks(
        const std::vector<ActiveInputSinkTarget>& targets, const CancelCallback& cancel) const noexcept
    {
        try {
            std::vector<HotkeyRecord> records;
            std::unordered_set<std::string> seenTargets;
            for (const auto& target : targets) {
                if (ScanCancelled(cancel) || target.modulePath.empty() || target.processEventRva == 0) break;
                const auto identity = UHI::PathToUtf8(target.modulePath.lexically_normal()) + ':' +
                    std::to_string(target.processEventRva);
                if (!seenTargets.insert(Lower(identity)).second) continue;
                PeImage image(target.modulePath);
                if (!image.Parse()) continue;
                auto found = AnalyzeProcessEvent(target, image, cancel);
                UHI::AppendScanResults(records, found);
            }
            return records;
        } catch (...) {
            return {};
        }
    }
}
