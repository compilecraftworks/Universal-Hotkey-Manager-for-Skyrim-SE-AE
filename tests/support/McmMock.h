#pragma once
#include "UHI/HotkeyRecord.h"
#include "UHI/PathEncoding.h"
#include "UHI/McmWriteReceipt.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>
// Engine-only stand-ins. The target selection, stale-value guard, dispatch
// selection and write implementation are copied verbatim from plugin.cpp.
namespace RE {
using BSFixedString=std::string;
template<class T> using BSTSmartPointer=std::shared_ptr<T>;
struct TESGlobal { float value{}; };
struct TESQuest {int GetFormType(){return 0;}};
struct TESForm {template<class T>static T* LookupByEditorID(const char*){static T t;return &t;}};
struct TESDataHandler {static TESDataHandler* GetSingleton(){static TESDataHandler d;return &d;} template<class T>T* LookupForm(int,const char*){return TESForm::LookupByEditorID<T>("");}};
namespace BSScript {
struct Object;
struct Variable {
 enum Type {integer,string,object,array} type=integer;
 int number{}; std::string text;
 std::shared_ptr<Object> obj;
 std::shared_ptr<std::vector<Variable>> arr;
 bool IsInt()const{return type==integer;} bool IsString()const{return type==string;}
 bool IsObject()const{return type==object;} bool IsArray()const{return type==array;}
 int GetSInt()const{return number;} void SetSInt(int n){number=n;}
 const char* GetString()const{return text.c_str();}
 auto GetObject()const{return obj;} auto GetArray()const{return arr;}
 template<class T>T Unpack(){return nullptr;}
};
struct TypeInfo {
 struct Field {std::string name;}; std::vector<Field> fields; std::string name;
 TypeInfo* GetParent(){return nullptr;} const char* GetName(){return name.c_str();}
 const Field* GetPropertyIter(){return nullptr;} unsigned GetNumProperties(){return 0;}
 const Field* GetVariableIter(){return fields.data();} unsigned GetNumVariables(){return unsigned(fields.size());}
};
struct Object {
 TypeInfo type;std::map<std::string,Variable> vars;
 bool IsValid(){return true;} TypeInfo* GetTypeInfo(){return &type;}
 Variable* GetVariable(const std::string& n){auto p=vars.find(n);return p==vars.end()?nullptr:&p->second;}
 Variable* GetProperty(const std::string& n){return GetVariable(n);}
 void Add(std::string name,Variable v){type.fields.push_back({name});vars.emplace(name,v);}
};
struct IStackCallbackFunctor {virtual ~IStackCallbackFunctor()=default;virtual void operator()(Variable)=0;virtual void SetObject(const BSTSmartPointer<Object>&)=0;};
struct HandlePolicy {int EmptyHandle(){return 0;}int GetHandleForObject(int,TESQuest*){return 1;}};
namespace Internal {
struct VirtualMachine {
 std::shared_ptr<Object> manager; HandlePolicy policy;
 static VirtualMachine* GetSingleton(){static VirtualMachine vm;return &vm;}
 HandlePolicy* GetObjectHandlePolicy(){return &policy;}
 bool FindBoundObject(int,const char*,std::shared_ptr<Object>& out){out=manager;return !!out;}
 template<class A>bool DispatchMethodCall(std::shared_ptr<Object>,const char*,A,std::shared_ptr<IStackCallbackFunctor>){return false;}
};
}
}
template<class...T>int MakeFunctionArguments(T...){return 0;}
}
namespace SKSE::log {template<class...T>void info(T...){} }
