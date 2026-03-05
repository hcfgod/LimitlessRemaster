#include "Assets/AnimationClipAsset.h"

#include "Assets/AssetBundle.h"
#include "Assets/AssetDatabase.h"
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

        const char* ToInterpolationName(AnimationClipAsset::InterpolationMode interpolation)
        {
            return interpolation == AnimationClipAsset::InterpolationMode::Step ? "Step" : "Linear";
        }

        AnimationClipAsset::InterpolationMode ParseInterpolation(const json& value, AnimationClipAsset::InterpolationMode fallback)
        {
            if (!value.is_string())
                return fallback;

            const std::string mode = value.get<std::string>();
            if (mode == "Step")
                return AnimationClipAsset::InterpolationMode::Step;
            if (mode == "Linear")
                return AnimationClipAsset::InterpolationMode::Linear;
            return fallback;
        }

        std::vector<float> ReadFloatArray(const json& value, const std::vector<float>& fallback)
        {
            if (!value.is_array())
                return fallback;
            std::vector<float> result;
            result.reserve(value.size());
            for (const auto& element : value)
            {
                if (element.is_number())
                    result.push_back(element.get<float>());
            }
            return result.empty() ? fallback : result;
        }

        void SortAndClampTrackTimes(AnimationClipAsset::Data& data)
        {
            const auto clampTime = [&](float timeSeconds) {
                const float safeDuration = std::max(0.0001f, data.DurationSeconds);
                return std::clamp(timeSeconds, 0.0f, safeDuration);
            };

            for (auto& keyframe : data.SpriteSubRectTrack)
                keyframe.TimeSeconds = clampTime(keyframe.TimeSeconds);
            for (auto& keyframe : data.SpriteTextureTrack)
                keyframe.TimeSeconds = clampTime(keyframe.TimeSeconds);
            for (auto& keyframe : data.PositionTrack)
                keyframe.TimeSeconds = clampTime(keyframe.TimeSeconds);
            for (auto& keyframe : data.ScaleTrack)
                keyframe.TimeSeconds = clampTime(keyframe.TimeSeconds);
            for (auto& keyframe : data.RotationTrack)
                keyframe.TimeSeconds = clampTime(keyframe.TimeSeconds);
            for (auto& keyframe : data.EventTrack)
                keyframe.TimeSeconds = clampTime(keyframe.TimeSeconds);

            auto byTime = [](const auto& left, const auto& right) { return left.TimeSeconds < right.TimeSeconds; };
            std::sort(data.SpriteSubRectTrack.begin(), data.SpriteSubRectTrack.end(), byTime);
            std::sort(data.SpriteTextureTrack.begin(), data.SpriteTextureTrack.end(), byTime);
            std::sort(data.PositionTrack.begin(), data.PositionTrack.end(), byTime);
            std::sort(data.ScaleTrack.begin(), data.ScaleTrack.end(), byTime);
            std::sort(data.RotationTrack.begin(), data.RotationTrack.end(), byTime);
            std::sort(data.EventTrack.begin(), data.EventTrack.end(), byTime);
        }

        Result<AnimationClipAsset::Data> ParseClipJsonText(const std::string& jsonText, const std::string& debugSourcePath)
        {
            json root = json::object();
            try
            {
                root = json::parse(jsonText);
            }
            catch (const std::exception& exception)
            {
                return Result<AnimationClipAsset::Data>(
                    ErrorCode::FileCorrupted,
                    "AnimationClipAsset parse failed for '" + debugSourcePath + "': " + exception.what());
            }

            if (!root.is_object())
            {
                return Result<AnimationClipAsset::Data>(
                    ErrorCode::FileCorrupted,
                    "AnimationClipAsset JSON root must be an object: " + debugSourcePath);
            }

            AnimationClipAsset::Data data{};
            data.Name = root.value("Name", std::string{});
            data.Loop = root.value("Loop", true);
            data.DurationSeconds = std::max(0.0001f, root.value("DurationSeconds", 1.0f));
            data.SamplesPerSecond = std::max(1.0f, root.value("SamplesPerSecond", 30.0f));

            if (root.contains("SpriteSubRectTrack") && root["SpriteSubRectTrack"].is_array())
            {
                for (const auto& keyframeJson : root["SpriteSubRectTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;

                    AnimationClipAsset::SpriteSubRectKeyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    const std::vector<float> uvMin = ReadFloatArray(keyframeJson.value("UvMin", json::array()), {0.0f, 0.0f});
                    const std::vector<float> uvMax = ReadFloatArray(keyframeJson.value("UvMax", json::array()), {1.0f, 1.0f});
                    if (uvMin.size() >= 2)
                        keyframe.UvMin = glm::vec2(uvMin[0], uvMin[1]);
                    if (uvMax.size() >= 2)
                        keyframe.UvMax = glm::vec2(uvMax[0], uvMax[1]);
                    data.SpriteSubRectTrack.push_back(std::move(keyframe));
                }
            }

            if (root.contains("SpriteTextureTrack") && root["SpriteTextureTrack"].is_array())
            {
                for (const auto& keyframeJson : root["SpriteTextureTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;

                    AnimationClipAsset::SpriteTextureKeyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    if (keyframeJson.contains("Texture"))
                        keyframe.TextureKey = ResolveKeyFromReferenceJson(keyframeJson["Texture"]);
                    else
                        keyframe.TextureKey = keyframeJson.value("TextureKey", std::string{});
                    data.SpriteTextureTrack.push_back(std::move(keyframe));
                }
            }

            if (root.contains("PositionTrack") && root["PositionTrack"].is_array())
            {
                for (const auto& keyframeJson : root["PositionTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;
                    AnimationClipAsset::Vector3Keyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    keyframe.Interpolation = ParseInterpolation(
                        keyframeJson.value("Interpolation", json(ToInterpolationName(AnimationClipAsset::InterpolationMode::Linear))),
                        AnimationClipAsset::InterpolationMode::Linear);
                    const std::vector<float> value = ReadFloatArray(keyframeJson.value("Value", json::array()), {0.0f, 0.0f, 0.0f});
                    if (value.size() >= 3)
                        keyframe.Value = glm::vec3(value[0], value[1], value[2]);
                    data.PositionTrack.push_back(std::move(keyframe));
                }
            }

            if (root.contains("ScaleTrack") && root["ScaleTrack"].is_array())
            {
                for (const auto& keyframeJson : root["ScaleTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;
                    AnimationClipAsset::Vector3Keyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    keyframe.Interpolation = ParseInterpolation(
                        keyframeJson.value("Interpolation", json(ToInterpolationName(AnimationClipAsset::InterpolationMode::Linear))),
                        AnimationClipAsset::InterpolationMode::Linear);
                    const std::vector<float> value = ReadFloatArray(keyframeJson.value("Value", json::array()), {0.0f, 0.0f, 0.0f});
                    if (value.size() >= 3)
                        keyframe.Value = glm::vec3(value[0], value[1], value[2]);
                    data.ScaleTrack.push_back(std::move(keyframe));
                }
            }

            if (root.contains("RotationTrack") && root["RotationTrack"].is_array())
            {
                for (const auto& keyframeJson : root["RotationTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;
                    AnimationClipAsset::Vector3Keyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    keyframe.Interpolation = ParseInterpolation(
                        keyframeJson.value("Interpolation", json(ToInterpolationName(AnimationClipAsset::InterpolationMode::Linear))),
                        AnimationClipAsset::InterpolationMode::Linear);
                    const std::vector<float> value = ReadFloatArray(keyframeJson.value("Value", json::array()), {0.0f, 0.0f, 0.0f});
                    if (value.size() >= 3)
                        keyframe.Value = glm::vec3(value[0], value[1], value[2]);
                    data.RotationTrack.push_back(std::move(keyframe));
                }
            }
            else if (root.contains("RotationZTrack") && root["RotationZTrack"].is_array())
            {
                for (const auto& keyframeJson : root["RotationZTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;
                    AnimationClipAsset::Vector3Keyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    keyframe.Interpolation = ParseInterpolation(
                        keyframeJson.value("Interpolation", json(ToInterpolationName(AnimationClipAsset::InterpolationMode::Linear))),
                        AnimationClipAsset::InterpolationMode::Linear);
                    keyframe.Value = glm::vec3(0.0f, 0.0f, keyframeJson.value("Value", 0.0f));
                    data.RotationTrack.push_back(std::move(keyframe));
                }
            }

            if (root.contains("EventTrack") && root["EventTrack"].is_array())
            {
                for (const auto& keyframeJson : root["EventTrack"])
                {
                    if (!keyframeJson.is_object())
                        continue;
                    AnimationClipAsset::EventKeyframe keyframe{};
                    keyframe.TimeSeconds = keyframeJson.value("TimeSeconds", 0.0f);
                    keyframe.Name = keyframeJson.value("Name", std::string{});
                    keyframe.StringPayload = keyframeJson.value("StringPayload", std::string{});
                    keyframe.FloatPayload = keyframeJson.value("FloatPayload", 0.0f);
                    keyframe.IntegerPayload = keyframeJson.value("IntegerPayload", 0);
                    keyframe.BooleanPayload = keyframeJson.value("BooleanPayload", false);
                    if (!keyframe.Name.empty())
                        data.EventTrack.push_back(std::move(keyframe));
                }
            }

            SortAndClampTrackTimes(data);
            return data;
        }

        std::string SerializeDataToJsonText(const AnimationClipAsset::Data& data)
        {
            json root = json::object();
            root["Version"] = 1;
            root["Name"] = data.Name;
            root["Loop"] = data.Loop;
            root["DurationSeconds"] = std::max(0.0001f, data.DurationSeconds);
            root["SamplesPerSecond"] = std::max(1.0f, data.SamplesPerSecond);

            root["SpriteSubRectTrack"] = json::array();
            for (const auto& keyframe : data.SpriteSubRectTrack)
            {
                root["SpriteSubRectTrack"].push_back({
                    {"TimeSeconds", keyframe.TimeSeconds},
                    {"UvMin", {keyframe.UvMin.x, keyframe.UvMin.y}},
                    {"UvMax", {keyframe.UvMax.x, keyframe.UvMax.y}}
                });
            }

            root["SpriteTextureTrack"] = json::array();
            for (const auto& keyframe : data.SpriteTextureTrack)
            {
                root["SpriteTextureTrack"].push_back({
                    {"TimeSeconds", keyframe.TimeSeconds},
                    {"Texture", {{"key", keyframe.TextureKey}}}
                });
            }

            auto serializeVectorTrack = [](const std::vector<AnimationClipAsset::Vector3Keyframe>& track) {
                json result = json::array();
                for (const auto& keyframe : track)
                {
                    result.push_back({
                        {"TimeSeconds", keyframe.TimeSeconds},
                        {"Value", {keyframe.Value.x, keyframe.Value.y, keyframe.Value.z}},
                        {"Interpolation", ToInterpolationName(keyframe.Interpolation)}
                    });
                }
                return result;
            };

            root["PositionTrack"] = serializeVectorTrack(data.PositionTrack);
            root["ScaleTrack"] = serializeVectorTrack(data.ScaleTrack);

            root["RotationTrack"] = serializeVectorTrack(data.RotationTrack);

            root["EventTrack"] = json::array();
            for (const auto& keyframe : data.EventTrack)
            {
                root["EventTrack"].push_back({
                    {"TimeSeconds", keyframe.TimeSeconds},
                    {"Name", keyframe.Name},
                    {"StringPayload", keyframe.StringPayload},
                    {"FloatPayload", keyframe.FloatPayload},
                    {"IntegerPayload", keyframe.IntegerPayload},
                    {"BooleanPayload", keyframe.BooleanPayload}
                });
            }

            return root.dump(2);
        }

        Result<AnimationClipAsset::Data> LoadClipDataFromKey(const std::string& key,
                                                             std::string& outResolvedPath,
                                                             std::string& outGuid)
        {
            std::string jsonText;
            bool loadedFromBundle = false;
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
                    return Result<AnimationClipAsset::Data>(
                        resolvedResult.GetError().GetCode(),
                        "AnimationClipAsset::Load failed resolving '" + key + "': " + resolvedResult.GetError().GetErrorMessage());
                }
                outResolvedPath = resolvedResult.GetValue().string();

                std::ifstream input(outResolvedPath, std::ios::in | std::ios::binary);
                if (!input.is_open())
                {
                    return Result<AnimationClipAsset::Data>(
                        ErrorCode::FileNotFound,
                        "AnimationClipAsset::Load failed opening file '" + outResolvedPath + "'");
                }
                jsonText.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            }

            if (outGuid.empty())
            {
                const auto guidResult = LoadOrCreateGuid(outResolvedPath, {{"key", key}, {"type", "AnimationClip"}});
                if (guidResult.IsFailure())
                {
                    return Result<AnimationClipAsset::Data>(
                        guidResult.GetError().GetCode(),
                        "AnimationClipAsset::Load failed obtaining GUID for '" + outResolvedPath + "': " + guidResult.GetError().GetErrorMessage());
                }
                outGuid = guidResult.GetValue();
            }

            return ParseClipJsonText(jsonText, outResolvedPath);
        }

        std::vector<std::string> BuildDependencyGuids(const AnimationClipAsset::Data& data)
        {
            std::vector<std::string> result;
            std::unordered_set<std::string> uniqueGuids;
            for (const auto& keyframe : data.SpriteTextureTrack)
            {
                if (keyframe.TextureKey.empty())
                    continue;
                const auto record = AssetDatabase::GetInstance().FindByKey(keyframe.TextureKey);
                if (!record.IsSuccess() || record.GetValue().Guid.empty())
                    continue;
                if (uniqueGuids.insert(record.GetValue().Guid).second)
                    result.push_back(record.GetValue().Guid);
            }
            return result;
        }
    }

    Async::Task<AnimationClipAsset::Ptr> AnimationClipAsset::LoadAsync(const std::string& key)
    {
        return LoadAsync(key, Settings{});
    }

    Async::Task<AnimationClipAsset::Ptr> AnimationClipAsset::LoadAsync(const std::string& key, Settings settings)
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

            AssetLoadProgress::SetProgress(key, 0.1f, "Loading animation clip...");

            std::string resolvedPath;
            std::string guid;
            const auto dataResult = LoadClipDataFromKey(key, resolvedPath, guid);
            if (dataResult.IsFailure())
            {
                AssetLoadProgress::ClearProgress(key);
                LT_CORE_ERROR("{}", dataResult.GetError().GetErrorMessage());
                promise.set_value(nullptr);
                return;
            }

            AssetLoadProgress::SetProgress(key, 0.8f, "Finalizing...");
            auto asset = AssetManager::GetOrLoad<AnimationClipAsset>(key, [&]() -> Ptr {
                Ptr created(new AnimationClipAsset(key, guid, dataResult.GetValue(), settings));
                created->m_ResolvedPath = resolvedPath;
                created->m_Revision.fetch_add(1, std::memory_order_relaxed);
                (void)AssetDatabase::GetInstance().SetDependencies(created->GetGuid(), BuildDependencyGuids(created->m_Data));
                return created;
            });

            AssetLoadProgress::ClearProgress(key);
            promise.set_value(std::move(asset));
        });

        return Async::Task<Ptr>(std::move(shared));
    }

    AnimationClipAsset::Ptr AnimationClipAsset::LoadBlocking(const std::string& key)
    {
        return LoadBlocking(key, Settings{});
    }

    AnimationClipAsset::Ptr AnimationClipAsset::LoadBlocking(const std::string& key, Settings settings)
    {
        auto task = LoadAsync(key, std::move(settings));
        task.Wait();
        return task.Get();
    }

    bool AnimationClipAsset::Reload()
    {
        std::string resolvedPath;
        std::string guid;
        const auto dataResult = LoadClipDataFromKey(GetKey(), resolvedPath, guid);
        if (dataResult.IsFailure())
        {
            LT_CORE_ERROR("AnimationClipAsset::Reload failed: {}", dataResult.GetError().GetErrorMessage());
            return false;
        }

        m_ResolvedPath = std::move(resolvedPath);
        m_Data = dataResult.GetValue();
        SortAndClampTrackTimes(m_Data);
        m_Revision.fetch_add(1, std::memory_order_relaxed);
        (void)AssetDatabase::GetInstance().SetDependencies(GetGuid(), BuildDependencyGuids(m_Data));
        return true;
    }
}

