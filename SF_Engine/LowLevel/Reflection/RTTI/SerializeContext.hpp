#pragma once
//
// SF Engine :: Reflection :: SerializeContext.h
//
// Usage (mirrors AZ::SerializeContext):
//
//   struct CloudscapeShaderConstantData
//   {
//       SF_TYPE_INFO(CloudscapeShaderConstantData)
//
//       float m_uvwScale = 1.0f;
//       uint32 m_maxMipLevels = 4;
//       ...
//
//       static void Reflect(SF::Reflection::ReflectContext* context)
//       {
//           if (auto* serializeContext = ::rtti_cast<SF::Reflection::SerializeContext>(context))
//           {
//               serializeContext->Class<CloudscapeShaderConstantData>()
//                   ->Version(1)
//                   ->Field("UVWScale", &CloudscapeShaderConstantData::m_uvwScale)
//                   ->Field("MaxMipLevels", &CloudscapeShaderConstantData::m_maxMipLevels);
//           }
//       }
//   };
//
// Fields are stored type-erased (one small heap alloc per field, at
// reflection time only -- never per-frame, never per-instance). Reading
// and writing goes through IWriter/IReader below; wire SerializeValue's
// primitive overloads to your own XML manifest read/write calls (or your
// existing ADL SaveAssetPayload/LoadAssetPayload customization points --
// see the note at the bottom of this file) if you'd rather not maintain
// a second serialization backend.
//
#include <Math/Vectors/Vector.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include "ReflectContext.hpp"

#ifdef Bool
    #undef Bool
#endif

namespace SF::Reflection
{
    using namespace std;
    enum class FieldKind : uint8_t
    {
        Object,
        Float,
        Int,
        UInt,
        Bool,
        String,
        Vec2,
        Vec3,
        Vec4,
        Enum
    };

    template<typename T>
    constexpr FieldKind KindOf()
    {
        if constexpr (is_same_v<T, float>)
            return FieldKind::Float;
        else if constexpr (is_same_v<T, int32_t>)
            return FieldKind::Int;
        else if constexpr (is_same_v<T, uint32_t>)
            return FieldKind::UInt;
        else if constexpr (is_same_v<T, bool>)
            return FieldKind::Bool;
        else if constexpr (is_same_v<T, string>)
            return FieldKind::String;
        else if constexpr (is_same_v<T, Engine::Vec2>)
            return FieldKind::Vec2;
        else if constexpr (is_same_v<T, Engine::Vec3>)
            return FieldKind::Vec3;
        else if constexpr (is_same_v<T, Engine::Vec4>)
            return FieldKind::Vec4;
        else if constexpr (is_enum_v<T>)
            return FieldKind::Enum;
        else
            return FieldKind::Object;
    }

    template<typename T, typename = void>
    struct ReflectedTypeName
    {
    };

    template<typename T>
    struct ReflectedTypeName<T, enable_if_t<HasRtti<T>>>
    {
        static const char *Get() { return T::RTTI_TypeName(); }
    };

    template<typename T, typename = void>
    struct IsReflectable : false_type
    {
    };

    template<typename T>
    struct IsReflectable<T, void_t<decltype(ReflectedTypeName<T>::Get())>> : true_type
    {
    };

    class SerializeContext;

    class IWriter
    {
    public:
        virtual ~IWriter()                                          = default;
        virtual void WriteFloat(const char *name, float v)          = 0;
        virtual void WriteUInt32(const char *name, uint32_t v)      = 0;
        virtual void WriteInt32(const char *name, int32_t v)        = 0;
        virtual void WriteBool(const char *name, bool v)            = 0;
        virtual void WriteString(const char *name, const string &v) = 0;
        virtual void BeginObject(const char *name)                  = 0;
        virtual void EndObject()                                    = 0;
    };

    class IReader
    {
    public:
        virtual ~IReader()                                       = default;
        virtual bool ReadFloat(const char *name, float &out)     = 0;
        virtual bool ReadUInt32(const char *name, uint32_t &out) = 0;
        virtual bool ReadInt32(const char *name, int32_t &out)   = 0;
        virtual bool ReadBool(const char *name, bool &out)       = 0;
        virtual bool ReadString(const char *name, string &out)   = 0;
        virtual bool BeginObject(const char *name)               = 0;
        virtual void EndObject()                                 = 0;
    };

    struct ClassData
    {
        TypeId typeId;
        const char *name = nullptr;
        uint32_t version = 0;
        vector<unique_ptr<class IFieldBinding>> fields;
    };

    struct FieldMeta
    {
        const char *displayName = nullptr; // defaults to field name
        const char *tooltip     = nullptr;
        const char *format      = nullptr; // "%.2f", "%.1f deg"
        const char *enumItems   = nullptr; // "Point\0Spot\0Directional\0"
        float min = 0, max = 0, speed = 0.1f;
        float resetVec[3] = {0, 0, 0};
        bool hasRange = false, slider = false, color = false, hidden = false, readOnly = false;
        function<bool(const void *)> visible; // null = always
    };

