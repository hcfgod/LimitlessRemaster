#pragma once

#include "Assets/AssetTypes.h"
#include "Scene/Components/AudioComponents.h"
#include "Scene/Components/ScriptingComponents.h"

#include <nlohmann/json.hpp>

#include <string>

namespace Limitless::SceneSerialization
{
    nlohmann::json MakeAssetReferenceJson(const std::string& assetKey, Assets::AssetType type);
    std::string ResolveAssetKeyFromSceneJson(const nlohmann::json& value);

    nlohmann::json SerializeScriptPropertyValue(const ScriptPropertyValue& value);
    bool DeserializeScriptPropertyValue(const nlohmann::json& root, ScriptPropertyValue& outValue);

    const char* ToAudioPlaybackSpaceName(AudioSourceComponent::PlaybackSpace space);
    AudioSourceComponent::PlaybackSpace ParseAudioPlaybackSpaceName(const std::string& spaceName);
}
