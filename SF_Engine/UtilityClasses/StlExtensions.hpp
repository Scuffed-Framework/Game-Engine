#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace SF::Engine
{
    static constexpr std::uint32_t InvalidIndex = UINT32_MAX;

    struct DragAndDropObjectTag
    {
    };

    template<typename T>
    constexpr bool has_dnd_tag_v = std::is_base_of_v<DragAndDropObjectTag, T>;

    template<typename T>
    struct is_optional : std::false_type
    {
    };

    template<typename T>
    struct is_optional<std::optional<T>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_optional_v = is_optional<T>::value;

    template<typename T>
    struct is_pair : std::false_type
    {
    };

    template<typename T, typename U>
    struct is_pair<std::pair<T, U>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_pair_v = is_pair<T>::value;

    template<typename T>
    struct is_vector : std::false_type
    {
    };

    template<typename T, typename A>
    struct is_vector<std::vector<T, A>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_vector_v = is_vector<T>::value;

    template<typename T, typename U = void>
    struct is_map : std::false_type
    {
    };

    template<typename T>
    struct is_map<T, std::void_t<typename T::key_type, typename T::mapped_type,
                                 decltype(std::declval<T &>()[std::declval<const typename T::key_type &>()])>>
        : std::true_type
    {
    };

    template<std::ranges::input_range R1, std::ranges::input_range R2, typename Compare = std::ranges::less>
    constexpr bool LexicographicalCompare(R1 &&a, R2 &&b, Compare comp = {})
    {
        auto it1 = std::ranges::begin(a);
        auto it2 = std::ranges::begin(b);

        const auto end1 = std::ranges::end(a);
        const auto end2 = std::ranges::end(b);

        for (; it1 != end1 && it2 != end2; ++it1, ++it2)
        {
            if (comp(*it1, *it2))
                return true;

            if (comp(*it2, *it1))
                return false;
        }

        return it1 == end1 && it2 != end2;
    }

    template<typename T>
    inline constexpr bool is_map_v = is_map<T>::value;

    template<typename T>
    struct is_unique_ptr : std::false_type
    {
    };

    template<typename T, typename D>
    struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

    template<typename T>
    struct is_shared_ptr : std::false_type
    {
    };

    template<typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

    template<typename T>
    struct is_weak_ptr : std::false_type
    {
    };

    template<typename T>
    struct is_weak_ptr<std::weak_ptr<T>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_weak_ptr_v = is_weak_ptr<T>::value;

    template<typename T>
    inline constexpr bool is_ptr_access_v =
            std::is_pointer_v<T> || is_unique_ptr_v<T> || is_shared_ptr_v<T> || is_weak_ptr_v<T>;

    template<typename T>
    struct is_smart_pointer : std::false_type
    {
    };
    template<typename T>
    struct is_smart_pointer<std::unique_ptr<T>> : std::true_type
    {
    };
    template<typename T>
    struct is_smart_pointer<std::shared_ptr<T>> : std::true_type
    {
    };
    template<typename T>
    struct is_smart_pointer<std::weak_ptr<T>> : std::true_type
    {
    };
    template<typename T>
    constexpr bool is_smart_pointer_v = is_smart_pointer<std::remove_cvref_t<T>>::value;

    template<typename T>
    static T *to_address(T *obj) noexcept
    {
        return obj;
    }

    template<typename T>
    static T *to_address(T &obj) noexcept
    {
        return &obj;
    }

    template<typename T>
    static T *to_address(const std::shared_ptr<T> &obj) noexcept
    {
        return obj.get();
    }

    template<typename T>
    static T *to_address(const std::unique_ptr<T> &obj) noexcept
    {
        return obj.get();
    }

    template<typename T>
    static const T &to_reference(T &obj) noexcept
    {
        return obj;
    }

    template<typename T>
    static const T &to_reference(T *obj) noexcept
    {
        return *obj;
    }

    template<typename T>
    static const T &to_reference(const std::shared_ptr<T> &obj) noexcept
    {
        return *obj.get();
    }

    template<typename T>
    static const T &to_reference(const std::unique_ptr<T> &obj) noexcept
    {
        return *obj.get();
    }

    static constexpr std::string ToHex(uint32_t value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string s;
        do
        {
            s.insert(s.begin(), digits[value & 0xF]);
            value >>= 4;
        } while (value != 0);
        return s;
    }

    constexpr std::string ToLower(const std::string_view s)
    {
        std::string out(s);
        std::ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(tolower(c)); });
        return out;
    }

    constexpr bool ContainsNoCase(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it =
                std::ranges::search(haystack, needle, [](const char h, const char n)
                                    { return tolower(static_cast<unsigned char>(h)) == static_cast<unsigned char>(n); })
                        .begin();
        return it != haystack.end();
    }

    constexpr bool LessNoCase(const std::string &a, const std::string &b)
    {
        return LexicographicalCompare(a, b, [](const unsigned char x, const unsigned char y)
                                      { return tolower(x) < tolower(y); });
    }

    constexpr std::string CleanFileName(std::string s)
    {
        for (char &c: s)
            if (strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32)
                c = '_';
        s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1); // trailing whitespace (npos + 1 == 0 -> clears all-blank)
        return s;
    }

    inline std::string SafeStr(const char *s) { return s ? std::string(s) : std::string(); }

    template<typename T>
    std::vector<T> ParseNumericArrayFromStr(const std::string &data)
    {
        std::istringstream stream(data);

        return {std::istream_iterator<T>(stream), std::istream_iterator<T>()};
    }
} // namespace SF::Engine
