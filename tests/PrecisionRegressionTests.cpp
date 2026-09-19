#include "UHI/BindingIdentity.h"
#include "UHI/BindingHistory.h"
#include "UHI/BindingSerializer.h"
#include "UHI/OpeningHotkey.h"
#include "UHI/HotkeyViewModel.h"
#include "UHI/scanners/GenericConfigScanner.h"
#include "UHI/writers/ConfigFileWriter.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <random>
#include <limits>

namespace fs = std::filesystem;
namespace {
int failures{};
void Check(bool pass, const char* message) { if (!pass) { std::cerr << message << '\n'; ++failures; } }
void Put(const fs::path& p, std::string_view s) { fs::create_directories(p.parent_path()); std::ofstream f(p,std::ios::binary); f << s; }
std::string Get(const fs::path& p) { std::ifstream f(p,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
}
int main() {
 const auto root = fs::temp_directory_path() / "uhi_precision_regressions";
 fs::create_directories(root);
 UHI::Scanners::GenericConfigScanner scanner;
 UHI::Writers::ConfigFileWriter writer;
 const auto json=root/"Data/SKSE/Plugins/Scoped.json";
 auto rows=scanner.ScanContent(json,R"({"Alpha":{"ToggleKey":"F7","ctrl":true},"Beta":{"ToggleKey":"F8","ctrl":false}})");
 Check(rows.size()==2 && rows[0].binding=="Ctrl+F7" && rows[1].binding=="F8", "Boolean modifiers stay in their JSON object");
 rows=scanner.ScanContent(json,R"({"Alpha":{"Toggle_Key":63,"Toggle_Mod1":29},"Beta":{"Toggle_Key":64,"Toggle_Mod1":42}})");
 Check(rows.size()==2 && rows[0].binding=="LCtrl+F5" && rows[1].binding=="LShift+F6", "Compound fields stay in their JSON object");
 if(rows.size()==2) {
  const auto replacement=UHI::SerializeCapturedBinding(rows[0],"keyboard",66);
  Check(replacement && replacement.raw=="66" && replacement.display=="LCtrl+F8", "Scalar rebind preserves separately stored modifier in preview");
  Check(!UHI::SerializeCapturedBinding(rows[0],"keyboard",66,"keyboard",42), "Changing a separate modifier is rejected before write");
  Check(bool(UHI::SerializeCapturedBinding(rows[0],"keyboard",66,"keyboard",29)), "Recapturing the same modifier remains supported");
  Check(bool(UHI::SerializeUnboundBinding(rows[0])), "Separate modifiers do not prevent unbinding the main key");
 }
 for (const auto& extension : {".ini", ".toml", ".yaml"}) {
  const auto path=root/"Data/SKSE/Plugins"/(std::string("Scope")+extension);
  const bool yaml=std::string_view(extension)==".yaml";
  const auto text=yaml ? "Alpha:\n  Toggle_Key: 63\n  Toggle_Mod1: 29\nBeta:\n  Toggle_Key: 64\n  Toggle_Mod1: 42\n" : "[Alpha]\nToggle_Key=63\nToggle_Mod1=29\n[Beta]\nToggle_Key=64\nToggle_Mod1=42\n";
  rows=scanner.ScanContent(path,text);
  Check(rows.size()==2 && rows[0].binding=="LCtrl+F5" && rows[1].binding=="LShift+F6", "Text section/parent scopes preserve each compound chord");
 }
 rows=scanner.ScanContent(root/"Data/SKSE/Plugins/Scopes.toml", "[[Actions]]\nToggle_Key=63\nToggle_Mod1=29\n[[Actions]]\nToggle_Key=64\nToggle_Mod1=42\n");
 Check(rows.size()==2 && rows[0].binding=="LCtrl+F5" && rows[1].binding=="LShift+F6", "TOML array tables keep distinct modifier families");
 rows=scanner.ScanContent(json,R"({"ToggleKey":"F8","ctrlEnabled":true})");
 Check(rows.size()==1 && rows[0].binding=="Ctrl+F8", "Existing modifier flag aliases keep working within scope");
 rows=scanner.ScanContent(json,R"({"ToggleKey":"F8","ctrl":165,"shift":false,"note":"alt=true"})");
 Check(rows.size()==1 && rows[0].binding=="F8", "Numeric prefixes and text contents do not enable modifiers");
 const auto ini=root/"Scoped.ini";
 Put(ini,"[Beta]\nToggleKey=63\n[Alpha]\nToggleKey=63\n");
 Check(writer.SetBinding(ini,2,"ToggleKey","63","64","Alpha"), "Moved INI section is still editable");
 Check(Get(ini)=="[Beta]\nToggleKey=63\n[Alpha]\nToggleKey=64\n", "Only the selected INI section changes");
 Put(ini,"[Alpha]\nToggleKey=63\nToggleKey=63\n");
 Check(!writer.SetBinding(ini,2,"ToggleKey","63","64","Alpha"), "Ambiguous same-section keys refuse writes");
 Put(ini,"[Alpha]\nToggleKey=65\n");
 Check(!writer.SetBinding(ini,2,"ToggleKey","63","64","Alpha"), "External INI value changes remain protected");
 const auto scanDir=root/"Data/SKSE/Plugins";
 fs::create_directories(scanDir);
 for(const auto* name : {"New.ini", "Wide.ini", "Large.ini"}) fs::remove(scanDir/name);
 const auto large=scanDir/"Large.ini";
 std::string padding;
 for(int i=0;i<20000;++i) padding+="; Ordinary descriptive padding\n";
 padding+="ToggleKey=63\n"; Put(large,padding);
 UHI::ScanCache cache(root/"cache.bin"); cache.Load();
 rows=scanner.Scan(scanDir,true,{}, {},UHI::NumericCodeSpace::unknown,&cache);
 Check(rows.size()==1 && rows[0].rawBinding=="63", "Bindings beyond 256 KiB are discovered");
 Put(scanDir/"New.ini","ToggleKey=64\n");
 rows=scanner.Scan(scanDir,true,{}, {},UHI::NumericCodeSpace::unknown,&cache);
 Check(rows.size()==2 && cache.HitCount()>0, "Incremental scan discovers a new file and reuses unchanged cache entries");
 const std::u16string wide=u"[Keys]\nToggleKey=63\n";
 std::string bytes="\xff\xfe"; bytes.append(reinterpret_cast<const char*>(wide.data()),wide.size()*2);
 Put(scanDir/"Wide.ini",bytes);
 rows=scanner.Scan(scanDir);
 bool wideFound{};
 for(const auto& row:rows) if(row.evidencePath.filename()=="Wide.ini") { wideFound=true; Check(!row.editable,"UTF-16 detection does not advertise an unsupported writer"); }
 Check(wideFound,"UTF-16 bindings remain visible");
 const auto prefsPath=root/"Preferences.ini";
 Put(prefsPath,"\xEF\xBB\xBF[General]\nToggleKey=66\nEnabled=false\nUiScale=nan\nWindowOpacity=nan\n");
 auto prefs=UHI::LoadOpeningHotkey(prefsPath);
 Check(prefs.scanCode==66 && !prefs.enabled,"UTF-8 BOM preserves opening-hotkey settings");
 Check(std::isfinite(prefs.uiScale)&&std::isfinite(prefs.windowOpacity),"Non-finite preferences fall back to defaults");
 prefs.uiScale=std::numeric_limits<float>::infinity();
 Check(!UHI::SaveOpeningHotkey(prefsPath,prefs),"Non-finite preferences cannot be saved");
 auto a=scanner.ScanContent(json,R"({"Alpha":{"ToggleKey":63}})").at(0);
 auto b=scanner.ScanContent(json,"{\n  \"Alpha\": {\n    \"ToggleKey\":64\n  }\n}\n").at(0);
 Check(UHI::SameBindingSource(a,b),"JSON history survives reflow and rebind");
 Check(UHI::BindingSourceIdentity(a)==UHI::BindingSourceIdentity(b),"Action aliases survive reflow and rebind");
 const auto legacy=UHI::PathToUtf8(a.evidencePath.lexically_normal())+'\x1F'+std::to_string(a.evidenceLine)+'\x1F'+a.detector+'\x1F'+a.settingSection+'\x1F'+a.settingName+'\x1F'+a.rawBinding;
 Check(UHI::MigrateActionIdentity(legacy)==UHI::BindingSourceIdentity(b),"1.1.0 action aliases migrate to stable identity");
 const auto history=root/"history.bin"; fs::remove(history);
 Check(UHI::SaveBindingChange(history,a,"64") && UHI::SaveBindingChange(history,b,"65"),"Reflowed history entries save");
 const auto changes=UHI::LoadBindingHistory(history);
 Check(changes.size()==2 && UHI::SameBindingSource(changes[0].before,changes[1].before),"Old backup and latest value resolve the same JSON target");
 // Profile optimization must agree with an exhaustive pairwise oracle.
 std::mt19937 random(1170);
 UHI::Registry registry;
 for(int i=0;i<300;++i) {
  UHI::HotkeyRecord r;
  r.owner="Owner"+std::to_string(random()%12); r.action="Action"+std::to_string(random()%12);
  r.binding="F8"; r.device="keyboard"; r.evidenceLine=i+1;
  r.detector=random()%3==0?"ControlMapScanner":"Test";
  r.contextMask=random()%4==0?0:1U<<(random()%4);
  r.contextConfidence=random()%2?UHI::ContextConfidence::confirmed:UHI::ContextConfidence::inferred;
  registry.Add(r);
 }
 const auto analysis=registry.AnalyzeConflicts();
 for(std::size_t i=0;i<registry.Records().size();++i) {
  auto expected=UHI::ConflictStatus::none;
  for(std::size_t j=0;j<registry.Records().size();++j) expected=(std::max)(expected,UHI::PairConflictStatus(registry.Records()[i],registry.Records()[j]));
  Check(analysis.recordStatus[i]==expected,"Optimized conflict status equals exhaustive pairwise result");
 }
 UHI::Registry largeRegistry;
 for(int i=0;i<4096;++i) {
  UHI::HotkeyRecord r; r.owner="Owner"+std::to_string(i);r.action="Toggle";r.binding="F8";r.device="keyboard";
  r.contextMask=static_cast<unsigned>(UHI::ActivationContext::gameplay);r.contextConfidence=UHI::ContextConfidence::confirmed;
  largeRegistry.Add(r);
 }
 const auto groups=UHI::BuildHotkeyView(largeRegistry);
 Check(groups.size()==1 && groups[0].entries.size()==4096,"All large conflict group entries remain visible");
 if(!groups.empty()&&!groups[0].entries.empty()) {
  Check(groups[0].entries[0].Peers().size()==4095,"Every conflict peer remains available without truncation");
  Check(groups[0].entries[0].peerGroup==groups[0].entries.back().peerGroup,"Conflict membership is shared once instead of copied quadratically");
 }
 // Only this test's dedicated, fully specified temporary directory is removed.
 fs::remove_all(root);
 return failures?1:0;
}