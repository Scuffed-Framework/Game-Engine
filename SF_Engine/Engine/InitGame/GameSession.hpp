#pragma once
#include <Device/MicrophoneController.hpp>
#include <UtilityClasses/UUID.hpp>
#include <cstdint>
#include <string>

namespace SF::Engine
{
    struct GameSession
    {
        uint32_t maxSpectators;
        uint32_t maxPlayers;
        uint32_t maxPartySize;
        uint32_t maxSplitscreenPlayers = 4;

        MicrophoneRecordMode micMode = MicrophoneRecordMode::PushToTalk;
        std::wstring sessionName;

        UUID GetNextPlayerID();

        virtual bool ProcessAutoLogin();
        virtual void OnLoginComplete(const UUID &playerID, bool success, const std::string &error);
        virtual std::string ApproveLogin(const std::string options);

        virtual void RegisterPlayer(const UUID &playerID, bool fromInvitation);
    };
}