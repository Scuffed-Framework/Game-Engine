#pragma once
/*
Compile-time reflection, no external deps (C++17, MSVC / Clang / GCC).
    1. Type names            SF::Reflection::TypeName<T>(), StaticTypeId<T>()
    2. Enum reflection       enum_count / enum_values / enum_names / enum_name /
                            enum_cast / enum_index / enum_contains / enum_for_each
    3. Flag enums            enum_flags_name / enum_flags_cast
    4. Member reflection     SF_FIELDS / SF_FIELD  +  for_each_field / visit_field

    enum class Blend : uint8_t { Opaque, Alpha, Additive };

    static_assert(SF::Reflection::enum_count<Blend>() == 3);
    constexpr auto n = SF::Reflection::enum_name(Blend::Alpha);      // "Alpha"
    auto b = SF::Reflection::enum_cast<Blend>("Additive");           // optional<Blend>
    auto c = SF::Reflection::enum_cast<Blend>(1);                    // optional<Blend>, Alpha

Values are discovered by probing a range (default -128..128, or 0..128 for
unsigned underlying types). Widen/narrow it per enum BEFORE first use:

    SF_ENUM_RANGE(MyEnum, 0, 1024)

Bit-flag enums: probes every bit instead of a numeric range.

    enum class Usage : uint32_t { None = 0, Vertex = 1, Index = 2, Uniform = 4 };
    SF_ENUM_FLAGS(Usage)
    enum_flags_name(Usage::Vertex | Usage::Uniform)   // "Vertex|Uniform"

Caveat (same as magic_enum unfortunately): plain "enum Foo { A, B }" with NO fixed
underlying type makes probing values outside its range ill-formed on
Clang/GCC. Prefer "enum class Foo : Type", or use SF_ENUM_RANGE(Foo, 0, N).

    struct Cloud
    {
       SF_TYPE_INFO(Cloud)
       float    m_scale = 1.0f;
       uint32_t m_mips  = 4;
       SF_FIELDS(SF_FIELD(m_scale), SF_FIELD(m_mips))
    };

    SF::Reflection::for_each_field(cloud, [](string_view name, auto& v) { ... });
    SF::Reflection::visit_field(cloud, "m_mips", [](auto& v) { v = 8; });
*/
#include "../RTTI/TypeId.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace SF::Reflection
{

    namespace Detail
    {
#if defined(_MSC_VER)
        inline constexpr std::string_view TypeKey = "TypeSig<";
        inline constexpr std::string_view EnumKey = "EnumSig<";
#else
        inline constexpr std::string_view TypeKey = "T = ";
        inline constexpr std::string_view EnumKey = "V = ";
#endif

        template<typename T>
        constexpr const char *TypeSig()
        {
            return SF_RTTI_FUNC_SIG;
        }

        template<auto V>
        constexpr const char *EnumSig()
        {
            return SF_RTTI_FUNC_SIG;
        }

        // Get the template argument text out of a compiler function signature
        constexpr std::string_view ExtractArg(const std::string_view sig, const std::string_view key)
        {
            const auto b = sig.find(key);
            if (b == std::string_view::npos)
                return {};
            const auto start = b + key.size();
#if defined(_MSC_VER)
            const auto e = sig.rfind(">(");
#else
            const auto e = sig.rfind(']');
#endif
            if (e == std::string_view::npos || e < start)
                return {};
            return sig.substr(start, e - start);
        }

        constexpr bool StartsWith(const std::string_view s, const std::string_view p)
        {
            return s.size() >= p.size() && s.substr(0, p.size()) == p;
        }

        constexpr std::string_view StripKeyword(const std::string_view s)
        {
            for (const std::string_view kw: {std::string_view("struct "), std::string_view("class "),
                                             std::string_view("enum "), std::string_view("union ")})
            {
                if (StartsWith(s, kw))
                    return s.substr(kw.size());
            }
            return s;
        }

        constexpr bool IsIdentStart(const char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        constexpr std::string_view LastSegment(const std::string_view s)
        {
            const auto p = s.rfind("::");
            return p == std::string_view::npos ? s : s.substr(p + 2);
        }

        constexpr uint64_t Fnv1a(const std::string_view s)
        {
            uint64_t h = FnvOffset;
            for (const char c: s)
                h = (h ^ static_cast<uint64_t>(static_cast<unsigned char>(c))) * ::SF::Reflection::Detail::FnvPrime;
            return h;
        }
    } // namespace Detail

    // Fully qualified, compiler-spelled type name, e.g. "namespace::Foo<int>".
    template<typename T>
    constexpr std::string_view TypeName()
    {
        return Detail::StripKeyword(Detail::ExtractArg(Detail::TypeSig<T>(), Detail::TypeKey));
    }

    template<typename T>
    constexpr TypeId StaticTypeId()
    {
        return TypeId(Detail::Fnv1a(TypeName<std::decay_t<T>>()));
    }

    template<typename E>
    struct EnumRange
    {
        using U                       = std::underlying_type_t<E>;
        static constexpr int Min      = std::is_signed_v<U> ? -128 : 0;
        static constexpr int Max      = 128; // inclusive
        static constexpr bool IsFlags = false;
    };

    namespace Detail
    {
        template<typename E>
        constexpr int CandidateCount()
        {
            using R = EnumRange<E>;
            if constexpr (R::IsFlags)
                return static_cast<int>(sizeof(std::underlying_type_t<E>) * 8) + 1; // zero + each bit
            else
            {
                static_assert(R::Max >= R::Min, "EnumRange: Max must be >= Min");
                return R::Max - R::Min + 1;
            }
        }

        template<typename E, int I>
        constexpr E Candidate()
        {
            using R = EnumRange<E>;
            using U = std::underlying_type_t<E>;
            if constexpr (R::IsFlags)
            {
                using UU = std::make_unsigned_t<U>;
                if constexpr (I == 0)
                    return static_cast<E>(static_cast<U>(0));
                else
                    return static_cast<E>(static_cast<U>(static_cast<UU>(UU{1} << (I - 1))));
            } else
            {
                return static_cast<E>(static_cast<U>(R::Min + I));
            }
        }

        // Empty result == "not a named enumerator".
        template<auto V>
        constexpr std::string_view EnumNameOf()
        {
            static_assert(std::is_enum_v<decltype(V)>, "EnumNameOf expects an enum value");
            if constexpr (constexpr std::string_view raw = Detail::ExtractArg(EnumSig<V>(), EnumKey);
                          raw.empty() || !IsIdentStart(raw[0]))
                return {}; // "(Color)5" / "(enum Color)0x5" style output for unnamed values
            else
                return LastSegment(raw);
        }

        template<typename E, int... I>
        constexpr std::array<std::string_view, sizeof...(I)> RawNames(std::integer_sequence<int, I...>)
        {
            return {{EnumNameOf<Candidate<E, I>()>()...}};
        }

        template<typename E, int... I>
        constexpr std::array<E, sizeof...(I)> RawValues(std::integer_sequence<int, I...>)
        {
            return {{Candidate<E, I>()...}};
        }

        template<size_t N>
        constexpr size_t CountNamed(const std::array<std::string_view, N> &names)
        {
            size_t n = 0;
            for (const auto &s: names)
                n += s.empty() ? 0 : 1;
            return n;
        }

        template<typename E, size_t Count>
        struct Compacted
        {
            std::array<E, Count> values{};
            std::array<std::string_view, Count> names{};
        };

        template<typename E, size_t Count, size_t N>
        constexpr Compacted<E, Count> Compact(const std::array<E, N> &vals,
                                              const std::array<std::string_view, N> &names)
        {
            Compacted<E, Count> out{};
            size_t k = 0;
            for (size_t i = 0; i < N; ++i)
            {
                if (!names[i].empty())
                {
                    out.values[k] = vals[i];
                    out.names[k]  = names[i];
                    ++k;
                }
            }
            return out;
        }

        template<typename E>
        struct EnumTable
        {
            static_assert(std::is_enum_v<E>, "enum reflection requires an enum type");

            static constexpr int N          = CandidateCount<E>();
            static constexpr auto AllNames  = RawNames<E>(std::make_integer_sequence<int, N>{});
            static constexpr auto AllValues = RawValues<E>(std::make_integer_sequence<int, N>{});
            static constexpr size_t Count   = CountNamed(AllNames);
            static constexpr auto Data      = Compact<E, Count>(AllValues, AllNames);
        };

        constexpr char ToLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

        constexpr bool EqualsNoCase(const std::string_view a, const std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (ToLower(a[i]) != ToLower(b[i]))
                    return false;
            return true;
        }

        constexpr std::string_view Trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        }
    } // namespace Detail

    template<typename E>
    constexpr size_t enum_count() noexcept
    {
        return Detail::EnumTable<E>::Count;
    }

    template<typename E>
    inline constexpr size_t enum_count_v = Detail::EnumTable<E>::Count;

    template<typename E>
    constexpr const auto &enum_values() noexcept
    {
        return Detail::EnumTable<E>::Data.values;
    }

    template<typename E>
    constexpr const auto &enum_names() noexcept
    {
        return Detail::EnumTable<E>::Data.names;
    }

    template<typename E>
    constexpr auto enum_entries() noexcept
    {
        constexpr size_t N = enum_count<E>();
        std::array<std::pair<E, std::string_view>, N> out{};
        for (size_t i = 0; i < N; ++i)
            out[i] = {enum_values<E>()[i], enum_names<E>()[i]};
        return out;
    }

    template<typename E>
    constexpr std::string_view enum_type_name() noexcept
    {
        return TypeName<E>();
    }

    template<typename E>
    constexpr std::optional<size_t> enum_index(E value) noexcept
    {
        const auto &vals = enum_values<E>();
        for (size_t i = 0; i < vals.size(); ++i)
            if (vals[i] == value)
                return i;
        return std::nullopt;
    }

    template<typename E>
    constexpr bool enum_contains(E value) noexcept
    {
        return enum_index(value).has_value();
    }

    template<typename E>
    constexpr std::string_view enum_name(E value) noexcept
    {
        if (auto i = enum_index(value))
            return enum_names<E>()[*i];
        return {};
    }

    template<typename E>
    constexpr std::optional<E> enum_cast(std::string_view name, bool ignoreCase = false) noexcept
    {
        const auto &names = enum_names<E>();
        for (size_t i = 0; i < names.size(); ++i)
        {
            if (ignoreCase ? Detail::EqualsNoCase(names[i], name) : names[i] == name)
                return enum_values<E>()[i];
        }
        return std::nullopt;
    }

    template<typename E, typename I, typename = std::enable_if_t<std::is_integral_v<I>>>
    constexpr std::optional<E> enum_cast(I raw) noexcept
    {
        for (const auto &vals = enum_values<E>(); const E v: vals)
            if (static_cast<std::underlying_type_t<E>>(v) == static_cast<std::underlying_type_t<E>>(raw))
                return v;
        return std::nullopt;
    }

    template<typename E>
    constexpr auto enum_underlying(E value) noexcept
    {
        return static_cast<std::underlying_type_t<E>>(value);
    }

    // f(E value, std::string_view name) for every enumerator, in value order.
    template<typename E, typename F>
    constexpr void enum_for_each(F &&f)
    {
        const auto &vals  = enum_values<E>();
        const auto &names = enum_names<E>();
        for (size_t i = 0; i < vals.size(); ++i)
            f(vals[i], names[i]);
    }

    // "Vertex|Uniform". Unnamed leftover bits are appended as hex ("0x40").
    // Zero returns the name of the zero enumerator if any, else "0".
    template<typename E>
    std::string enum_flags_name(E value, const std::string_view sep = "|")
    {
        static_assert(EnumRange<E>::IsFlags, "enum_flags_name requires SF_ENUM_FLAGS(E)");
        using U  = std::underlying_type_t<E>;
        using UU = std::make_unsigned_t<U>;

        UU remaining = static_cast<UU>(static_cast<U>(value));
        if (remaining == 0)
        {
            const auto n = enum_name(value);
            return n.empty() ? std::string("0") : std::string(n);
        }

        std::string out;
        const auto &vals  = enum_values<E>();
        const auto &names = enum_names<E>();
        for (size_t i = 0; i < vals.size(); ++i)
        {
            const UU bit = static_cast<UU>(static_cast<U>(vals[i]));
            if (bit != 0 && (remaining & bit) == bit)
            {
                if (!out.empty())
                    out += sep;
                out += names[i];
                remaining = static_cast<UU>(remaining & ~bit);
            }
        }
        if (remaining != 0)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string hex;
            for (UU r = remaining; r != 0; r = static_cast<UU>(r >> 4))
                hex.insert(hex.begin(), digits[r & 0xF]);
            if (!out.empty())
                out += sep;
            out += "0x";
            out += hex;
        }
        return out;
    }

    template<typename E>
    std::optional<E> enum_flags_cast(std::string_view text, const char sep = '|', const bool ignoreCase = false)
    {
        static_assert(EnumRange<E>::IsFlags, "enum_flags_cast requires SF_ENUM_FLAGS(E)");
        using U  = std::underlying_type_t<E>;
        using UU = std::make_unsigned_t<U>;

        UU acc = 0;
        while (true)
        {
            const auto p   = text.find(sep);
            const auto tok = Detail::Trim(text.substr(0, p));
            if (tok.empty())
                return std::nullopt;
            const auto e = enum_cast<E>(tok, ignoreCase);
            if (!e)
                return std::nullopt;
            acc = static_cast<UU>(acc | static_cast<UU>(static_cast<U>(*e)));
            if (p == std::string_view::npos)
                break;
            text.remove_prefix(p + 1);
        }
        return static_cast<E>(static_cast<U>(acc));
    }

    template<typename C, typename M>
    struct FieldInfo
    {
        using ClassType  = C;
        using MemberType = M;

        std::string_view name;
        M C::*ptr;

        template<typename O>
        constexpr decltype(auto) Get(O &&obj) const
        {
            return std::forward<O>(obj).*ptr;
        }
    };

    template<typename C, typename M>
    constexpr FieldInfo<C, M> MakeField(std::string_view name, M C::*ptr) noexcept
    {
        return FieldInfo<C, M>{name, ptr};
    }

    template<typename... Fs>
    constexpr auto MakeFields(Fs... fs)
    {
        return std::make_tuple(fs...);
    }

    namespace Detail
    {
        template<typename T, typename = void>
        struct HasFieldsImpl : std::false_type
        {
        };

        template<typename T>
        struct HasFieldsImpl<T, std::void_t<decltype(T::RTTI_Fields())>> : std::true_type
        {
        };
    } // namespace Detail

    template<typename T>
    inline constexpr bool HasFields = Detail::HasFieldsImpl<std::remove_cv_t<std::remove_reference_t<T>>>::value;

    template<typename T>
    constexpr size_t field_count() noexcept
    {
        static_assert(HasFields<T>, "Type has no SF_FIELDS declared.");
        return std::tuple_size_v<decltype(std::remove_cv_t<std::remove_reference_t<T>>::RTTI_Fields())>;
    }

    // f(const FieldInfo&) for each field; works on the type alone (no instance).
    template<typename T, typename F>
    constexpr void for_each_field_info(F &&f)
    {
        static_assert(HasFields<T>, "Type has no SF_FIELDS declared.");
        std::apply([&](const auto &...fi) { (void) (f(fi), ...); },
                   std::remove_cv_t<std::remove_reference_t<T>>::RTTI_Fields());
    }

    // f(string_view name, FieldRef value) for each field.
    template<typename T, typename F>
    constexpr void for_each_field(T &&obj, F &&f)
    {
        static_assert(HasFields<T>, "Type has no SF_FIELDS declared.");
        std::apply([&](const auto &...fi) { (f(fi.name, fi.Get(obj)), ...); },
                   std::remove_cv_t<std::remove_reference_t<T>>::RTTI_Fields());
    }

    // Calls f(FieldRef) on the field called "name". Returns false if none matches.
    template<typename T, typename F>
    constexpr bool visit_field(T &&obj, std::string_view name, F &&f)
    {
        static_assert(HasFields<T>, "Type has no SF_FIELDS declared.");
        bool found = false;
        std::apply([&](const auto &...fi)
                   { ((fi.name == name ? (void) ((void) f(fi.Get(obj)), found = true) : (void) 0), ...); },
                   std::remove_cv_t<std::remove_reference_t<T>>::RTTI_Fields());
        return found;
    }
} // namespace SF::Reflection

