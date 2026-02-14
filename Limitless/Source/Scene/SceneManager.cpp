#include "Scene/SceneManager.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string_view>

namespace Limitless
{
    namespace
    {
        constexpr std::string_view kSceneAssetSuffix = ".scene.json";
        std::mutex s_SceneTransitionMutex;
        std::optional<SceneTransitionRequest> s_PendingSceneTransition;
        SceneTransitionBridgeCallback s_TransitionBridgeCallback = nullptr;

        std::string NormalizeSlashes(std::string pathText)
        {
            std::replace(pathText.begin(), pathText.end(), '\\', '/');
            return pathText;
        }

        std::optional<std::string> NormalizeSceneAssetKey(const std::string& sceneIdentifier)
        {
            if (sceneIdentifier.empty())
                return std::nullopt;

            const std::string normalizedIdentifier = NormalizeSlashes(sceneIdentifier);
            const bool looksLikeAssetPath = normalizedIdentifier.find('/') != std::string::npos;
            if (looksLikeAssetPath)
            {
                std::string sceneAssetKey = normalizedIdentifier;
                if (sceneAssetKey.rfind("Assets/", 0) != 0)
                    sceneAssetKey = "Assets/" + sceneAssetKey;

                if (sceneAssetKey.ends_with(kSceneAssetSuffix))
                    return sceneAssetKey;
                return sceneAssetKey + std::string(kSceneAssetSuffix);
            }
            return normalizedIdentifier;
        }
    }

    bool SceneManager::LoadScene(const std::string& sceneIdentifier)
    {
        if (sceneIdentifier.empty())
            return false;

        std::scoped_lock<std::mutex> lock(s_SceneTransitionMutex);
        if (s_TransitionBridgeCallback != nullptr)
            return s_TransitionBridgeCallback(SceneTransitionType::LoadByAssetKey, sceneIdentifier.c_str());

        const auto normalizedSceneIdentifier = NormalizeSceneAssetKey(sceneIdentifier);
        if (!normalizedSceneIdentifier.has_value())
            return false;

        s_PendingSceneTransition = SceneTransitionRequest{ SceneTransitionType::LoadByAssetKey, *normalizedSceneIdentifier };
        return true;
    }

    bool SceneManager::ReloadCurrentScene()
    {
        std::scoped_lock<std::mutex> lock(s_SceneTransitionMutex);
        if (s_TransitionBridgeCallback != nullptr)
            return s_TransitionBridgeCallback(SceneTransitionType::ReloadCurrentScene, "");

        s_PendingSceneTransition = SceneTransitionRequest{ SceneTransitionType::ReloadCurrentScene, {} };
        return true;
    }

    void SceneManager::SetTransitionBridgeCallback(SceneTransitionBridgeCallback callback)
    {
        std::scoped_lock<std::mutex> lock(s_SceneTransitionMutex);
        s_TransitionBridgeCallback = callback;
    }

    bool SceneManager::HasPendingSceneTransition()
    {
        std::scoped_lock<std::mutex> lock(s_SceneTransitionMutex);
        return s_PendingSceneTransition.has_value();
    }

    std::optional<SceneTransitionRequest> SceneManager::ConsumePendingSceneTransition()
    {
        std::scoped_lock<std::mutex> lock(s_SceneTransitionMutex);
        if (!s_PendingSceneTransition.has_value())
            return std::nullopt;

        std::optional<SceneTransitionRequest> transition = std::move(s_PendingSceneTransition);
        s_PendingSceneTransition.reset();
        return transition;
    }

    void SceneManager::ClearPendingSceneTransition()
    {
        std::scoped_lock<std::mutex> lock(s_SceneTransitionMutex);
        s_PendingSceneTransition.reset();
    }
}
