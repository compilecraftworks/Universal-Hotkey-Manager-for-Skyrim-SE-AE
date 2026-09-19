#include "support/McmMock.h"
#include "McmImplementation.inc"
int failures{};
void Check(bool ok,const char* text){if(!ok){std::cerr<<text<<'\n';++failures;}}
int main(){
 using namespace RE::BSScript;
 auto root=std::make_shared<Object>();root->type.name="OtherModMCM";
 Variable name;name.type=Variable::string;name.text="Other Mod";root->Add("ModName",name);
 Variable key;key.number=63;root->Add("ToggleKey",key);
 auto manager=std::make_shared<Object>();
 Variable config;config.type=Variable::object;config.obj=root;
 Variable configs;configs.type=Variable::array;configs.arr=std::make_shared<std::vector<Variable>>(1,config);
 manager->Add("_modConfigs",configs);Internal::VirtualMachine::GetSingleton()->manager=manager;
 UHI::HotkeyRecord record;record.owner="Unrelated Native Plugin";record.settingName="ToggleKey";
 record.detector="StructuredConfigScanner";record.rawBinding="63";record.evidencePath="Data/SKSE/Plugins/UnrelatedNativePlugin.ini";
 std::string detail;
 Check(!ChangeRegisteredMcmHotkey(record,64,detail)&&root->vars["ToggleKey"].number==63,"Matching field name and old value cannot authorize another mod's write");
 record.detector="PapyrusRuntimeProperty";
 Check(!ChangeRegisteredMcmHotkey(record,65,detail)&&root->vars["ToggleKey"].number==63,"Absent live owner cannot select an unrelated MCM");
 record.owner="Other Mod";record.evidencePath="Data/Scripts/OtherModMCM.pex";
 Check(ChangeRegisteredMcmHotkey(record,64,detail)&&root->vars["ToggleKey"].number==64,"Exact owner/script still writes the intended property");
 Check(!ChangeRegisteredMcmHotkey(record,65,detail)&&root->vars["ToggleKey"].number==64,"Stale expected value still protects the property");
 Variable keys;keys.type=Variable::array;keys.arr=std::make_shared<std::vector<Variable>>(2);(*keys.arr)[1].number=63;root->Add("Keys",keys);
 record.settingName="Keys[1]";record.owner="Unrelated Native Plugin";record.evidencePath="Data/Scripts/MissingMCM.pex";
 Check(!ChangeRegisteredMcmHotkey(record,65,detail)&&(*keys.arr)[1].number==63,"Array indices cannot authorize another mod's write");
 record.owner="Other Mod";record.evidencePath="Data/Scripts/OtherModMCM.pex";
 Check(ChangeRegisteredMcmHotkey(record,65,detail)&&(*keys.arr)[1].number==65,"Owned array bindings still write the selected element");
 record.settingName="ToggleKey";
 root->Add("ToggleOID",Variable{});
 Check(!FindMcmOptionId(root,record),"Uninitialized zero OID is not treated as a keymap");
 root->Add("_currentPageNum",Variable{});
 Variable flags;flags.type=Variable::array;flags.arr=std::make_shared<std::vector<Variable>>(1);
 (*flags.arr)[0].number=7;root->Add("_optionFlagsBuf",flags);
 Check(FindMcmOptionId(root,record)==0,"Verified first keymap option on page zero is accepted");
 (*flags.arr)[0].number=3;Check(!FindMcmOptionId(root,record),"A toggle option at index zero is not a keymap");
 (*flags.arr)[0].number=7;root->vars["_currentPageNum"].number=1;
 Check(!FindMcmOptionId(root,record),"Page-zero option is not inferred from another page's buffer");
 root->vars["ToggleOID"].number=1;
 Check(FindMcmOptionId(root,record)==1,"Existing positive option IDs keep their behavior");
 return failures?1:0;
}