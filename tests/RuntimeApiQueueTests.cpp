#include "UHI/RuntimeAPI.h"
#include <SKSE/SKSE.h>
#include <iostream>
extern "C" bool UniversalHotkeyManager_Open();
extern "C" void UniversalHotkeyManager_Close();
namespace UHI {
bool visible{};
bool IsMenuFrameworkWindowOpen() { return visible; }
bool OpenMenuFrameworkWindow() { visible=true;return true; }
bool CloseMenuFrameworkWindow() { visible=false;return true; }
}
int failures{};
void Check(bool ok,const char* text) {if(!ok){std::cerr<<text<<'\n';++failures;}}
void Drain(){while(!SKSE::service.tasks.empty()){auto tasks=std::move(SKSE::service.tasks);SKSE::service.tasks.clear();for(auto& f:tasks)f();}}
int main(){
 Check(!UniversalHotkeyManager_Open(),"Unready API refuses open");
 UHI::RuntimeAPI::SetReady(true);
 for(int n=0;n<1000;++n){
  Check(UniversalHotkeyManager_Open(),"Ready open accepted"); Drain(); Check(UHI::visible,"Normal opening works");
  UniversalHotkeyManager_Close(); Check(UniversalHotkeyManager_Open(),"Last open accepted");
  Check(SKSE::service.tasks.size()==1,"Rapid requests coalesce into one task");
  Drain(); Check(UHI::visible,"Close then open retains the last request");
  UniversalHotkeyManager_Open(); UniversalHotkeyManager_Close(); Drain(); Check(!UHI::visible,"Open then close retains the last request");
 }
 UniversalHotkeyManager_Open(); UHI::RuntimeAPI::SetReady(false); Drain(); Check(!UHI::visible,"Disabling readiness cancels pending open");
 Check(SKSE::service.tasks.empty(),"Completed requests retain no queued tasks");
 return failures?1:0;
}