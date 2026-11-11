#pragma once
#include <UtilityClasses/UUID.hpp>
#include <string>

namespace SF::Engine
{

    class PlayerBase
    {
    public:
        virtual ~PlayerBase() = default;
        std::string Name;
        UUID PlayerID;

        // internet (lil scripty might lag the server :sob:)
        int32_t CurrentNetSpeed;
        int32_t ConfiguredInternetSpeed;
        int32_t ConfiguredLanSpeed;

        virtual void SwitchPlayerController();
    };

    template<typename Player>
    constexpr bool IsDerivedOfPlayerBase = std::is_base_of_v<PlayerBase, Player> && std::is_same_v<Player, PlayerBase>;
} // namespace SF::Engine