// Set the probe range for an enum. Place at global scope, before first use.
#define SF_ENUM_RANGE(EnumType, MinValue, MaxValue)                                                                    \
    template<>                                                                                                         \
    struct SF::Reflection::EnumRange<EnumType>                                                                         \
    {                                                                                                                  \
        static constexpr int Min      = (MinValue);                                                                    \
        static constexpr int Max      = (MaxValue);                                                                    \
        static constexpr bool IsFlags = false;                                                                         \
    };

// Mark an enum as a bit-flag enum. Place at global scope, before first use.
#define SF_ENUM_FLAGS(EnumType)                                                                                        \
    template<>                                                                                                         \
    struct SF::Reflection::EnumRange<EnumType>                                                                         \
    {                                                                                                                  \
        static constexpr int Min      = 0;                                                                             \
        static constexpr int Max      = 0;                                                                             \
        static constexpr bool IsFlags = true;                                                                          \
    };

// Inside a class that has SF_TYPE_INFO / SF_RTTI / SF_RTTI_BASE:
//   SF_FIELDS(SF_FIELD(m_a), SF_FIELD(m_b))
#define SF_FIELD(member) ::SF::Reflection::MakeField(#member, &RTTI_ThisType::member)

#define SF_FIELDS(...)                                                                                                 \
public:                                                                                                                \
    static constexpr auto RTTI_Fields() { return ::SF::Reflection::MakeFields(__VA_ARGS__); }
