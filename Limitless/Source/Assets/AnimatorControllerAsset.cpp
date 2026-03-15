#include "Assets/AnimatorControllerAsset.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetDatabase.h"
#include "Assets/GeneratedAssetRuntimeRegistry.h"
#include "Assets/AssetLoadCoordinator.h"
#include "Assets/AssetLoadProgress.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace Limitless::Assets
{
    namespace
    {
        using json = nlohmann::json;

        std::string ResolveKeyFromReferenceJson(const json& value)
        {
            if (value.is_string())
                return value.get<std::string>();
            if (!value.is_object())
                return {};

            if (value.contains("guid") && value["guid"].is_string())
            {
                const std::string guid = value["guid"].get<std::string>();
                if (!guid.empty())
                {
                    const auto record = AssetDatabase::GetInstance().FindByGuid(guid);
                    if (record.IsSuccess() && !record.GetValue().Key.empty())
                        return record.GetValue().Key;
                }
            }

            if (value.contains("key") && value["key"].is_string())
                return value["key"].get<std::string>();

            return {};
        }

        const char* ToParameterTypeName(AnimatorControllerAsset::ParameterType type)
        {
            switch (type)
            {
                case AnimatorControllerAsset::ParameterType::Bool: return "Bool";
                case AnimatorControllerAsset::ParameterType::Float: return "Float";
                case AnimatorControllerAsset::ParameterType::Integer: return "Integer";
                case AnimatorControllerAsset::ParameterType::Trigger: return "Trigger";
                default: return "Bool";
            }
        }

        AnimatorControllerAsset::ParameterType ParseParameterTypeName(const std::string& typeName)
        {
            if (typeName == "Float")
                return AnimatorControllerAsset::ParameterType::Float;
            if (typeName == "Integer")
                return AnimatorControllerAsset::ParameterType::Integer;
            if (typeName == "Trigger")
                return AnimatorControllerAsset::ParameterType::Trigger;
            return AnimatorControllerAsset::ParameterType::Bool;
        }

        const char* ToConditionModeName(AnimatorControllerAsset::ConditionMode mode)
        {
            switch (mode)
            {
                case AnimatorControllerAsset::ConditionMode::If: return "If";
                case AnimatorControllerAsset::ConditionMode::IfNot: return "IfNot";
                case AnimatorControllerAsset::ConditionMode::Greater: return "Greater";
                case AnimatorControllerAsset::ConditionMode::Less: return "Less";
                case AnimatorControllerAsset::ConditionMode::Equals: return "Equals";
                case AnimatorControllerAsset::ConditionMode::NotEquals: return "NotEquals";
                case AnimatorControllerAsset::ConditionMode::Triggered: return "Triggered";
                default: return "If";
            }
        }

        AnimatorControllerAsset::ConditionMode ParseConditionModeName(const std::string& modeName)
        {
            if (modeName == "IfNot")
                return AnimatorControllerAsset::ConditionMode::IfNot;
            if (modeName == "Greater")
                return AnimatorControllerAsset::ConditionMode::Greater;
            if (modeName == "Less")
                return AnimatorControllerAsset::ConditionMode::Less;
            if (modeName == "Equals")
                return AnimatorControllerAsset::ConditionMode::Equals;
            if (modeName == "NotEquals")
                return AnimatorControllerAsset::ConditionMode::NotEquals;
            if (modeName == "Triggered")
                return AnimatorControllerAsset::ConditionMode::Triggered;
            return AnimatorControllerAsset::ConditionMode::If;
        }

        void SortAndNormalize(AnimatorControllerAsset::Data& data)
        {
            for (auto& state : data.States)
            {
                state.SpeedMultiplier = std::max(0.0f, state.SpeedMultiplier);
                for (auto& transition : state.Transitions)
                {
                    transition.ExitTimeNormalized = std::clamp(transition.ExitTimeNormalized, 0.0f, 1.0f);
                    transition.DurationSeconds = std::max(0.0f, transition.DurationSeconds);
                }
            }

            if (data.DefaultStateName.empty() && !data.States.empty())
                data.DefaultStateName = data.States.front().Name;
        }

        Result<AnimatorControllerAsset::Data> ParseControllerJsonText(const std::string& jsonText, const std::string& debugSourcePath)
        {
            json root = json::object();
            try
            {
                root = json::parse(jsonText);
            }
            catch (const std::exception& exception)
            {
                return Result<AnimatorControllerAsset::Data>(
                    ErrorCode::FileCorrupted,
                    "AnimatorControllerAsset parse failed for '" + debugSourcePath + "': " + exception.what());
            }

            if (!root.is_object())
            {
                return Result<AnimatorControllerAsset::Data>(
                    ErrorCode::FileCorrupted,
                    "AnimatorControllerAsset JSON root must be an object: " + debugSourcePath);
            }

            AnimatorControllerAsset::Data data{};
            data.Name = root.value("Name", std::string{});
            data.DefaultStateName = root.value("DefaultStateName", std::string{});

            if (root.contains("Parameters") && root["Parameters"].is_array())
            {
                for (const auto& parameterJson : root["Parameters"])
                {
                    if (!parameterJson.is_object())
                        continue;
                    AnimatorControllerAsset::ParameterDefinition definition{};
                    definition.Name = parameterJson.value("Name", std::string{});
                    definition.Type = ParseParameterTypeName(parameterJson.value("Type", std::string("Bool")));
                    definition.DefaultBool = parameterJson.value("DefaultBool", false);
                    definition.DefaultFloat = parameterJson.value("DefaultFloat", 0.0f);
                    definition.DefaultInteger = parameterJson.value("DefaultInteger", 0);
                    if (!definition.Name.empty())
                        data.Parameters.push_back(std::move(definition));
                }
            }

            if (root.contains("States") && root["States"].is_array())
            {
                for (const auto& stateJson : root["States"])
                {
                    if (!stateJson.is_object())
                        continue;

                    AnimatorControllerAsset::StateDefinition state{};
                    state.Name = stateJson.value("Name", std::string{});
                    if (stateJson.contains("Clip"))
                        state.ClipKey = ResolveKeyFromReferenceJson(stateJson["Clip"]);
                    else
                        state.ClipKey = stateJson.value("ClipKey", std::string{});
                    state.SpeedMultiplier = stateJson.value("SpeedMultiplier", 1.0f);
                    state.LoopOverrideEnabled = stateJson.value("LoopOverrideEnabled", false);
                    state.LoopOverride = stateJson.value("LoopOverride", true);

                    if (stateJson.contains("Transitions") && stateJson["Transitions"].is_array())
                    {
                        for (const auto& transitionJson : stateJson["Transitions"])
                        {
                            if (!transitionJson.is_object())
                                continue;
                            AnimatorControllerAsset::TransitionDefinition transition{};
                            transition.ToState = transitionJson.value("ToState", std::string{});
                            transition.HasExitTime = transitionJson.value("HasExitTime", false);
                            transition.ExitTimeNormalized = transitionJson.value("ExitTimeNormalized", 1.0f);
                            transition.DurationSeconds = transitionJson.value("DurationSeconds", 0.1f);
                            transition.CanTransitionToSelf = transitionJson.value("CanTransitionToSelf", false);

                            if (transitionJson.contains("Conditions") && transitionJson["Conditions"].is_array())
                            {
                                for (const auto& conditionJson : transitionJson["Conditions"])
                                {
                                    if (!conditionJson.is_object())
                                        continue;
                                    AnimatorControllerAsset::TransitionCondition condition{};
                                    condition.ParameterName = conditionJson.value("ParameterName", std::string{});
                                    condition.Mode = ParseConditionModeName(conditionJson.value("Mode", std::string("If")));
                                    condition.BoolValue = conditionJson.value("BoolValue", false);
                                    condition.FloatThreshold = conditionJson.value("FloatThreshold", 0.0f);
                                    condition.IntegerThreshold = conditionJson.value("IntegerThreshold", 0);
                                    if (!condition.ParameterName.empty())
                                        transition.Conditions.push_back(std::move(condition));
                                }
                            }

                            if (!transition.ToState.empty())
                                state.Transitions.push_back(std::move(transition));
                        }
                    }

                    if (!state.Name.empty())
                        data.States.push_back(std::move(state));
                }
            }

            SortAndNormalize(data);
            return data;
        }

        Result<AnimatorControllerAsset::Data> LoadControllerDataFromKey(const std::string& key,
                                                                        std::string& outResolvedPath,
                                                                        std::string& outGuid)
        {
            std::string jsonText;
            bool loadedFromBundle = false;
            if (const auto generatedRecordResult = FindGeneratedAssetRecord(key, AssetType::AnimatorController); generatedRecordResult.IsSuccess())
            {
                const auto textResult = GeneratedAssetRuntimeRegistry::GetInstance().LoadText(key);
                if (textResult.IsFailure())
                {
                    return Result<AnimatorControllerAsset::Data>(
                        textResult.GetError().GetCode(),
                        "AnimatorControllerAsset::Load failed reading generated payload for '" + key + "': " + textResult.GetError().GetErrorMessage());
                }

                outGuid = generatedRecordResult.GetValue().Guid;
                outResolvedPath = "<Generated>";
                jsonText = textResult.GetValue();
                return ParseControllerJsonText(jsonText, outResolvedPath);
            }

            auto& bundle = AssetBundle::GetInstance();

            if (bundle.IsEnabled() && bundle.IsLoaded())
            {
                if (const auto entry = bundle.FindEntryByKey(key); entry.has_value())
                {
                    outGuid = entry->Guid;
                    const auto bundleTextResult = bundle.ReadAllTextByKey(key);
                    if (bundleTextResult.IsSuccess())
                    {
                        jsonText = bundleTextResult.GetValue();
                        outResolvedPath = "<AssetBundle>";
                        loadedFromBundle = true;
                    }
                }
            }

            if (!loadedFromBundle)
            {
                const auto resolvedResult = ResolveAssetKeyToPath(key);
                if (resolvedResult.IsFailure())
                {
                    return Result<AnimatorControllerAsset::Data>(
                        resolvedResult.GetError().GetCode(),
                        "AnimatorControllerAsset::Load failed resolving '" + key + "': " + resolvedResult.GetError().GetErrorMessage());
                }
                outResolvedPath = resolvedResult.GetValue().string();
                std::ifstream input(outResolvedPath, std::ios::in | std::ios::binary);
                if (!input.is_open())
                {
                    return Result<AnimatorControllerAsset::Data>(
                        ErrorCode::FileNotFound,
                        "AnimatorControllerAsset::Load failed opening file '" + outResolvedPath + "'");
                }
                jsonText.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            }

            if (outGuid.empty())
            {
                const auto guidResult = LoadOrCreateGuid(outResolvedPath, {{"key", key}, {"type", "AnimatorController"}});
                if (guidResult.IsFailure())
                {
                    return Result<AnimatorControllerAsset::Data>(
                        guidResult.GetError().GetCode(),
                        "AnimatorControllerAsset::Load failed obtaining GUID for '" + outResolvedPath + "': " + guidResult.GetError().GetErrorMessage());
                }
                outGuid = guidResult.GetValue();
            }

            return ParseControllerJsonText(jsonText, outResolvedPath);
        }

        std::vector<std::string> BuildDependencyGuids(const AnimatorControllerAsset::Data& data)
        {
            std::vector<std::string> result;
            std::unordered_set<std::string> uniqueGuids;
            for (const auto& state : data.States)
            {
                if (state.ClipKey.empty())
                    continue;
                const auto record = AssetDatabase::GetInstance().FindByKey(state.ClipKey);
                if (!record.IsSuccess() || record.GetValue().Guid.empty())
                    continue;
                if (uniqueGuids.insert(record.GetValue().Guid).second)
                    result.push_back(record.GetValue().Guid);
            }
            return result;
        }
    }

    Async::Task<AnimatorControllerAsset::Ptr> AnimatorControllerAsset::LoadAsync(const std::string& key)
    {
        return LoadAsync(key, Settings{});
    }

    Async::Task<AnimatorControllerAsset::Ptr> AnimatorControllerAsset::LoadAsync(const std::string& key, Settings settings)
    {
        const uint64_t generation = AssetLoadCoordinator::GetGeneration();
        std::promise<Ptr> promise;
        std::shared_future<Ptr> shared = promise.get_future().share();

        Async::GetAsyncIO().RunAsync([key, settings, generation, promise = std::move(promise)]() mutable {
            if (!AssetLoadCoordinator::IsGenerationCurrent(generation))
            {
                promise.set_value(nullptr);
                return;
            }

            const auto generatedRecordResult = FindGeneratedAssetRecord(key, AssetType::AnimatorController);
            if (generatedRecordResult.IsSuccess())
            {
                if (auto existing = GeneratedAssetRuntimeRegistry::GetInstance().GetAsset<AnimatorControllerAsset>(key))
                {
                    auto cached = AssetManager::GetOrLoad<AnimatorControllerAsset>(key, [&]() -> Ptr { return existing; });
                    promise.set_value(std::move(cached));
                    return;
                }
            }

            AssetLoadProgress::SetProgress(key, 0.1f, "Loading animator controller...");
            std::string resolvedPath;
            std::string guid;
            const auto dataResult = LoadControllerDataFromKey(key, resolvedPath, guid);
            if (dataResult.IsFailure())
            {
                AssetLoadProgress::ClearProgress(key);
                LT_CORE_ERROR("{}", dataResult.GetError().GetErrorMessage());
                promise.set_value(nullptr);
                return;
            }

            AssetLoadProgress::SetProgress(key, 0.8f, "Finalizing...");
            auto asset = AssetManager::GetOrLoad<AnimatorControllerAsset>(key, [&]() -> Ptr {
                Ptr created(new AnimatorControllerAsset(key, guid, dataResult.GetValue(), settings));
                created->m_ResolvedPath = resolvedPath;
                created->m_Revision.fetch_add(1, std::memory_order_relaxed);
                (void)AssetDatabase::GetInstance().SetDependencies(created->GetGuid(), BuildDependencyGuids(created->m_Data));
                return created;
            });
            if (generatedRecordResult.IsSuccess() && asset)
                GeneratedAssetRuntimeRegistry::GetInstance().RegisterAsset(key, asset);

            AssetLoadProgress::ClearProgress(key);
            promise.set_value(std::move(asset));
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    AnimatorControllerAsset::Ptr AnimatorControllerAsset::LoadBlocking(const std::string& key)
    {
        return LoadBlocking(key, Settings{});
    }

    AnimatorControllerAsset::Ptr AnimatorControllerAsset::LoadBlocking(const std::string& key, Settings settings)
    {
        auto task = LoadAsync(key, std::move(settings));
        task.Wait();
        return task.Get();
    }

    bool AnimatorControllerAsset::Reload()
    {
        std::string resolvedPath;
        std::string guid;
        const auto dataResult = LoadControllerDataFromKey(GetKey(), resolvedPath, guid);
        if (dataResult.IsFailure())
        {
            LT_CORE_ERROR("AnimatorControllerAsset::Reload failed: {}", dataResult.GetError().GetErrorMessage());
            return false;
        }

        m_ResolvedPath = std::move(resolvedPath);
        m_Data = dataResult.GetValue();
        SortAndNormalize(m_Data);
        m_Revision.fetch_add(1, std::memory_order_relaxed);
        (void)AssetDatabase::GetInstance().SetDependencies(GetGuid(), BuildDependencyGuids(m_Data));
        return true;
    }
}

