#pragma once

#include <EntityComponentSystem/Component.hpp>
#include <EntityComponentSystem/Entity.hpp>
#include <Math/Transform.hpp>
#include <Math/Vector.hpp>
#include <memory>
#include "Audio.hpp"
#include "SoundBuffer.hpp"
#include "Waves.hpp"

namespace SF::Engine
{
    // TODO: ADD A WAY TO CREATE FROM MEMORY OR FROM THE STUFF IN WAVES.HPP
    /**
     * @brief Component that represents a playable AudioClip.
     */
    class AudioClip : public Component::Registrar<AudioClip>
    {
        static bool Reflect()
        {
            Reflection::SerializeContext::Instance()
                    .Class<AudioClip>()
                    ->Version(1)
                    ->Field("Position", &AudioClip::position)
                    ->Speed(0.05f)
                    ->ResetTo(0, 0, 0)
                    ->Field("Direction", &AudioClip::direction)
                    ->Speed(0.05f)
                    ->ResetTo(0, 0, 0)
                    ->Field("Velocity", &AudioClip::velocity)
                    ->Speed(0.05f)
                    ->ResetTo(0, 0, 0)
                    ->Speed(0.05f)
                    ->Field("Type", &AudioClip::type)
                    ->Field("Gain", &AudioClip::gain)
                    ->Field("Pitch", &AudioClip::pitch);
            return true;
        }
        inline static const bool Registered = Register("audioClip");

    public:
        AudioClip() = default;
        explicit AudioClip(const DataInput &input, const Audio::Type &type = Audio::Type::General, bool begin = false,
                           bool loop = false, float gain = 1.0f, float pitch = 1.0f);

        explicit AudioClip(std::shared_ptr<SoundBuffer> buffer, const Audio::Type &type = Audio::Type::General,
                           bool begin = false, bool loop = false, float gain = 1.0f, float pitch = 1.0f);

        AudioClip(const AudioClip &)            = delete;
        AudioClip &operator=(const AudioClip &) = delete;
        AudioClip(AudioClip &&other) noexcept;
        AudioClip &operator=(AudioClip &&other) noexcept;

        ~AudioClip() override;

        void Play(bool loop = false);
        void Pause() const;
        void Resume();
        void Stop() const;

        [[nodiscard]] bool IsPlaying() const;

        void SetPosition(const Vec3 &position);
        void SetDirection(const Vec3 &direction);
        void SetVelocity(const Vec3 &velocity);

        [[nodiscard]] const Audio::Type &GetType() const { return type; }
        void SetType(const Audio::Type &type) { this->type = type; }

        [[nodiscard]] float GetGain() const { return gain; }
        void SetGain(float gain);

        [[nodiscard]] float GetPitch() const { return pitch; }
        void SetPitch(float pitch);

        std::shared_ptr<SoundBuffer> buffer;
        std::uint32_t source = 0;

        Vec3 position{};
        Vec3 direction{};
        Vec3 velocity{};

        Audio::Type type = Audio::Type::General;
        float gain       = 1.0f;
        float pitch      = 1.0f;
        float length{};

        void Serialize(XMLNode &node) const override
        {
            Component::Serialize(node);
            auto clip = node.AddChild("Audioclip");
            SerializeVec3(clip, "position", position);
            SerializeVec3(clip, "direction", direction);
            SerializeVec3(clip, "velocity", velocity);
            clip.SetAttribute("gain", gain);
            clip.SetAttribute("pitch", pitch);
        }

        void Deserialize(const XMLNode &node) override
        {
            Component::Deserialize(node);
            const auto clip = node.GetChild("Audioclip");
            DeserializeVec3(clip, "position", position);
            DeserializeVec3(clip, "direction", direction);
            DeserializeVec3(clip, "velocity", velocity);
            clip.GetAttribute("gain", gain);
            clip.GetAttribute("pitch", pitch);
        }
    };

    class AudioClipSystem
    {
    public:
        static void Update(EntityRegistry &registry)
        {
            registry.ForEach(
                    [](Entity *entity)
                    {
                        auto *clip      = entity->GetComponent<AudioClip>();
                        auto *transform = entity->GetComponent<Transform>();
                        if (clip && transform)
                        {
                            clip->SetPosition(transform->GetPosition());
                        }
                    });
        }

        static void UpdateVolumes(EntityRegistry &registry)
        {
            registry.ForEach(
                    [](Entity *entity)
                    {
                        if (auto *clip = entity->GetComponent<AudioClip>())
                        {
                            clip->SetGain(clip->GetGain()); // recompute with current Audio volume
                        }
                    });
        }
    };
} // namespace SF::Engine
