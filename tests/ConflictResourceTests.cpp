#include "UHI/Registry.h"
#include "UHI/HotkeyViewModel.h"
#include "UHI/JsonConfigDocument.h"
#include <atomic>
#include <cstdlib>
#include <new>
#include <iostream>
#include <chrono>
#include <cstddef>
struct alignas(std::max_align_t) Header {std::size_t size;};
std::atomic_size_t live{},peak{};
void* operator new(std::size_t size){auto* h=static_cast<Header*>(std::malloc(size+sizeof(Header)));if(!h)throw std::bad_alloc();h->size=size;auto n=live.fetch_add(size)+size;auto p=peak.load();while(p<n&&!peak.compare_exchange_weak(p,n)){}return h+1;}
void operator delete(void* p)noexcept{if(!p)return;auto* h=static_cast<Header*>(p)-1;live-=h->size;std::free(h);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
int main(){
 int failures{};
 std::cout<<"Resource accounting: requested C++ allocation bytes (not process RSS)\n";
 UHI::NormalizeBinding("F8");
 // Warm process-lifetime normalizer/category tables before measuring releases.
 { UHI::Registry warm; UHI::HotkeyRecord r;r.owner="Warm";r.action="Toggle";r.binding="F8";warm.Add(r);auto view=UHI::BuildHotkeyView(warm); }
 for(int n:{256,1024,2048,4096}){
  UHI::Registry registry;
  for(int i=0;i<n;++i){UHI::HotkeyRecord r;r.owner="Mod"+std::to_string(i);r.action="Toggle";r.binding="F8";registry.Add(std::move(r));}
  auto before=live.load();peak=before;
  {
   auto analysis=registry.AnalyzeConflicts();std::size_t members=0;for(auto& [key,group]:analysis.groups)members+=group.indices.size();
   const auto extra=peak.load()-before;
   std::cout<<"CONFLICT n="<<n<<" stored_members="<<members<<" peak_extra_bytes="<<extra<<'\n';
   if(members!=n||extra>std::size_t(n)*1024)++failures;
  }
  if(live!=before)++failures;
  peak=before;
  {
   auto view=UHI::BuildHotkeyView(registry);
   if(view.size()!=1||view[0].entries.size()!=n||view[0].entries[0].Peers().size()!=n-1)++failures;
   std::cout<<"VIEW n="<<n<<" peak_extra_bytes="<<(peak-before)<<'\n';
   if(peak-before>std::size_t(n)*4096)++failures;
  }
  std::cout<<"AFTER release retained_extra_bytes="<<(live-before)<<'\n';
  if(live!=before)++failures;
 }
 const auto baseline=live.load();
 for(int cycle=0;cycle<1000;++cycle){
  UHI::JsonConfigDocument doc(R"({"Alpha":{"ToggleKey":63},"Beta":{"ToggleKey":64}})");
  UHI::Registry registry;
  for(int i=0;i<32;++i){UHI::HotkeyRecord r;r.owner="Mod"+std::to_string(i);r.action="Toggle";r.binding="F8";registry.Add(std::move(r));}
  auto view=UHI::BuildHotkeyView(registry);
 }
 std::cout<<"1000 JSON/registry/view cycles retained_bytes="<<(live-baseline)<<'\n';
 if(live!=baseline)++failures;
 return failures?1:0;
}