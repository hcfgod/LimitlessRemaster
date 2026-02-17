#pragma once

#include "Assets/Asset.h"
#include "Core/Concurrency/AsyncIO.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace Limitless::Assets
{
    class AnimatorControllerAsset final : public Limitless::Asset
    {
    public:
        using Ptr = std::shared_ptr<AnimatorControllerAsset>;

        enum class ParameterType : uint8_t
        {
            Bool = 0,
            Float = 1,
            Integer = 2,
            Trigger = 3
        };

        enum class ConditionMode : uint8_t
        {
            If = 0,
            IfNot = 1,
            Greater = 2,
            Less = 3,
            Equals = 4,
            NotEquals = 5,
            Triggered = 6
        };

        struct ParameterDefinition final
        {
            std::string Name;
            ParameterType Type = ParameterType::Bool;
            bool DefaultBool = false;
            float DefaultFloat = 0.0f;
            int32_t DefaultInteger = 0;
        };

        struct TransitionCondition final
        {
            std::string ParameterName;
            ConditionMode Mode = ConditionMode::If;
            bool BoolValue = false;
            float FloatThreshold = 0.0f;
            int32_t IntegerThreshold = 0;
        };

        struct TransitionDefinition final
        {
            std::string ToState;
            bool HasExitTime = false;
            float ExitTimeNormalized = 1.0f;
            float DurationSeconds = 0.1f;
            bool CanTransitionToSelf = false;
            std::vector<TransitionCondition> Conditions;
        };

        struct StateDefinition final
        {
            std::string Name;
            std::string ClipKey;
            float SpeedMultiplier = 1.0f;
            bool LoopOverrideEnabled = false;
            bool LoopOverride = true;
            std::vector<TransitionDefinition> Transitions;
        };

        struct Data final
        {
            std::string Name;
            std::string DefaultStateName;
            std::vector<ParameterDefinition> Parameters;
            std::vector<StateDefinition> States;
        };

        struct Settings
        {
            bool ValidateStrictly = false;
        };

        static Async::Task<Ptr> LoadAsync(const std::string& key, Settings settings = {});
        static Ptr LoadBlocking(const std::string& key, Settings settings = {});

        bool Reload() override;

        const Data& GetData() const { return m_Data; }
        uint64_t GetRevision() const { return m_Revision.load(std::memory_order_relaxed); }

    private:
        AnimatorControllerAsset(std::string key, std::string guid, Data data, Settings settings)
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

