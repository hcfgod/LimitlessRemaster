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
        struct SceneManagerRuntimeState
        {
            std::mutex SceneTransitionMutex;
            std::optional<SceneTransitionRequest> PendingSceneTransition;
            SceneTransitionBridgeCallback TransitionBridgeCallback = nullptr;
        };

        SceneManagerRuntimeState& GetSceneManagerRuntimeState()
        {
            static SceneManagerRuntimeState state{};
            return state;
        }

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

        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.TransitionBridgeCallback != nullptr)
            return state.TransitionBridgeCallback(SceneTransitionType::LoadByAssetKey, sceneIdentifier.c_str());

        const auto normalizedSceneIdentifier = NormalizeSceneAssetKey(sceneIdentifier);
        if (!normalizedSceneIdentifier.has_value())
            return false;

        state.PendingSceneTransition = SceneTransitionRequest{ SceneTransitionType::LoadByAssetKey, *normalizedSceneIdentifier };
        return true;
    }

    bool SceneManager::ReloadCurrentScene()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.TransitionBridgeCallback != nullptr)
            return state.TransitionBridgeCallback(SceneTransitionType::ReloadCurrentScene, "");

        state.PendingSceneTransition = SceneTransitionRequest{ SceneTransitionType::ReloadCurrentScene, {} };
        return true;
    }

    void SceneManager::SetTransitionBridgeCallback(SceneTransitionBridgeCallback callback)
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        state.TransitionBridgeCallback = callback;
    }

    bool SceneManager::HasPendingSceneTransition()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        return state.PendingSceneTransition.has_value();
    }

    std::optional<SceneTransitionRequest> SceneManager::ConsumePendingSceneTransition()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (!state.PendingSceneTransition.has_value())
            return std::nullopt;

        std::optional<SceneTransitionRequest> transition = std::move(state.PendingSceneTransition);
        state.PendingSceneTransition.reset();
        return transition;
    }

    void SceneManager::ClearPendingSceneTransition()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        state.PendingSceneTransition.reset();
    }
}
