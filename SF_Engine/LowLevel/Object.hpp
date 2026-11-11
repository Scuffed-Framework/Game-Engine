#ifndef OBJECT_HPP
#define OBJECT_HPP

#include <UtilityClasses/UUID.hpp>

namespace SF::Engine
{
    class Object
    {
    public:
        Object() : uuid(UUID::Generate()) {}

        Object(const Object &other) : uuid(UUID::Generate()) {}

        Object(Object &&other) noexcept : uuid(std::move(other.uuid)) {}

        Object &operator=(const Object &other) { return *this; }

        Object &operator=(Object &&other) noexcept
        {
            if (this != &other)
            {
                uuid = other.uuid;
            }
            return *this;
        }

        virtual ~Object() = default;

        [[nodiscard]] const UUID &GetUUID() const { return uuid; }

        [[nodiscard]] std::string GetUUIDString() const { return uuid.ToString(); }

        [[nodiscard]] bool IsNull() const { return uuid.IsNull(); }

        bool operator==(const Object &other) const { return uuid == other.uuid; }
        bool operator!=(const Object &other) const { return uuid != other.uuid; }
        bool operator<(const Object &other) const { return uuid < other.uuid; }

        struct UUIDHash
        {
            size_t operator()(const Object &obj) const { return UUID::Hash()(obj.uuid); }
        };

    protected:
        UUID uuid;
    };
} // namespace SF::Engine

#endif // OBJECT_HPP
