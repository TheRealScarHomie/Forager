#include "engine.hpp"

#include <cwchar>
#include <malloc.h>
#include <unordered_map>

namespace forager::ue
{
    using RC::Unreal::FField;

    namespace
    {
        struct LookupKey
        {
            void* type;
            std::wstring name;
            bool operator==(const LookupKey& o) const { return type == o.type && name == o.name; }
        };

        struct LookupHash
        {
            size_t operator()(const LookupKey& k) const
            {
                return std::hash<void*>()(k.type) ^ (std::hash<std::wstring>()(k.name) * 1099511628211ull);
            }
        };

        std::unordered_map<LookupKey, FProperty*, LookupHash> g_properties;
        std::unordered_map<LookupKey, UFunction*, LookupHash> g_functions;
        std::unordered_map<std::wstring, UObject*> g_objects;

        UStruct* super_of(UStruct* type)
        {
            return at<UStruct*>(type, off::UStruct_Super);
        }

        // fnames ignore-c
        bool same_name(const std::wstring& a, std::wstring_view b)
        {
            return a.size() == b.size() && _wcsnicmp(a.c_str(), b.data(), b.size()) == 0;
        }
    }

    UClass* class_of(UObject* object)
    {
        return object ? at<UClass*>(object, off::UObject_Class) : nullptr;
    }

    std::wstring name_of(UObject* object)
    {
        return object ? object->GetName() : std::wstring(L"null");
    }

    UObject* find_object(const wchar_t* path)
    {
        auto cached = g_objects.find(path);
        if (cached != g_objects.end()) return cached->second;
        UObject* object = RC::Unreal::UObjectGlobals::StaticFindObject_InternalSlow(nullptr, nullptr, path, false);
        if (object) g_objects[path] = object;
        return object;
    }

    std::vector<UObject*> find_all(const wchar_t* class_name)
    {
        std::vector<UObject*> out;
        RC::Unreal::UObjectGlobals::FindAllOf(class_name, out);
        return out;
    }

    Weak weak_of(UObject* object)
    {
        Weak out;
        if (!object) return out;
        const RC::Unreal::FWeakObjectPtr pointer(object);
        out.index = pointer.ObjectIndex;
        out.serial = pointer.ObjectSerialNumber;
        return out;
    }

    UObject* resolve(const Weak& weak)
    {
        if (weak.serial == 0 || weak.index < 0) return nullptr;
        return static_cast<const RC::Unreal::FWeakObjectPtr*>(static_cast<const void*>(&weak))->Get();
    }

    void forget_lookups()
    {
        g_properties.clear();
        g_functions.clear();
    }

    FProperty* find_property(UStruct* type, std::wstring_view name)
    {
        if (!type) return nullptr;
        LookupKey key{type, std::wstring(name)};
        auto cached = g_properties.find(key);
        if (cached != g_properties.end()) return cached->second;

        FProperty* found = nullptr;
        for (UStruct* s = type; s && !found; s = super_of(s))
        {
            for (auto* f = at<FField*>(s, off::UStruct_ChildProperties); f; f = at<FField*>(f, off::FField_Next))
            {
                if (same_name(f->GetName(), name))
                {
                    found = static_cast<FProperty*>(f);
                    break;
                }
            }
        }
        g_properties[key] = found;
        return found;
    }

    UFunction* find_function(UStruct* type, std::wstring_view name)
    {
        if (!type) return nullptr;
        LookupKey key{type, std::wstring(name)};
        auto cached = g_functions.find(key);
        if (cached != g_functions.end()) return cached->second;

        UFunction* found = nullptr;
        for (UStruct* s = type; s && !found; s = super_of(s))
        {
            for (auto* child = at<UObject*>(s, off::UStruct_Children); child; child = at<UObject*>(child, off::UField_Next))
            {
                if (same_name(child->GetName(), name))
                {
                    found = static_cast<UFunction*>(child);
                    break;
                }
            }
        }
        g_functions[key] = found;
        return found;
    }

    int32_t offset_of(FProperty* property)
    {
        return at<int32_t>(property, off::FProperty_Offset);
    }

    int32_t size_of(FProperty* property)
    {
        return at<int32_t>(property, off::FProperty_ElementSize);
    }

    UStruct* struct_of(FProperty* struct_property)
    {
        return struct_property ? at<UStruct*>(struct_property, off::FStructProperty_Struct) : nullptr;
    }

