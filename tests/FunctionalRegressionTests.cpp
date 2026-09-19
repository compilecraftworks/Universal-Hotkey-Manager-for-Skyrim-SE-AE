#include "UHI/BindingHistory.h"
#include "UHI/GameTransition.h"
#include "UHI/JsonConfigDocument.h"
#include "UHI/LastScanStore.h"
#include "UHI/McmWriteReceipt.h"
#include "UHI/Registry.h"
#include "UHI/scanners/GenericConfigScanner.h"
#include "UHI/writers/ConfigFileWriter.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>

namespace
{
    int failures{};
    void Check(bool passed, const char* label) { if (!passed) { std::cerr << label << '\n'; ++failures; } }
    void Write(const std::filesystem::path& path, std::string_view text) { std::ofstream file(path, std::ios::binary); file << text; }
    std::string Read(const std::filesystem::path& path) { std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}}; }
    enum class Message { kPreLoadGame, kDeleteGame, kSaveGame, kPostLoadGame, kNewGame };
}

int main()
{
    Check(UHI::BeginsGameTransition<Message>(Message::kPreLoadGame), "Loading still revokes runtime access");
    for (auto message : { Message::kDeleteGame, Message::kSaveGame, Message::kPostLoadGame, Message::kNewGame })
        Check(!UHI::BeginsGameTransition<Message>(message), "Save deletion must not start a loading transition");

    const auto root = std::filesystem::temp_directory_path() / "uhi_functional_regressions";
    std::filesystem::create_directories(root);
    UHI::Writers::ConfigFileWriter writer;
    const auto json = root / "settings.json";
    Write(json, R"({"Alpha":{"ToggleKey":"F5"},"Beta":{"ToggleKey":"F6"}})");
    auto records = UHI::Scanners::GenericConfigScanner{}.ScanContent(json, Read(json));
    Check(records.size() == 2, "Both distinct JSON actions are scanned");
    if (records.size() == 2) {
        auto beta = records[1];
        Check(beta.settingSection == "json:/Beta/ToggleKey", "JSON setting preserves its complete identity");
        Write(json, R"({"Alpha":{"ToggleKey":"F6"},"Beta":{"ToggleKey":"F6"}})");
        Check(writer.SetBinding(json, beta.evidenceLine, beta.settingName, "F6", "F7", beta.settingSection),
            "A duplicate name in another scope does not prevent a valid edit");
        Check(Read(json) == R"({"Alpha":{"ToggleKey":"F6"},"Beta":{"ToggleKey":"F7"}})", "Only selected Beta is changed");
        const auto original = UHI::OriginalBackupValue(beta);
        Check(original && *original == "F6", "Original backup resolves the same JSON scope");
        Check(writer.SetBinding(json, 1, "ToggleKey", "F7", "F6", beta.settingSection), "Restore selected property only");
        records = UHI::Scanners::GenericConfigScanner{}.ScanContent(json, Read(json));
        Check(records.size() == 2, "Identical bindings in different objects remain two records");
        UHI::Registry registry;
        for (auto record : records) { record.action = "Same label"; registry.Add(record); }
        Check(registry.Records().size() == 2, "Registry must retain distinct JSON identities even with the same label");
        Check(!writer.SetBinding(json, 1, "ToggleKey", "F6", "F8"), "Old records without scope reject genuinely ambiguous targets");
        Write(json, "{\n  \"Alpha\": {\"ToggleKey\":\"F6\"},\n  \"Beta\": {\"ToggleKey\":\"F6\"}\n}\n");
        Check(writer.SetBinding(json, beta.evidenceLine, beta.settingName, "F6", "F8", beta.settingSection),
            "Harmless JSON formatting changes do not disable editing");
        Check(!writer.SetBinding(json, 1, "ToggleKey", "F6", "F9", beta.settingSection), "Changed target value is still protected");
        Write(json, R"({"Beta":{"ToggleKey":"F8","ToggleKey":"F8"}})");
        Check(!writer.SetBinding(json, 1, "ToggleKey", "F8", "F9", beta.settingSection), "Duplicate properties within the same object are ambiguous");
    }
    const auto jsonc = root / "settings.jsonc";
    Write(jsonc, "{ /* \"ToggleKey\":\"F6\" */ \"A/B~\": [{\"ToggleKey\":\"F6\",}], // comment\n}\n");
    Check(writer.SetBinding(jsonc, 1, "ToggleKey", "F6", "F7", "json:/A~1B~0/0/ToggleKey"),
        "JSONC comments, trailing commas, arrays and escaped pointer components remain editable");
    Check(Read(jsonc).find("/* \"ToggleKey\":\"F6\" */") != std::string::npos, "Comments are preserved");
    Check(!writer.SetBinding(jsonc, 1, "ToggleKey", "F7", "F\"8", "json:/A~1B~0/0/ToggleKey"),
        "Invalid replacement cannot corrupt JSON syntax");
    Check(UHI::JsonConfigDocument(R"({"\u0041":{"ToggleKey":63}})").Find("ToggleKey", 1, "/A/ToggleKey"),
        "Escaped JSON property names use the same structural identity");

    const auto ini = root / "settings.ini";
    for (const auto* comment : { "; ToggleKey=F6\n", "# ToggleKey=F6\n", "// ToggleKey=F6\n",
             "Description=\"ToggleKey=F6\"\n", "ToggleKeyExtra=F4 ; ToggleKey=F6\n" }) {
        Write(ini, comment);
        Check(!writer.SetBinding(ini, 1, "ToggleKey", "F6", "F7"), "Comments/string contents cannot become writable assignments");
        Check(Read(ini) == comment, "Rejected write leaves original bytes intact");
    }
    Write(ini, "\xEF\xBB\xBFToggleKey=F6 ; note\r\n");
    Check(writer.SetBinding(ini, 1, "ToggleKey", "F6", "F7"), "UTF-8 BOM and CRLF remain supported");
    Check(Read(ini) == "\xEF\xBB\xBFToggleKey=F7 ; note\r\n", "BOM, comment and CRLF preserved");

    UHI::HotkeyRecord oldRecord{ .owner="Old", .action="Toggle", .rawBinding="F6", .settingName="ToggleKey",
        .detector="StructuredConfigScanner", .evidencePath=ini, .evidenceLine=1, .editable=true };
    const auto history = root / "history.bin";
    Check(UHI::SaveBindingChange(history, oldRecord, "F7"), "New history can be saved");
    {
        std::fstream file(history, std::ios::in | std::ios::out | std::ios::binary);
        const std::uint32_t previousSchema = 11;
        file.seekp(8); file.write(reinterpret_cast<const char*>(&previousSchema), sizeof(previousSchema));
    }
    Check(UHI::LoadBindingHistory(history).size() == 1, "1.0.9 history survives 1.1.0 upgrade");
    Check(!UHI::LastScanStore{}.Load(history), "Old scan semantics are invalidated independently of history");

    using Receipt = UHI::McmWriteReceipt;
    using Outcome = Receipt::Outcome;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        auto receipt = std::make_shared<Receipt>();
        receipt->deadline = 30'000;
        std::weak_ptr<Receipt> weak = receipt;
        const std::function<void()> callback = [weak] { if (const auto value = weak.lock()) value->handlerCompleted = true; };
        Check(receipt->Inspect(1, 64, 64) == Outcome::waiting, "Dispatch/value alone does not claim handler completion");
        Check(receipt->Inspect(30'000, 64, 64) == Outcome::timedOut, "A missing callback has a bounded lifetime");
        callback();
        Check(receipt->Inspect(2, 64, 64) == Outcome::applied, "Completed and verified remaps succeed");
        Check(receipt->Inspect(2, 63, 64) == Outcome::rejected, "No-op handlers cannot report success");
        Check(receipt->Inspect(2, std::nullopt, 64) == Outcome::rejected, "Disappearing target cannot report success");
        receipt.reset();
        callback(); // late VM callback after completion/cancellation/timeout
        Check(weak.expired(), "Late callbacks do not retain completed operation state");
    }
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
