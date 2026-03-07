#include "Scene/SceneManager.h"

#include <algorithm>
#include <cctype>
#include <deque>
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
            std::deque<SceneTransitionRequest> PendingSceneTransitions;
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

        bool EnqueueSceneTransition(SceneManagerRuntimeState& state,
                                    SceneTransitionType transitionType,
                                    const std::string& sceneIdentifier,
                                    LoadSceneMode loadMode)
        {
            state.PendingSceneTransitions.push_back(SceneTransitionRequest{ transitionType, sceneIdentifier, loadMode });
            return true;
        }
    }

    bool SceneManager::LoadScene(const std::string& sceneIdentifier, LoadSceneMode loadMode)
    {
        if (sceneIdentifier.empty())
            return false;

        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.TransitionBridgeCallback != nullptr)
            return state.TransitionBridgeCallback(SceneTransitionType::LoadByAssetKey, sceneIdentifier.c_str(), loadMode);

        const auto normalizedSceneIdentifier = NormalizeSceneAssetKey(sceneIdentifier);
        if (!normalizedSceneIdentifier.has_value())
            return false;

        return EnqueueSceneTransition(state, SceneTransitionType::LoadByAssetKey, *normalizedSceneIdentifier, loadMode);
    }

    bool SceneManager::ReloadCurrentScene()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.TransitionBridgeCallback != nullptr)
            return state.TransitionBridgeCallback(SceneTransitionType::ReloadCurrentScene, "", LoadSceneMode::Single);

        return EnqueueSceneTransition(state, SceneTransitionType::ReloadCurrentScene, {}, LoadSceneMode::Single);
    }

    bool SceneManager::SetActiveScene(const std::string& sceneIdentifier)
    {
        if (sceneIdentifier.empty())
            return false;

        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.TransitionBridgeCallback != nullptr)
            return state.TransitionBridgeCallback(SceneTransitionType::SetActiveSceneByAssetKey, sceneIdentifier.c_str(), LoadSceneMode::Single);

        const auto normalizedSceneIdentifier = NormalizeSceneAssetKey(sceneIdentifier);
        if (!normalizedSceneIdentifier.has_value())
            return false;

        return EnqueueSceneTransition(state, SceneTransitionType::SetActiveSceneByAssetKey, *normalizedSceneIdentifier, LoadSceneMode::Single);
    }

    bool SceneManager::UnloadScene(const std::string& sceneIdentifier)
    {
        if (sceneIdentifier.empty())
            return false;

        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.TransitionBridgeCallback != nullptr)
            return state.TransitionBridgeCallback(SceneTransitionType::UnloadByAssetKey, sceneIdentifier.c_str(), LoadSceneMode::Single);

        const auto normalizedSceneIdentifier = NormalizeSceneAssetKey(sceneIdentifier);
        if (!normalizedSceneIdentifier.has_value())
            return false;

        return EnqueueSceneTransition(state, SceneTransitionType::UnloadByAssetKey, *normalizedSceneIdentifier, LoadSceneMode::Single);
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
        return !state.PendingSceneTransitions.empty();
    }

    std::optional<SceneTransitionRequest> SceneManager::ConsumePendingSceneTransition()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        if (state.PendingSceneTransitions.empty())
            return std::nullopt;

        SceneTransitionRequest transition = std::move(state.PendingSceneTransitions.front());
        state.PendingSceneTransitions.pop_front();
        return transition;
    }

    void SceneManager::ClearPendingSceneTransition()
    {
        auto& state = GetSceneManagerRuntimeState();
        std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
        state.PendingSceneTransitions.clear();
    }
}
