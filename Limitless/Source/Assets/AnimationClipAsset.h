#pragma once

#include "Assets/Asset.h"
#include "Core/Concurrency/AsyncIO.h"

#include <glm/glm.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace Limitless::Assets
{
    class AnimationClipAsset final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<AnimationClipAsset>;

        enum class InterpolationMode : uint8_t
        {
            Step = 0,
            Linear = 1
        };

        struct SpriteSubRectKeyframe final
        {
            float TimeSeconds = 0.0f;
            glm::vec2 UvMin = glm::vec2(0.0f, 0.0f);
            glm::vec2 UvMax = glm::vec2(1.0f, 1.0f);
        };

        struct SpriteTextureKeyframe final
        {
            float TimeSeconds = 0.0f;
            std::string TextureKey;
        };

        struct Vector3Keyframe final
        {
            float TimeSeconds = 0.0f;
            glm::vec3 Value = glm::vec3(0.0f);
            InterpolationMode Interpolation = InterpolationMode::Linear;
        };

        struct FloatKeyframe final
        {
            float TimeSeconds = 0.0f;
            float Value = 0.0f;
            InterpolationMode Interpolation = InterpolationMode::Linear;
        };

        struct EventKeyframe final
        {
            float TimeSeconds = 0.0f;
            std::string Name;
            std::string StringPayload;
            float FloatPayload = 0.0f;
            int32_t IntegerPayload = 0;
            bool BooleanPayload = false;
        };

        struct Data final
        {
            std::string Name;
            bool Loop = true;
            float DurationSeconds = 1.0f;
            float SamplesPerSecond = 30.0f;
            std::vector<SpriteSubRectKeyframe> SpriteSubRectTrack;
            std::vector<SpriteTextureKeyframe> SpriteTextureTrack;
            std::vector<Vector3Keyframe> PositionTrack;
            std::vector<Vector3Keyframe> ScaleTrack;
            std::vector<FloatKeyframe> RotationZTrack;
            std::vector<EventKeyframe> EventTrack;
        };

        struct Settings
        {
            bool ValidateStrictly = false;
        };

        // NOTE(macOS/Linux Clang/GCC):
        // Avoid default arguments like `Settings settings = {}` for nested settings types
        // that rely on default member initializers. Some toolchains reject this pattern
        // while the enclosing class definition is still being parsed.
        // Use overloads for portable defaults.
        static Async::Task<Ptr> LoadAsync(const std::string& key);
        static Async::Task<Ptr> LoadAsync(const std::string& key, Settings settings);
        static Ptr LoadBlocking(const std::string& key);
        static Ptr LoadBlocking(const std::string& key, Settings settings);

        bool Reload() override;

        const Data& GetData() const { return m_Data; }
        uint64_t GetRevision() const { return m_Revision.load(std::memory_order_relaxed); }

    private:
        AnimationClipAsset(std::string key, std::string guid, Data data, Settings settings)
            : Asset(std::move(key), std::move(guid)),
              m_Data(std::move(data)),
              m_Settings(std::move(settings))
        {
        }

    private:
        std::string m_ResolvedPath;
        Data m_Data{};
        Settings m_Settings{};
        std::atomic<uint64_t> m_Revision{0};
    };
}

