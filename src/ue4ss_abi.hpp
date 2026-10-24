// UE4SS imports
// UE4SS 3.0.1 zDev git 2281fa31
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RC
{
    namespace LuaMadeSimple
    {
        class Lua;
    }
    namespace GUI
    {
        class GUITab;
    }

    // size 0xC0
    class __declspec(dllimport) CppUserModBase
    {
      public:
        std::vector<std::shared_ptr<GUI::GUITab>> GUITabs;
        std::wstring ModName;
        std::wstring ModVersion;
        std::wstring ModDescription;
        std::wstring ModAuthors;
        std::wstring ModIntendedSDKVersion;

        CppUserModBase();
        virtual ~CppUserModBase();
        virtual void on_update();
        virtual void on_unreal_init();
        virtual void on_ui_init();
        virtual void on_program_start();

        // vtable order matters here
        virtual void on_lua_start(std::wstring_view mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua,
                                  LuaMadeSimple::Lua& async_lua, std::vector<LuaMadeSimple::Lua*>& hook_luas);
        virtual void on_lua_start(LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua,
                                  std::vector<LuaMadeSimple::Lua*>& hook_luas);
        virtual void on_lua_start(std::wstring_view mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua,
                                  LuaMadeSimple::Lua& async_lua, LuaMadeSimple::Lua* hook_lua);
        virtual void on_lua_start(LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua,
                                  LuaMadeSimple::Lua* hook_lua);

        virtual void on_lua_stop(std::wstring_view mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua,
                                 LuaMadeSimple::Lua& async_lua, std::vector<LuaMadeSimple::Lua*>& hook_luas);
        virtual void on_lua_stop(LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua,
                                 std::vector<LuaMadeSimple::Lua*>& hook_luas);
        virtual void on_lua_stop(std::wstring_view mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua,
                                 LuaMadeSimple::Lua& async_lua, LuaMadeSimple::Lua* hook_lua);
        virtual void on_lua_stop(LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua,
                                 LuaMadeSimple::Lua* hook_lua);

        virtual void on_dll_load(std::wstring_view dll_name);
        virtual void render_tab();
        virtual void on_cpp_mods_loaded();
    };

    namespace Output
    {
        __declspec(dllimport) void send(std::wstring_view message);
    }

    namespace Unreal
    {
        class UClass;
        class UStruct;
        class UFunction;
        class UEngine;
        class FOutputDevice;

        class __declspec(dllimport) UObject
        {
          public:
            void ProcessEvent(UFunction* Function, void* Parms);
            std::wstring GetName() const;
        };

        class UFunction : public UObject
        {
        };

        class __declspec(dllimport) FField
        {
          public:
            std::wstring GetName();
        };

        class __declspec(dllimport) FProperty : public FField
        {
          public:
            void InitializeValue_InContainer(void* Dest) const;
            void DestroyValue_InContainer(void* Dest);
            void CopyCompleteValue(void* Dest, const void* Src) const;
        };

        // size 0x8
        class FName
        {
          public:
            uint32_t ComparisonIndex;
            uint32_t Number;
            __declspec(dllimport) std::wstring ToString();
        };

        // size 0x30
        class __declspec(dllimport) FText
        {
          public:
            uint8_t Storage[0x30];
            FText(const wchar_t* Text);
            ~FText();
            std::wstring ToString() const;
        };

        // size 0x8
        struct FWeakObjectPtr
        {
            int32_t ObjectIndex;
            int32_t ObjectSerialNumber;
            __declspec(dllimport) FWeakObjectPtr(const UObject* Object);
            __declspec(dllimport) UObject* Get() const;
        };

        class __declspec(dllimport) FMemory
        {
          public:
            static void* Malloc(unsigned __int64 Count, unsigned int Alignment);
            static void Free(void* Original);
        };

        namespace UObjectGlobals
        {
            __declspec(dllimport) UObject* StaticFindObject_InternalSlow(UClass* Class, UObject* InOuter, const wchar_t* Name, bool ExactClass);
            __declspec(dllimport) void FindAllOf(const wchar_t* ClassName, std::vector<UObject*>& OutObjects);
        }

        namespace Hook
        {
            __declspec(dllimport) void RegisterEngineTickPreCallback(std::function<void(UEngine*, float)> Callback);
            __declspec(dllimport) void RegisterProcessConsoleExecCallback(std::function<bool(UObject*, const wchar_t*, FOutputDevice&, UObject*)> Callback);
        }
    }
}
