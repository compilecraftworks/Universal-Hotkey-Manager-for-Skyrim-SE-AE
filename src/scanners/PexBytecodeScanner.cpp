#include "UHI/scanners/PexBytecodeScanner.h"
#include "UHI/PathEncoding.h"

#include "UHI/ConfigBindingParser.h"
#include "UHI/ActivationContextInference.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    constexpr std::size_t kMaximumPexBytes = 16U * 1024U * 1024U;
    constexpr std::size_t kMaximumInstructions = 1'000'000U;
    constexpr std::size_t kMaximumVarArgs = 4096U;
    constexpr std::size_t kMaximumRecords = 4096U;
    constexpr std::array<std::uint8_t, 4> kSkyrimMagic{ 0xFA, 0x57, 0xC0, 0xDE };

    enum class ValueKind : std::uint8_t
    {
        none,
        identifier,
        string,
        integer,
        other
    };

    struct Value
    {
        ValueKind kind{ ValueKind::none };
        std::uint16_t index{};
        std::int32_t integer{};
    };

    // Keeps the identity of the setting that produced a temporary PEX value.
    // MCM scripts almost always compile `SomeHotkey` to PROPGET -> ::tempN ->
    // AddKeyMapOption.  The integer is save-backed and therefore cannot be
    // recovered from the PEX alone, but the property identity can and is later
    // joined to the live registered MCM instance by the SKSE plugin.
    struct ValueOrigin
    {
        std::string settingName;
        std::string settingSection;
    };

    using OriginMap = std::unordered_map<std::uint16_t, ValueOrigin>;

    class Reader
    {
    public:
        explicit Reader(const std::string_view bytes) : bytes_(bytes) {}

        bool Bytes(const std::size_t count, std::string_view& result)
        {
            if (!CanRead(count)) return false;
            result = bytes_.substr(position_, count);
            position_ += count;
            return true;
        }

        bool Skip(const std::size_t count)
        {
            if (!CanRead(count)) return false;
            position_ += count;
            return true;
        }

        bool U8(std::uint8_t& value)
        {
            if (!CanRead(1)) return false;
            value = static_cast<std::uint8_t>(bytes_[position_++]);
            return true;
        }

        bool U16(std::uint16_t& value)
        {
            if (!CanRead(2)) return false;
            const auto* data = reinterpret_cast<const unsigned char*>(bytes_.data() + position_);
            value = static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8U) | data[1]);
            position_ += 2;
            return true;
        }

        bool U32(std::uint32_t& value)
        {
            if (!CanRead(4)) return false;
            const auto* data = reinterpret_cast<const unsigned char*>(bytes_.data() + position_);
            value = (static_cast<std::uint32_t>(data[0]) << 24U) |
                (static_cast<std::uint32_t>(data[1]) << 16U) |
                (static_cast<std::uint32_t>(data[2]) << 8U) | data[3];
            position_ += 4;
            return true;
        }

        bool U64(std::uint64_t& value)
        {
            std::uint32_t high{}, low{};
            if (!U32(high) || !U32(low)) return false;
            value = (static_cast<std::uint64_t>(high) << 32U) | low;
            return true;
        }

        bool String(std::string& value)
        {
            std::uint16_t length{};
            std::string_view data;
            if (!U16(length) || !Bytes(length, data)) return false;
            value.assign(data);
            return true;
        }

        bool Good() const noexcept { return position_ <= bytes_.size(); }

    private:
        bool CanRead(const std::size_t count) const noexcept
        {
            return position_ <= bytes_.size() && count <= bytes_.size() - position_;
        }

        std::string_view bytes_;
        std::size_t position_{};
    };

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    bool IsImplementationAction(const std::string& action)
    {
        const auto lowered = Lower(action);
        return lowered.starts_with("registerforkey") || lowered.starts_with("addkeymapoption") ||
            lowered.starts_with("setkeymapoptionvalue");
    }

    class Parser
    {
    public:
        Parser(const std::filesystem::path& source, const std::string_view bytes) : source_(source), reader_(bytes) {}

        std::vector<UHI::HotkeyRecord> Parse()
        {
            std::string_view magic;
            if (!reader_.Bytes(kSkyrimMagic.size(), magic) ||
                std::memcmp(magic.data(), kSkyrimMagic.data(), kSkyrimMagic.size()) != 0) return {};
            std::uint8_t major{}, minor{};
            std::uint16_t gameId{};
            std::uint64_t compiled{};
            std::string ignored;
            if (!reader_.U8(major) || !reader_.U8(minor) || !reader_.U16(gameId) || !reader_.U64(compiled) ||
                !reader_.String(ignored) || !reader_.String(ignored) || !reader_.String(ignored) ||
                !ReadStringTable() || !SkipDebugInfo() || !SkipUserFlags() || !ReadObjects()) return {};
            if (!reader_.Good()) return {};
            FinalizeActions();
            return std::move(records_);
        }

    private:
        void FinalizeActions()
        {
            // RegisterForKey/SetKeyMapOptionValue and AddKeyMapOption commonly
            // describe the same MCM option. Keeping all of those plumbing
            // calls as separate actions creates a false self-conflict. Prefer
            // the human-facing MCM label when the same owner and binding has
            // one, and use the script/mod owner when no semantic label exists.
            const auto recordIdentity = [](const UHI::HotkeyRecord& record) {
                if (!record.settingName.empty()) {
                    return Lower(record.owner) + '\x1F' + Lower(record.settingName);
                }
                return Lower(record.owner) + '\x1F' + record.device + '\x1F' +
                    UHI::NormalizeBinding(record.binding);
            };
            std::unordered_set<std::string> labelledBindings;
            for (const auto& record : records_) {
                if (IsImplementationAction(record.action)) continue;
                labelledBindings.insert(recordIdentity(record));
            }
            std::erase_if(records_, [&](const auto& record) {
                return IsImplementationAction(record.action) && labelledBindings.contains(recordIdentity(record));
            });
            std::unordered_set<std::string> implementationBindings;
            std::erase_if(records_, [&](const auto& record) {
                if (!IsImplementationAction(record.action) || record.settingName.empty()) return false;
                return !implementationBindings.insert(recordIdentity(record)).second;
            });
            for (auto& record : records_) {
                if (IsImplementationAction(record.action)) record.action = record.owner;
            }
        }

        bool Index(std::uint16_t& value)
        {
            return reader_.U16(value) && value < strings_.size();
        }

        const std::string& StringAt(const std::uint16_t index) const
        {
            static const std::string empty;
            return index < strings_.size() ? strings_[index] : empty;
        }

        bool ReadStringTable()
        {
            std::uint16_t count{};
            if (!reader_.U16(count)) return false;
            strings_.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string value;
                if (!reader_.String(value)) return false;
                strings_.push_back(std::move(value));
            }
            return true;
        }

        bool SkipDebugInfo()
        {
            std::uint8_t present{};
            if (!reader_.U8(present)) return false;
            if (!present) return true;
            std::uint64_t modified{};
            std::uint16_t functionCount{};
            if (!reader_.U64(modified) || !reader_.U16(functionCount)) return false;
            for (std::uint32_t function = 0; function < functionCount; ++function) {
                std::uint16_t object{}, state{}, name{}, lineCount{};
                std::uint8_t type{};
                if (!Index(object) || !Index(state) || !Index(name) || !reader_.U8(type) ||
                    !reader_.U16(lineCount) || !reader_.Skip(static_cast<std::size_t>(lineCount) * 2U)) return false;
            }
            return true;
        }

        bool SkipUserFlags()
        {
            std::uint16_t count{};
            if (!reader_.U16(count)) return false;
            for (std::uint32_t index = 0; index < count; ++index) {
                std::uint16_t name{};
                std::uint8_t flag{};
                if (!Index(name) || !reader_.U8(flag)) return false;
            }
            return true;
        }

        bool ReadValue(Value& value)
        {
            std::uint8_t type{};
            if (!reader_.U8(type)) return false;
            switch (type) {
            case 0:
                value = {};
                return true;
            case 1:
            case 2:
                value.kind = type == 1 ? ValueKind::identifier : ValueKind::string;
                return Index(value.index);
            case 3: {
                std::uint32_t integer{};
                if (!reader_.U32(integer)) return false;
                value.kind = ValueKind::integer;
                value.integer = static_cast<std::int32_t>(integer);
                return true;
            }
            case 4: {
                std::uint32_t ignored{};
                value.kind = ValueKind::other;
                return reader_.U32(ignored);
            }
            case 5: {
                std::uint8_t ignored{};
                value.kind = ValueKind::other;
                return reader_.U8(ignored);
            }
            default:
                return false;
            }
        }

        bool ReadObjects()
        {
            std::uint16_t count{};
            if (!reader_.U16(count)) return false;
            for (std::uint32_t index = 0; index < count; ++index) {
                if (!ReadObject()) return false;
            }
            return true;
        }

        bool ReadObject()
        {
            const auto firstObjectRecord = records_.size();
            std::uint16_t objectName{}, parent{}, doc{}, autoState{};
            std::uint32_t objectSize{}, userFlags{};
            if (!Index(objectName) || !reader_.U32(objectSize) || !Index(parent) || !Index(doc) ||
                !reader_.U32(userFlags) || !Index(autoState)) return false;
            const auto owner = StringAt(objectName).empty() ? UHI::PathToUtf8(source_.stem()) : StringAt(objectName);
            std::unordered_map<std::uint16_t, std::int32_t> defaults;
            std::uint16_t variableCount{};
            if (!reader_.U16(variableCount)) return false;
            for (std::uint32_t index = 0; index < variableCount; ++index) {
                std::uint16_t name{}, type{};
                std::uint32_t flags{};
                Value value;
                if (!Index(name) || !Index(type) || !reader_.U32(flags) || !ReadValue(value)) return false;
                if (value.kind == ValueKind::integer) defaults[name] = value.integer;
            }

            std::unordered_map<std::uint16_t, std::int32_t> propertyDefaults;
            std::unordered_map<std::uint16_t, std::uint16_t> propertyVariables;
            std::unordered_map<std::uint16_t, std::uint16_t> variableProperties;
            std::unordered_map<std::uint16_t, std::int32_t> inferredDefaults;
            std::uint16_t propertyCount{};
            if (!reader_.U16(propertyCount)) return false;
            for (std::uint32_t index = 0; index < propertyCount; ++index) {
                std::uint16_t name{}, type{}, propertyDoc{};
                std::uint32_t flags{};
                std::uint8_t propertyFlags{};
                if (!Index(name) || !Index(type) || !Index(propertyDoc) || !reader_.U32(flags) ||
                    !reader_.U8(propertyFlags)) return false;
                if ((propertyFlags & 0x04U) != 0) {
                    std::uint16_t autoVariable{};
                    if (!Index(autoVariable)) return false;
                    propertyVariables[name] = autoVariable;
                    variableProperties[autoVariable] = name;
                    if (const auto found = defaults.find(autoVariable); found != defaults.end()) {
                        propertyDefaults[name] = found->second;
                    }
                } else {
                    if ((propertyFlags & 0x01U) != 0 &&
                        !ReadFunction("property getter", owner, defaults, propertyDefaults,
                            propertyVariables, variableProperties, inferredDefaults)) return false;
                    if ((propertyFlags & 0x02U) != 0 &&
                        !ReadFunction("property setter", owner, defaults, propertyDefaults,
                            propertyVariables, variableProperties, inferredDefaults)) return false;
                }
            }

            std::uint16_t stateCount{};
            if (!reader_.U16(stateCount)) return false;
            for (std::uint32_t stateIndex = 0; stateIndex < stateCount; ++stateIndex) {
                std::uint16_t stateName{}, functionCount{};
                if (!Index(stateName) || !reader_.U16(functionCount)) return false;
                for (std::uint32_t functionIndex = 0; functionIndex < functionCount; ++functionIndex) {
                    std::uint16_t functionName{};
                    if (!Index(functionName) ||
                        !ReadFunction(StringAt(functionName), owner, defaults, propertyDefaults,
                            propertyVariables, variableProperties, inferredDefaults)) return false;
                }
            }

            // MCM setup functions commonly add their key-map controls before a
            // later initialization function assigns the backing property.  A
            // strictly function-local pass therefore sees the option identity
            // but misses the literal default.  Resolve those already-emitted
            // runtime records after every state/function in the object has
            // contributed to inferredDefaults.
            const auto canonical = [](std::string_view text) {
                std::string result;
                result.reserve(text.size());
                for (const auto character : text) {
                    const auto byte = static_cast<unsigned char>(character);
                    if (std::isalnum(byte)) result.push_back(static_cast<char>(std::tolower(byte)));
                }
                constexpr std::string_view backingSuffix = "var";
                if (result.size() > backingSuffix.size() && result.ends_with(backingSuffix)) {
                    result.resize(result.size() - backingSuffix.size());
                }
                return result;
            };
            for (std::size_t recordIndex = firstObjectRecord; recordIndex < records_.size(); ++recordIndex) {
                auto& record = records_[recordIndex];
                if (record.conflictEligible || !record.codeSystem.contains("runtime value unresolved")) continue;
                const auto setting = canonical(record.settingName);
                const auto action = canonical(record.action);
                if (setting.empty() && action.empty()) continue;
                std::optional<std::int32_t> resolved;
                for (const auto& [name, value] : inferredDefaults) {
                    const auto candidate = canonical(StringAt(name));
                    if (candidate.empty()) continue;
                    if ((!setting.empty() && (candidate == setting || candidate.ends_with(setting) ||
                            setting.ends_with(candidate))) ||
                        (!action.empty() && candidate == action)) {
                        resolved = value;
                        break;
                    }
                }
                if (!resolved || *resolved <= 0) continue;
                const auto parsed = UHI::ParseSkseInputCode(std::to_string(*resolved));
                record.binding = parsed.binding;
                record.rawBinding = std::to_string(*resolved);
                record.codeSystem = parsed.codeSystem;
                record.device = parsed.device;
                record.confidence = UHI::Confidence::inferred;
                record.conflictEligible = parsed.conflictEligible;
            }
            return true;
        }

        bool ReadTypedNames()
        {
            std::uint16_t count{};
            if (!reader_.U16(count)) return false;
            for (std::uint32_t index = 0; index < count; ++index) {
                std::uint16_t name{}, type{};
                if (!Index(name) || !Index(type)) return false;
            }
            return true;
        }

        std::optional<std::int32_t> Resolve(const Value& value,
            const std::unordered_map<std::uint16_t, std::int32_t>& constants) const
        {
            if (value.kind == ValueKind::integer) return value.integer;
            if (value.kind == ValueKind::identifier) {
                if (const auto found = constants.find(value.index); found != constants.end()) return found->second;
            }
            if (value.kind == ValueKind::string) {
                const auto& text = StringAt(value.index);
                std::int32_t number{};
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
                if (error == std::errc{} && end == text.data() + text.size()) return number;
            }
            return std::nullopt;
        }

        std::optional<ValueOrigin> ResolveOrigin(const Value& value, const OriginMap& origins) const
        {
            if (value.kind != ValueKind::identifier) return std::nullopt;
            if (const auto found = origins.find(value.index); found != origins.end()) return found->second;
            const auto& identifier = StringAt(value.index);
            if (identifier.empty() || identifier.starts_with("::temp") || identifier.starts_with("::none")) {
                return std::nullopt;
            }
            return ValueOrigin{ .settingName = identifier };
        }

        std::optional<std::string> ResolveString(const Value& value,
            const std::unordered_map<std::uint16_t, std::string>& strings) const
        {
            if (value.kind == ValueKind::string) return StringAt(value.index);
            if (value.kind == ValueKind::identifier) {
                if (const auto found = strings.find(value.index); found != strings.end()) return found->second;
            }
            return std::nullopt;
        }

        void InspectCall(const std::uint8_t opcode, const std::vector<Value>& fixed,
            const std::vector<Value>& variable, const std::string_view owner, const std::string_view function,
            const std::unordered_map<std::uint16_t, std::int32_t>& constants,
            const std::unordered_map<std::uint16_t, std::string>& stringConstants,
            const OriginMap& origins)
        {
            const std::size_t methodPosition = opcode == 25 ? 1U : 0U;
            if (methodPosition >= fixed.size() || fixed[methodPosition].kind != ValueKind::identifier) return;
            const auto method = StringAt(fixed[methodPosition].index);
            const auto lowered = Lower(method);
            std::optional<std::size_t> keyPosition;
            std::optional<std::size_t> labelPosition;
            if (lowered == "registerforkey") keyPosition = 0;
            else if (lowered == "addkeymapoption") { keyPosition = 1; labelPosition = 0; }
            else if (lowered == "addkeymapoptionst") { keyPosition = 2; labelPosition = 1; }
            else if (lowered == "setkeymapoptionvalue") keyPosition = 1;
            else if (lowered == "setkeymapoptionvaluest") keyPosition = 0;
            else return;
            if (!keyPosition || *keyPosition >= variable.size() || records_.size() >= kMaximumRecords) return;

            std::string action;
            if (labelPosition && *labelPosition < variable.size()) {
                if (const auto label = ResolveString(variable[*labelPosition], stringConstants)) action = *label;
            }
            if (action.empty()) {
                action = method;
                if (!function.empty()) action += " in " + std::string(function);
            }
            auto origin = ResolveOrigin(variable[*keyPosition], origins);
            if (!origin && !action.empty() && !IsImplementationAction(action)) {
                // Even when a custom getter hides the backing property, retain
                // the MCM option identity instead of dropping the option. The
                // live resolver can use this semantic name as a conservative
                // fallback, while the raw record stays non-conflicting.
                origin = ValueOrigin{ .settingName = action, .settingSection = method };
            }
            const auto resolved = Resolve(variable[*keyPosition], constants);
            const auto identity = std::string(owner) + '\x1F' + action + '\x1F' +
                (resolved ? std::to_string(*resolved) : "?") + '\x1F' +
                (origin ? origin->settingName : std::string{});
            if (!seen_.insert(identity).second) return;
            if (resolved) {
                const auto parsed = UHI::ParseSkseInputCode(std::to_string(*resolved));
                records_.push_back({
                    .owner = std::string(owner), .action = std::move(action), .binding = parsed.binding,
                    .rawBinding = std::to_string(*resolved),
                    .settingName = origin ? origin->settingName : std::string{},
                    .settingSection = origin ? origin->settingSection : std::string{},
                    .codeSystem = parsed.codeSystem, .device = parsed.device,
                    .detector = "PexBytecodeScanner", .confidence = UHI::Confidence::inferred,
                    .evidencePath = source_, .evidenceLine = 0, .stage = UHI::ScanStage::scripts,
                    .conflictEligible = parsed.conflictEligible
                });
            } else {
                records_.push_back({
                    .owner = std::string(owner), .action = std::move(action),
                    .binding = method + " (runtime value)", .rawBinding = method,
                    .settingName = origin ? origin->settingName : std::string{},
                    .settingSection = origin ? origin->settingSection : std::string{},
                    .codeSystem = "SKSE unified input code (runtime value unresolved)",
                    .device = "unknown", .detector = "PexBytecodeScanner",
                    .confidence = UHI::Confidence::candidate, .evidencePath = source_, .evidenceLine = 0,
                    .stage = UHI::ScanStage::scripts, .conflictEligible = false
                });
            }
        }

        bool ReadFunction(const std::string_view functionName, const std::string_view owner,
            const std::unordered_map<std::uint16_t, std::int32_t>& defaults,
            const std::unordered_map<std::uint16_t, std::int32_t>& propertyDefaults,
            const std::unordered_map<std::uint16_t, std::uint16_t>& propertyVariables,
            const std::unordered_map<std::uint16_t, std::uint16_t>& variableProperties,
            std::unordered_map<std::uint16_t, std::int32_t>& inferredDefaults)
        {
            std::uint16_t returnType{}, doc{};
            std::uint32_t userFlags{};
            std::uint8_t flags{};
            if (!Index(returnType) || !Index(doc) || !reader_.U32(userFlags) || !reader_.U8(flags) ||
                !ReadTypedNames() || !ReadTypedNames()) return false;
            std::uint16_t instructionCount{};
            if (!reader_.U16(instructionCount) || totalInstructions_ + instructionCount > kMaximumInstructions) return false;
            totalInstructions_ += instructionCount;
            auto constants = defaults;
            for (const auto& [name, value] : inferredDefaults) constants[name] = value;
            std::unordered_map<std::uint16_t, std::string> stringConstants;
            OriginMap origins;
            // Auto-property backing variables use ::Name_var.  Associate both
            // names so direct backing-variable reads and normal PROPGET output
            // converge on the public property name.
            for (const auto& [property, backingVariable] : propertyVariables) {
                origins[property] = { .settingName = StringAt(property) };
                origins[backingVariable] = { .settingName = StringAt(property) };
            }
            std::unordered_map<std::uint64_t, std::int32_t> arrayConstants;
            std::unordered_map<std::uint64_t, ValueOrigin> arrayOrigins;
            const auto firstFunctionRecord = records_.size();
            std::string contextEvidence(functionName);
            const auto appendEvidence = [&](const Value& value) {
                if (value.kind != ValueKind::identifier && value.kind != ValueKind::string) return;
                const auto& text = StringAt(value.index);
                if (text.empty() || contextEvidence.size() >= 64U * 1024U) return;
                contextEvidence.push_back(' ');
                contextEvidence.append(text.substr(0, 512));
            };
            for (std::uint32_t instructionIndex = 0; instructionIndex < instructionCount; ++instructionIndex) {
                std::uint8_t opcode{};
                if (!reader_.U8(opcode) || opcode >= kArgumentCounts.size()) return false;
                std::vector<Value> fixed(kArgumentCounts[opcode]);
                for (auto& value : fixed) if (!ReadValue(value)) return false;
                std::vector<Value> variable;
                if (kHasVarArgs[opcode]) {
                    Value count;
                    if (!ReadValue(count) || count.kind != ValueKind::integer || count.integer < 0 ||
                        static_cast<std::size_t>(count.integer) > kMaximumVarArgs) return false;
                    variable.resize(static_cast<std::size_t>(count.integer));
                    for (auto& value : variable) if (!ReadValue(value)) return false;
                }
                for (const auto& value : fixed) appendEvidence(value);
                for (const auto& value : variable) appendEvidence(value);
                if ((opcode == 13 || opcode == 14) && fixed.size() == 2 &&
                    fixed[0].kind == ValueKind::identifier) {
                    if (const auto value = Resolve(fixed[1], constants)) {
                        constants[fixed[0].index] = *value;
                        auto loweredStorage = Lower(StringAt(fixed[0].index));
                        std::erase_if(loweredStorage, [](const unsigned char character) {
                            return !std::isalnum(character);
                        });
                        const bool keyStorage = loweredStorage.contains("hotkey") ||
                            loweredStorage.contains("keybind") || loweredStorage.contains("keycode") ||
                            loweredStorage.contains("keymap") || loweredStorage.starts_with("hk") ||
                            loweredStorage.contains("hk");
                        if (keyStorage || variableProperties.contains(fixed[0].index) ||
                            propertyVariables.contains(fixed[0].index)) {
                            inferredDefaults[fixed[0].index] = *value;
                            if (const auto property = variableProperties.find(fixed[0].index);
                                property != variableProperties.end()) {
                                inferredDefaults[property->second] = *value;
                            }
                            if (const auto variable = propertyVariables.find(fixed[0].index);
                                variable != propertyVariables.end()) {
                                inferredDefaults[variable->second] = *value;
                            }
                        }
                    } else constants.erase(fixed[0].index);
                    if (const auto value = ResolveString(fixed[1], stringConstants)) {
                        stringConstants[fixed[0].index] = *value;
                    } else {
                        stringConstants.erase(fixed[0].index);
                    }
                    if (const auto origin = ResolveOrigin(fixed[1], origins)) origins[fixed[0].index] = *origin;
                    else origins.erase(fixed[0].index);
                } else if (opcode == 28 && fixed.size() == 3 &&
                    fixed[0].kind == ValueKind::identifier && fixed[2].kind == ValueKind::identifier) {
                    origins[fixed[2].index] = {
                        .settingName = StringAt(fixed[0].index),
                        .settingSection = fixed[1].kind == ValueKind::identifier ? StringAt(fixed[1].index) : ""
                    };
                    if (const auto found = inferredDefaults.find(fixed[0].index); found != inferredDefaults.end()) {
                        constants[fixed[2].index] = found->second;
                    } else if (const auto found = propertyDefaults.find(fixed[0].index); found != propertyDefaults.end()) {
                        constants[fixed[2].index] = found->second;
                    } else {
                        constants.erase(fixed[2].index);
                    }
                } else if (opcode == 29 && fixed.size() == 3 &&
                    fixed[0].kind == ValueKind::identifier) {
                    if (const auto value = Resolve(fixed[2], constants)) {
                        inferredDefaults[fixed[0].index] = *value;
                        constants[fixed[0].index] = *value;
                        if (const auto variable = propertyVariables.find(fixed[0].index);
                            variable != propertyVariables.end()) {
                            inferredDefaults[variable->second] = *value;
                            constants[variable->second] = *value;
                        }
                    }
                } else if (opcode == 27 && fixed.size() == 3 && fixed[0].kind == ValueKind::identifier) {
                    const auto left = ResolveString(fixed[1], stringConstants);
                    const auto right = ResolveString(fixed[2], stringConstants);
                    if (left && right) stringConstants[fixed[0].index] = *left + *right;
                    else stringConstants.erase(fixed[0].index);
                } else if (opcode == 33 && fixed.size() == 3 && fixed[0].kind == ValueKind::identifier) {
                    if (const auto index = Resolve(fixed[1], constants)) {
                        const auto slot = (static_cast<std::uint64_t>(fixed[0].index) << 32U) |
                            static_cast<std::uint32_t>(*index);
                        if (const auto value = Resolve(fixed[2], constants)) arrayConstants[slot] = *value;
                        else arrayConstants.erase(slot);
                        if (const auto origin = ResolveOrigin(fixed[2], origins)) arrayOrigins[slot] = *origin;
                        else arrayOrigins.erase(slot);
                    }
                } else if (opcode == 32 && fixed.size() == 3 && fixed[0].kind == ValueKind::identifier &&
                    fixed[1].kind == ValueKind::identifier) {
                    if (const auto index = Resolve(fixed[2], constants)) {
                        const auto slot = (static_cast<std::uint64_t>(fixed[1].index) << 32U) |
                            static_cast<std::uint32_t>(*index);
                        if (const auto found = arrayConstants.find(slot); found != arrayConstants.end()) {
                            constants[fixed[0].index] = found->second;
                        } else constants.erase(fixed[0].index);
                        if (const auto found = arrayOrigins.find(slot); found != arrayOrigins.end()) {
                            origins[fixed[0].index] = found->second;
                        } else if (const auto arrayOrigin = ResolveOrigin(fixed[1], origins)) {
                            auto elementOrigin = *arrayOrigin;
                            elementOrigin.settingName += '[' + std::to_string(*index) + ']';
                            origins[fixed[0].index] = std::move(elementOrigin);
                        } else origins.erase(fixed[0].index);
                    } else if (const auto arrayOrigin = ResolveOrigin(fixed[1], origins)) {
                        auto elementOrigin = *arrayOrigin;
                        elementOrigin.settingName += "[]";
                        origins[fixed[0].index] = std::move(elementOrigin);
                    }
                }
                if (opcode == 23 || opcode == 24 || opcode == 25) {
                    InspectCall(opcode, fixed, variable, owner, functionName, constants, stringConstants, origins);
                    // MCM Helper and similar libraries return a save-backed
                    // setting into the call destination. Preserve the literal
                    // setting path even though its current integer is not in
                    // the PEX file.
                    const std::size_t methodPosition = opcode == 25 ? 1U : 0U;
                    const std::size_t destinationPosition = opcode == 24 ? 1U : 2U;
                    if (methodPosition < fixed.size() && destinationPosition < fixed.size() &&
                        fixed[methodPosition].kind == ValueKind::identifier &&
                        fixed[destinationPosition].kind == ValueKind::identifier) {
                        // PEX temporaries are aggressively reused. A call
                        // always overwrites its destination; retaining an old
                        // constant here turns unrelated values into false keys.
                        constants.erase(fixed[destinationPosition].index);
                        stringConstants.erase(fixed[destinationPosition].index);
                        origins.erase(fixed[destinationPosition].index);
                        const auto method = Lower(StringAt(fixed[methodPosition].index));
                        auto semanticMethod = std::string_view(method);
                        while (semanticMethod.starts_with('_')) semanticMethod.remove_prefix(1);
                        const bool keyGetter = semanticMethod.starts_with("get") &&
                            (semanticMethod.contains("key") || semanticMethod.contains("hotkey"));
                        const bool settingGetter = method.contains("getmodsetting") ||
                            method.contains("getsettingint") || method.contains("getsettingbool") ||
                            method == "getintvalue" || method.contains("getiniint") ||
                            method.contains("getmappedkey") || keyGetter;
                        if (settingGetter) {
                            std::optional<std::string> setting;
                            for (const auto& argument : variable) {
                                setting = ResolveString(argument, stringConstants);
                                if (setting && !setting->empty()) break;
                            }
                            origins[fixed[destinationPosition].index] = {
                                .settingName = setting.value_or(StringAt(fixed[methodPosition].index)),
                                .settingSection = StringAt(fixed[methodPosition].index)
                            };
                        } else if (opcode == 23 && semanticMethod.starts_with("get") && fixed.size() > 1) {
                            // GlobalVariable.GetValue[Int] and similar object
                            // getters retain the identity of their receiver.
                            if (const auto receiver = ResolveOrigin(fixed[1], origins)) {
                                origins[fixed[destinationPosition].index] = *receiver;
                            }
                        }
                    }
                }
            }
            const auto activation = UHI::InferActivationContext(
                contextEvidence, UHI::ContextEvidenceSource::papyrusFunction);
            if (activation.mask != 0) {
                for (std::size_t index = firstFunctionRecord; index < records_.size(); ++index) {
                    if (records_[index].contextMask != 0) continue;
                    records_[index].contextMask = activation.mask;
                    records_[index].contextConfidence = activation.confidence;
                }
            }
            return true;
        }

        static constexpr std::array<std::uint8_t, 51> kArgumentCounts{
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
            1, 2, 2, 3, 2, 3, 1, 3, 3, 3, 2, 2, 3, 3, 4, 4, 3, 1, 3, 3,
            5, 5, 3, 3, 1, 3, 1, 6, 0, 0, 1
        };
        static constexpr std::array<bool, 51> kHasVarArgs{
            false, false, false, false, false, false, false, false, false, false,
            false, false, false, false, false, false, false, false, false, false,
            false, false, false, true, true, true, false, false, false, false,
            false, false, false, false, false, false, false, false, false, false,
            false, false, false, false, false, false, false, false, true, true, true
        };

        std::filesystem::path source_;
        Reader reader_;
        std::vector<std::string> strings_;
        std::vector<UHI::HotkeyRecord> records_;
        std::unordered_set<std::string> seen_;
        std::size_t totalInstructions_{};
    };
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> PexBytecodeScanner::ScanContent(const std::filesystem::path& source,
        const std::string_view bytes) const noexcept
    {
        if (bytes.size() < kSkyrimMagic.size() || bytes.size() > kMaximumPexBytes) return {};
        try {
            return Parser(source, bytes).Parse();
        } catch (...) {
            // Malformed or allocation-hostile PEX files are isolated. No
            // exception can leave the scan worker and reach the game thread.
            return {};
        }
    }
}