    class IFieldBinding
    {
    public:
        virtual ~IFieldBinding()                                                                        = default;
        [[nodiscard]] virtual const char *GetName() const                                               = 0;
        [[nodiscard]] virtual TypeId GetFieldTypeId() const                                             = 0;
        virtual void Save(const void *instance, IWriter &writer, const SerializeContext &context) const = 0;
        virtual void Load(void *instance, IReader &reader, const SerializeContext &context) const       = 0;

        [[nodiscard]] virtual FieldKind Kind() const              = 0;
        virtual void *GetPtr(void *instance) const                = 0;
        [[nodiscard]] virtual bool IsEnum() const                 = 0;
        virtual int32_t GetEnum(const void *instance) const       = 0;
        virtual void SetEnum(void *instance, int32_t value) const = 0;
        FieldMeta meta;
    };

    template<typename FieldT>
    void SerializeValue(IWriter &writer, const char *name, const FieldT &value, const SerializeContext &context);

    template<typename FieldT>
    bool DeserializeValue(IReader &reader, const char *name, FieldT &value, const SerializeContext &context);

    template<typename ClassT, typename FieldT>
    class FieldBinding final : public IFieldBinding
    {
    public:
        FieldBinding(const char *name, FieldT ClassT::*member) : m_name(name), m_member(member) {}

        [[nodiscard]] const char *GetName() const override { return m_name; }
        [[nodiscard]] TypeId GetFieldTypeId() const override { return GetTypeId<FieldT>(); }

        void Save(const void *instance, IWriter &writer, const SerializeContext &context) const override
        {
            const auto *obj = static_cast<const ClassT *>(instance);
            SerializeValue<FieldT>(writer, m_name, obj->*m_member, context);
        }

        void Load(void *instance, IReader &reader, const SerializeContext &context) const override
        {
            auto *obj = static_cast<ClassT *>(instance);
            DeserializeValue<FieldT>(reader, m_name, obj->*m_member, context);
        }

        [[nodiscard]] bool IsEnum() const override { return is_enum_v<FieldT>; }

        [[nodiscard]] FieldKind Kind() const override { return KindOf<FieldT>(); }
        void *GetPtr(void *i) const override { return &(static_cast<ClassT *>(i)->*m_member); }
        int32_t GetEnum(const void *i) const override
        {
            if constexpr (is_enum_v<FieldT>)
                return static_cast<int32_t>(static_cast<const ClassT *>(i)->*m_member);
            else
                return 0;
        }
        void SetEnum(void *i, int32_t v) const override
        {
            if constexpr (is_enum_v<FieldT>)
                static_cast<ClassT *>(i)->*m_member = static_cast<FieldT>(v);
        }

    private:
        const char *m_name;
        FieldT ClassT::*m_member;
    };

    template<typename T>
    class ClassBuilder
    {
    public:
        ClassBuilder(ClassData &data) : m_data(data) {}

        ClassBuilder &Version(const uint32_t version)
        {
            m_data.version = version;
            return *this;
        }

        template<typename FieldT>
        ClassBuilder &Field(const char *name, FieldT T::*member)
        {
            m_data.fields.push_back(make_unique<FieldBinding<T, FieldT>>(name, member));
            return *this;
        }

        ClassBuilder *operator->() { return this; }

        ClassBuilder &Range(const float lo, const float hi)
        {
            M().min      = lo;
            M().max      = hi;
            M().hasRange = true;
            return *this;
        }

        ClassBuilder &Slider(const float lo, const float hi)
        {
            Range(lo, hi);
            M().slider = true;
            return *this;
        }

        ClassBuilder &Speed(const float s)
        {
            M().speed = s;
            return *this;
        }

        ClassBuilder &Format(const char *f)
        {
            M().format = f;
            return *this;
        }

        ClassBuilder &Color()
        {
            M().color = true;
            return *this;
        }

        ClassBuilder &Tooltip(const char *t)
        {
            M().tooltip = t;
            return *this;
        }

        ClassBuilder &DisplayName(const char *n)
        {
            M().displayName = n;
            return *this;
        }

        ClassBuilder &Enum(const char *items)
        {
            M().enumItems = items;
            return *this;
        }

        ClassBuilder &Hidden()
        {
            M().hidden = true;
            return *this;
        }

        ClassBuilder &ReadOnly()
        {
            M().readOnly = true;
            return *this;
        }

        ClassBuilder &ResetTo(const float x, const float y, const float z)
        {
            M().resetVec[0] = x;
            M().resetVec[1] = y;
            M().resetVec[2] = z;
            return *this;
        }

