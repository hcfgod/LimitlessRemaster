#include "Assets/AssetTypes.h"

namespace Limitless::Assets
{
    const char* ToString(AssetType type)
    {
        switch (type)
        {
            case AssetType::Texture2D: return "Texture2D";
            case AssetType::Shader:    return "Shader";
            case AssetType::Material:  return "Material";
            case AssetType::Mesh:      return "Mesh";
            case AssetType::Scene:     return "Scene";
            case AssetType::InputActions: return "InputActions";
            case AssetType::AudioClip: return "AudioClip";
            case AssetType::Prefab: return "Prefab";
            case AssetType::Tilemap: return "Tilemap";
            case AssetType::AnimationClip: return "AnimationClip";
            case AssetType::Tileset: return "Tileset";
            case AssetType::AudioMixer: return "AudioMixer";
            case AssetType::AnimatorController: return "AnimatorController";
            case AssetType::Unknown:
            default:                   return "Unknown";
        }
    }

    AssetType AssetTypeFromString(const std::string& s)
    {
        if (s == "Texture2D") return AssetType::Texture2D;
        if (s == "Shader") return AssetType::Shader;
        if (s == "Material") return AssetType::Material;
        if (s == "Mesh") return AssetType::Mesh;
        if (s == "Scene") return AssetType::Scene;
        if (s == "InputActions") return AssetType::InputActions;
        if (s == "AudioClip") return AssetType::AudioClip;
        if (s == "Prefab") return AssetType::Prefab;
        if (s == "Tilemap") return AssetType::Tilemap;
        if (s == "AnimationClip") return AssetType::AnimationClip;
        if (s == "Tileset") return AssetType::Tileset;
        if (s == "AudioMixer") return AssetType::AudioMixer;
        if (s == "AnimatorController") return AssetType::AnimatorController;
        return AssetType::Unknown;
    }
}

