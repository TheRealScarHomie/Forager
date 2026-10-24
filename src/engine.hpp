// reflection helpers
#pragma once

#include "ue4ss_abi.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace forager::ue
{
    using RC::Unreal::FProperty;
    using RC::Unreal::UClass;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    using RC::Unreal::UStruct;

    // 1.0.4 member offsets
    namespace off
    {
        constexpr size_t UObject_Class = 0x10;
        constexpr size_t UStruct_Super = 0x40;
        constexpr size_t UStruct_Children = 0x48;
        constexpr size_t UStruct_ChildProperties = 0x50;
        constexpr size_t UField_Next = 0x28;
        constexpr size_t UEnum_Names = 0x48;
        constexpr size_t UFunction_ParmsSize = 0xB6;
        constexpr size_t FField_Next = 0x20;
        constexpr size_t FProperty_ElementSize = 0x3C;
        constexpr size_t FProperty_Flags = 0x40;
        constexpr size_t FProperty_Offset = 0x4C;
        constexpr size_t FStructProperty_Struct = 0x78;
        constexpr size_t FMapProperty_Layout = 0x88;
        constexpr size_t FBoolProperty_ByteOffset = 0x79;
        constexpr size_t FBoolProperty_ByteMask = 0x7A;
        constexpr size_t FBoolProperty_FieldMask = 0x7B;
    }

    constexpr uint64_t CPF_Parm = 0x80;

    template <class T>
    inline T& at(void* base, size_t offset)
    {
        return *static_cast<T*>(static_cast<void*>(static_cast<uint8_t*>(base) + offset));
    }

    UClass* class_of(UObject* object);
    std::wstring name_of(UObject* object);

    // slow natives
    UObject* find_object(const wchar_t* path);
    std::vector<UObject*> find_all(const wchar_t* class_name);

    // nulling
    struct Weak
    {
        int32_t index = -1;
        int32_t serial = 0;
    };

    Weak weak_of(UObject* object);
    UObject* resolve(const Weak& weak);

    void forget_lookups();
    FProperty* find_property(UStruct* type, std::wstring_view name);
    UFunction* find_function(UStruct* type, std::wstring_view name);
    int32_t offset_of(FProperty* property);
    int32_t size_of(FProperty* property);
    UStruct* struct_of(FProperty* struct_property);
    int64_t enum_value(const wchar_t* enum_path, const std::wstring& entry, int64_t fallback);

    inline UStruct* type_of(UObject* object)
    {
        return static_cast<UStruct*>(static_cast<void*>(class_of(object)));
    }

    template <class T>
    T* field(UObject* object, std::wstring_view name)
    {
        if (!object) return nullptr;
        FProperty* property = find_property(type_of(object), name);
        return property ? &at<T>(object, offset_of(property)) : nullptr;
    }

    inline UObject* object_field(UObject* object, std::wstring_view name)
    {
        UObject** slot = field<UObject*>(object, name);
        return slot ? *slot : nullptr;
    }

    void write_bool(FProperty* property, void* container, bool value);

    struct RawArray
    {
        void* data;
        int32_t num;
        int32_t max;
    };

    void write_fstring(void* memory, const std::wstring& text);
    std::wstring read_fname(void* memory);
    std::wstring read_text(FProperty* text_property, void* memory);
    void write_text(FProperty* text_property, void* memory, const std::wstring& text);

    // tmap live pairs
    template <class F>
    void for_each_pair(void* map_memory, int32_t value_offset, int32_t stride, F&& fn)
    {
        if (!map_memory) return;
        auto* data = at<uint8_t*>(map_memory, 0x00);
        const int32_t num = at<int32_t>(map_memory, 0x08);
        auto* bits = at<uint32_t*>(map_memory, 0x20);
        if (!bits) bits = &at<uint32_t>(map_memory, 0x10);
        const int32_t num_bits = at<int32_t>(map_memory, 0x28);
        if (!data || stride <= 0 || stride > 0x10000 || value_offset < 0 || value_offset >= stride || num < 0 || num > 1000000) return;
        for (int32_t i = 0; i < num && i < num_bits; ++i)
        {
            if ((bits[i >> 5] & (1u << (i & 31))) == 0) continue;
            uint8_t* element = data + static_cast<size_t>(i) * stride;
            if (fn(element, element + value_offset)) break;
        }
    }

    template <class F>
    void for_each_pair(void* map_memory, FProperty* map_property, F&& fn)
    {
        if (!map_property) return;
        const int32_t* layout = &at<int32_t>(map_property, off::FMapProperty_Layout);
        for_each_pair(map_memory, layout[0], layout[5], fn);
    }

    class Call
    {
      public:
        explicit Call(UFunction* function);
        ~Call();
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;

        bool ok() const { return function_ != nullptr; }
        FProperty* param(std::wstring_view name) const;
        uint8_t* ptr(std::wstring_view name) const;

        template <class T>
        bool set(std::wstring_view name, const T& value)
        {
            uint8_t* p = ptr(name);
            if (!p) return false;
            std::memcpy(p, &value, sizeof(T));
            return true;
        }

        template <class T>
        T get(std::wstring_view name, T fallback = T{}) const
        {
            uint8_t* p = ptr(name);
            if (!p) return fallback;
            T value;
            std::memcpy(&value, p, sizeof(T));
            return value;
        }

        bool set_string(std::wstring_view name, const std::wstring& text);
        // copying
        bool copy_in(std::wstring_view name, FProperty* source_property, const void* source);
        bool invoke(UObject* target);

      private:
        UFunction* function_ = nullptr;
        uint8_t* buffer_ = nullptr;
        std::vector<FProperty*> params_;
    };
}