    int64_t enum_value(const wchar_t* enum_path, const std::wstring& entry, int64_t fallback)
    {
        UObject* object = find_object(enum_path);
        if (!object) return fallback;
        const RawArray& names = at<RawArray>(object, off::UEnum_Names);
        if (!names.data || names.num <= 0 || names.num > 4096) return fallback;
        for (int32_t i = 0; i < names.num; ++i)
        {
            uint8_t* pair = static_cast<uint8_t*>(names.data) + static_cast<size_t>(i) * 16;
            if (_wcsicmp(read_fname(pair).c_str(), entry.c_str()) == 0) return at<int64_t>(pair, 8);
        }
        return fallback;
    }

    void write_bool(FProperty* property, void* container, bool value)
    {
        if (!property || !container) return;
        const uint8_t byte_offset = at<uint8_t>(property, off::FBoolProperty_ByteOffset);
        const uint8_t byte_mask = at<uint8_t>(property, off::FBoolProperty_ByteMask);
        const uint8_t field_mask = at<uint8_t>(property, off::FBoolProperty_FieldMask);
        uint8_t& byte = at<uint8_t>(container, offset_of(property) + byte_offset);
        byte = static_cast<uint8_t>((byte & ~field_mask) | (value ? byte_mask : 0));
    }

    void write_fstring(void* memory, const std::wstring& text)
    {
        if (!memory) return;
        auto* string = static_cast<RawArray*>(memory);
        if (string->data) RC::Unreal::FMemory::Free(string->data);
        *string = RawArray{nullptr, 0, 0};
        if (text.empty()) return;
        const int32_t count = static_cast<int32_t>(text.size()) + 1;
        auto* data = static_cast<wchar_t*>(RC::Unreal::FMemory::Malloc(static_cast<size_t>(count) * sizeof(wchar_t), 0));
        if (!data) return;
        std::memcpy(data, text.c_str(), static_cast<size_t>(count) * sizeof(wchar_t));
        *string = RawArray{data, count, count};
    }

    std::wstring read_fname(void* memory)
    {
        if (!memory) return L"";
        uint8_t copy[8];
        std::memcpy(copy, memory, 8);
        return static_cast<RC::Unreal::FName*>(static_cast<void*>(copy))->ToString();
    }

    std::wstring read_text(FProperty* text_property, void* memory)
    {
        if (!text_property || !memory) return L"";
        RC::Unreal::FText holder(L"");
        text_property->CopyCompleteValue(&holder, memory);
        return holder.ToString();
    }

    void write_text(FProperty* text_property, void* memory, const std::wstring& text)
    {
        if (!text_property || !memory) return;
        RC::Unreal::FText source(text.c_str());
        text_property->CopyCompleteValue(memory, &source);
    }

    Call::Call(UFunction* function) : function_(function)
    {
        if (!function_) return;
        const size_t size = at<uint16_t>(function_, off::UFunction_ParmsSize);
        buffer_ = static_cast<uint8_t*>(_aligned_malloc(size + 16, 16));
        std::memset(buffer_, 0, size + 16);
        for (auto* f = at<FField*>(function_, off::UStruct_ChildProperties); f; f = at<FField*>(f, off::FField_Next))
        {
            auto* property = static_cast<FProperty*>(f);
            if ((at<uint64_t>(property, off::FProperty_Flags) & CPF_Parm) == 0) continue;
            property->InitializeValue_InContainer(buffer_);
            params_.push_back(property);
        }
    }

    Call::~Call()
    {
        if (!buffer_) return;
        for (FProperty* property : params_) property->DestroyValue_InContainer(buffer_);
        _aligned_free(buffer_);
    }

    FProperty* Call::param(std::wstring_view name) const
    {
        return function_ ? find_property(static_cast<UStruct*>(static_cast<void*>(function_)), name) : nullptr;
    }

    uint8_t* Call::ptr(std::wstring_view name) const
    {
        FProperty* property = param(name);
        return property ? buffer_ + offset_of(property) : nullptr;
    }

    bool Call::set_string(std::wstring_view name, const std::wstring& text)
    {
        uint8_t* p = ptr(name);
        if (!p) return false;
        write_fstring(p, text);
        return true;
    }

    bool Call::copy_in(std::wstring_view name, FProperty* source_property, const void* source)
    {
        FProperty* target = param(name);
        if (!target || !source_property || !source || size_of(target) != size_of(source_property)) return false;
        target->CopyCompleteValue(buffer_ + offset_of(target), source);
        return true;
    }

    bool Call::invoke(UObject* target)
    {
        if (!function_ || !target) return false;
        target->ProcessEvent(function_, buffer_);
        return true;
    }
}