        template<typename F>
        ClassBuilder &ShowIf(F &&f)
        {
            M().visible = [f = std::forward<F>(f)](const void *o) { return f(*static_cast<const T *>(o)); };
            return *this;
        }

    private:
        [[nodiscard]] FieldMeta &M() const { return m_data.fields.back()->meta; }
        ClassData &m_data;
    };

    class SerializeContext : public ReflectContext
    {
        SF_RTTI(SerializeContext, ReflectContext)
    public:
        static SerializeContext &Instance()
        {
            static SerializeContext instance;
            return instance;
        }

        template<typename T>
        ClassBuilder<T> Class()
        {
            static_assert(IsReflectable<T>::value, "T needs SF_TYPE_INFO (or SF_RTTI/SF_RTTI_BASE) to be reflected, "
                                                   "or use SF_REFLECT_EXTERNAL_TYPE(T) for external/POD types.");
            const TypeId id                   = GetTypeId<T>();
            ClassData &data                   = m_classes[id];
            m_stdTypes[type_index(typeid(T))] = id;
            data.typeId                       = id;
            data.name                         = ReflectedTypeName<T>::Get();
            return ClassBuilder<T>(data);
        }

        const TypeId *FindTypeId(const type_index ti) const
        {
            const auto it = m_stdTypes.find(ti);
            return it != m_stdTypes.end() ? &it->second : nullptr;
        }

        const ClassData *FindClassData(const TypeId id) const
        {
            const auto it = m_classes.find(id);
            return it != m_classes.end() ? &it->second : nullptr;
        }

        template<typename T>
        void Save(const T &instance, IWriter &writer) const
        {
            if (const ClassData *data = FindClassData(GetTypeId<T>()))
            {
                for (auto &field: data->fields)
                {
                    field->Save(&instance, writer, *this);
                }
            }
        }

        template<typename T>
        void Load(T &instance, IReader &reader) const
        {
            if (const ClassData *data = FindClassData(GetTypeId<T>()))
            {
                for (auto &field: data->fields)
                {
                    field->Load(&instance, reader, *this);
                }
            }
        }

    private:
        unordered_map<TypeId, ClassData> m_classes;
        unordered_map<std::type_index, TypeId> m_stdTypes;
    };

    template<typename FieldT>
    void SerializeValue(IWriter &writer, const char *name, const FieldT &value, const SerializeContext &context)
    {
        if constexpr (is_same_v<FieldT, float>)
        {
            writer.WriteFloat(name, value);
        } else if constexpr (is_same_v<FieldT, uint32_t>)
        {
            writer.WriteUInt32(name, value);
        } else if constexpr (is_same_v<FieldT, int32_t>)
        {
            writer.WriteInt32(name, value);
        } else if constexpr (is_same_v<FieldT, bool>)
        {
            writer.WriteBool(name, value);
        } else if constexpr (is_same_v<FieldT, string>)
        {
            writer.WriteString(name, value);
        } else if constexpr (is_enum_v<FieldT>)
        {
            writer.WriteInt32(name, static_cast<int32_t>(value));
        } else
        {
            writer.BeginObject(name);
            if (const ClassData *nested = context.FindClassData(GetTypeId<FieldT>()))
            {
                for (auto &field: nested->fields)
                {
                    field->Save(&value, writer, context);
                }
            }
            writer.EndObject();
        }
    }

    template<typename FieldT>
    bool DeserializeValue(IReader &reader, const char *name, FieldT &value, const SerializeContext &context)
    {
        if constexpr (is_same_v<FieldT, float>)
        {
            return reader.ReadFloat(name, value);
        } else if constexpr (is_same_v<FieldT, uint32_t>)
        {
            return reader.ReadUInt32(name, value);
        } else if constexpr (is_same_v<FieldT, int32_t>)
        {
            return reader.ReadInt32(name, value);
        } else if constexpr (is_same_v<FieldT, bool>)
        {
            return reader.ReadBool(name, value);
        } else if constexpr (is_same_v<FieldT, string>)
        {
            return reader.ReadString(name, value);
        } else if constexpr (is_enum_v<FieldT>)
        {
            int32_t raw{};
            if (!reader.ReadInt32(name, raw))
            {
                return false;
            }
            value = static_cast<FieldT>(raw);
            return true;
        } else
        {
            if (!reader.BeginObject(name))
            {
                return false;
            }
            if (const ClassData *nested = context.FindClassData(GetTypeId<FieldT>()))
            {
                for (auto &field: nested->fields)
                {
                    field->Load(&value, reader, context);
                }
            }
            reader.EndObject();
            return true;
        }
    }

} // namespace SF::Reflection

#define SF_REFLECT_EXTERNAL_TYPE(TypeName)                                                                             \
    template<>                                                                                                         \
    struct SF::Reflection::ReflectedTypeName<TypeName>                                                                 \
    {                                                                                                                  \
        static const char *Get() { return #TypeName; }                                                                 \
    };
