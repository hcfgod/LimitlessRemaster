#include "Scene/SceneSerialization.h"

#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetUtils.h"

#include <nlohmann/json.hpp>

namespace Limitless::SceneSerialization
{
    namespace
    {
        // Resolve legacy/stale asset keys to the latest known key in AssetDatabase.
        // This keeps scene references resilient across asset moves/renames.
        std::string ResolveLatestKeyFromDatabase(const std::string& assetKey)
        {
            if (assetKey.empty())
                return {};

            const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
            if (record.IsSuccess() && !record.GetValue().Key.empty())
                return record.GetValue().Key;

            return assetKey;
        }
    }

    nlohmann::json MakeAssetReferenceJson(const std::string& assetKey, Assets::AssetType type)
    {
        using json = nlohmann::json;

        json ref = json::object();
        ref["guid"] = "";
        ref["key"] = assetKey;

        if (assetKey.empty())
        {
            return ref;
        }

        // Preferred: AssetDatabase GUID (stable, fast).
        const auto record = Assets::AssetDatabase::GetInstance().FindByKey(assetKey);
        if (record.IsSuccess() && !record.GetValue().Guid.empty())
        {
            ref["guid"] = record.GetValue().Guid;
            return ref;
        }

        // Fallback: resolve path and ensure `.meta` exists.
        const auto resolved = Assets::ResolveAssetKeyToPath(assetKey);
        if (resolved.IsSuccess())
        {
            const auto guidResult = Assets::LoadOrCreateGuid(resolved.GetValue().string(), {{"key", assetKey}, {"type", Assets::ToString(type)}});
            if (guidResult.IsSuccess())
            {
                ref["guid"] = guidResult.GetValue();
                (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, type);
            }
        }

        return ref;
    }

    std::string ResolveAssetKeyFromSceneJson(const nlohmann::json& value)
    {
        if (value.is_string())
        {
            return ResolveLatestKeyFromDatabase(value.get<std::string>());
        }

        if (!value.is_object())
        {
            return {};
        }

        // Preferred: GUID -> key.
        if (value.contains("guid") && value["guid"].is_string())
        {
            const std::string guid = value["guid"].get<std::string>();
            if (!guid.empty())
            {
                const auto rec = Assets::AssetDatabase::GetInstance().FindByGuid(guid);
                if (rec.IsSuccess() && !rec.GetValue().Key.empty())
                {
                    return rec.GetValue().Key;
                }
            }
        }

        // Fallback: embedded key.
        if (value.contains("key") && value["key"].is_string())
        {
            return ResolveLatestKeyFromDatabase(value["key"].get<std::string>());
        }

        return {};
    }

    nlohmann::json SerializeScriptPropertyValue(const ScriptPropertyValue& value)
    {
        nlohmann::json root = nlohmann::json::object();
        if (const auto* floatValue = std::get_if<float>(&value))
        {
            root["Type"] = "Float";
            root["Value"] = *floatValue;
        }
        else if (const auto* integerValue = std::get_if<int32_t>(&value))
        {
            root["Type"] = "Integer";
            root["Value"] = *integerValue;
        }
        else if (const auto* booleanValue = std::get_if<bool>(&value))
        {
            root["Type"] = "Boolean";
            root["Value"] = *booleanValue;
        }
        else if (const auto* vectorValue = std::get_if<glm::vec3>(&value))
        {
            root["Type"] = "Vector3";
            root["Value"] = { vectorValue->x, vectorValue->y, vectorValue->z };
        }
        else if (const auto* stringValue = std::get_if<std::string>(&value))
        {
            root["Type"] = "String";
            root["Value"] = *stringValue;
        }
        else if (const auto* entityValue = std::get_if<ScriptEntityReference>(&value))
        {
            root["Type"] = "Entity";
            root["Value"] = {
                { "Tag", entityValue->Tag },
                { "PrefabAssetKey", entityValue->PrefabAssetKey },
                { "SceneEntityId", entityValue->SceneEntityId }
            };
        }
        else if (const auto* prefabValue = std::get_if<ScriptPrefabReference>(&value))
        {
            root["Type"] = "Prefab";
            root["Value"] = {
                { "AssetKey", prefabValue->AssetKey }
            };
        }
        return root;
    }

    bool DeserializeScriptPropertyValue(const nlohmann::json& root, ScriptPropertyValue& outValue)
    {
        if (!root.is_object())
            return false;

        const std::string typeName = root.value("Type", std::string{});
        if (typeName == "Float")
        {
            outValue = root.value("Value", 0.0f);
            return true;
        }
        if (typeName == "Integer")
        {
            outValue = root.value("Value", 0);
            return true;
        }
        if (typeName == "Boolean")
        {
            outValue = root.value("Value", false);
            return true;
        }
        if (typeName == "Vector3")
        {
            const auto vector = root.value("Value", std::vector<float>{ 0.0f, 0.0f, 0.0f });
            if (vector.size() >= 3)
                outValue = glm::vec3(vector[0], vector[1], vector[2]);
            else
                outValue = glm::vec3(0.0f);
            return true;
        }
        if (typeName == "String")
        {
            outValue = root.value("Value", std::string{});
            return true;
        }
        if (typeName == "Entity")
        {
            ScriptEntityReference entityReference{};
            if (root.contains("Value"))
            {
                const auto& value = root["Value"];
                if (value.is_object())
                {
                    entityReference.Tag = value.value("Tag", std::string{});
                    entityReference.PrefabAssetKey = value.value("PrefabAssetKey", std::string{});
                    entityReference.SceneEntityId = value.value("SceneEntityId", std::string{});
                }
                else if (value.is_string())
                    entityReference.Tag = value.get<std::string>();
            }
            outValue = std::move(entityReference);
            return true;
        }
        if (typeName == "Prefab")
        {
            Prefab prefabReference{};
            if (root.contains("Value"))
            {
                const auto& value = root["Value"];
                if (value.is_object())
                {
                    prefabReference.AssetKey = value.value("AssetKey", std::string{});
                    if (prefabReference.AssetKey.empty())
                        prefabReference.AssetKey = value.value("PrefabAssetKey", std::string{});
                }
                else if (value.is_string())
                    prefabReference.AssetKey = value.get<std::string>();
            }
            outValue = std::move(prefabReference);
            return true;
        }

        return false;
    }

    const char* ToAudioPlaybackSpaceName(AudioSourceComponent::PlaybackSpace space)
    {
        switch (space)
        {
            case AudioSourceComponent::PlaybackSpace::Spatial3D: return "Spatial3D";
            case AudioSourceComponent::PlaybackSpace::Spatial2D: return "Spatial2D";
            case AudioSourceComponent::PlaybackSpace::Global:
            default: return "Global";
        }
    }

    AudioSourceComponent::PlaybackSpace ParseAudioPlaybackSpaceName(const std::string& spaceName)
    {
        if (spaceName == "Spatial3D")
            return AudioSourceComponent::PlaybackSpace::Spatial3D;
        if (spaceName == "Spatial2D")
            return AudioSourceComponent::PlaybackSpace::Spatial2D;
        return AudioSourceComponent::PlaybackSpace::Global;
    }
}
