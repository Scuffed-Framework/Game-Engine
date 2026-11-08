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
    using namespace std;

    struct DragAndDropObjectTag
    {
    };

    template<typename T>
    constexpr bool has_dnd_tag_v = is_base_of_v<DragAndDropObjectTag, T>;

    template<typename T>
    struct is_optional : false_type
    {
    };

    template<typename T>
    struct is_optional<optional<T>> : true_type
    {
    };

    template<typename T>
    inline constexpr bool is_optional_v = is_optional<T>::value;

    template<typename T>
    struct is_pair : false_type
    {
    };

    template<typename T, typename U>
    struct is_pair<pair<T, U>> : true_type
    {
    };

    template<typename T>
    inline constexpr bool is_pair_v = is_pair<T>::value;

    template<typename T>
    struct is_vector : false_type
    {
    };

    template<typename T, typename A>
    struct is_vector<vector<T, A>> : true_type
    {
    };

    template<typename T>
    inline constexpr bool is_vector_v = is_vector<T>::value;

    template<typename T, typename U = void>
    struct is_map : false_type
    {
    };

    template<typename T>
    struct is_map<T, void_t<typename T::key_type, typename T::mapped_type,
                            decltype(declval<T &>()[declval<const typename T::key_type &>()])>> : true_type
    {
    };

    template<ranges::input_range R1, ranges::input_range R2, typename Compare = ranges::less>
    constexpr bool LexicographicalCompare(R1 &&a, R2 &&b, Compare comp = {})
    {
        auto it1 = ranges::begin(a);
        auto it2 = ranges::begin(b);

        const auto end1 = ranges::end(a);
        const auto end2 = ranges::end(b);

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
    struct is_unique_ptr : false_type
    {
    };

    template<typename T, typename D>
    struct is_unique_ptr<unique_ptr<T, D>> : true_type
    {
    };

    template<typename T>
    inline constexpr bool is_unique_ptr_v = is_unique_ptr<T>::value;

    template<typename T>
    struct is_shared_ptr : false_type
    {
    };

    template<typename T>
    struct is_shared_ptr<shared_ptr<T>> : true_type
    {
    };

    template<typename T>
    inline constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

    template<typename T>
    struct is_weak_ptr : false_type
    {
    };

    template<typename T>
    struct is_weak_ptr<weak_ptr<T>> : true_type
    {
    };

    template<typename T>
    inline constexpr bool is_weak_ptr_v = is_weak_ptr<T>::value;

    template<typename T>
    inline constexpr bool is_ptr_access_v =
            is_pointer_v<T> || is_unique_ptr_v<T> || is_shared_ptr_v<T> || is_weak_ptr_v<T>;

    template<typename T>
    struct is_smart_pointer : false_type
    {
    };
    template<typename T>
    struct is_smart_pointer<unique_ptr<T>> : true_type
    {
    };
    template<typename T>
    struct is_smart_pointer<shared_ptr<T>> : true_type
    {
    };
    template<typename T>
    struct is_smart_pointer<weak_ptr<T>> : true_type
    {
    };
    template<typename T>
    constexpr bool is_smart_pointer_v = is_smart_pointer<remove_cvref_t<T>>::value;

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
    static T *to_address(const shared_ptr<T> &obj) noexcept
    {
        return obj.get();
    }

    template<typename T>
    static T *to_address(const unique_ptr<T> &obj) noexcept
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
    static const T &to_reference(const shared_ptr<T> &obj) noexcept
    {
        return *obj.get();
    }

    template<typename T>
    static const T &to_reference(const unique_ptr<T> &obj) noexcept
    {
        return *obj.get();
    }

    static constexpr string ToHex(uint32_t value)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        string s;
        do
        {
            s.insert(s.begin(), digits[value & 0xF]);
            value >>= 4;
        } while (value != 0);
        return s;
    }

    constexpr string ToLower(const string_view s)
    {
        string out(s);
        ranges::transform(out, out.begin(), [](const unsigned char c) { return static_cast<char>(tolower(c)); });
        return out;
    }

    constexpr bool ContainsNoCase(string_view haystack, string_view needle)
    {
        if (needle.empty())
            return true;
        const auto it =
                ranges::search(haystack, needle, [](const char h, const char n)
                               { return tolower(static_cast<unsigned char>(h)) == static_cast<unsigned char>(n); })
                        .begin();
        return it != haystack.end();
    }

    constexpr bool LessNoCase(const string &a, const string &b)
    {
        return LexicographicalCompare(a, b, [](const unsigned char x, const unsigned char y)
                                      { return tolower(x) < tolower(y); });
    }

    constexpr string CleanFileName(string s)
    {
        for (char &c: s)
            if (strchr("<>:\"/\\|?*", c) || static_cast<unsigned char>(c) < 32)
                c = '_';
        s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1); // trailing whitespace (npos + 1 == 0 -> clears all-blank)
        return s;
    }

    inline string SafeStr(const char *s) { return s ? string(s) : string(); }
} // namespace SF::Engine
