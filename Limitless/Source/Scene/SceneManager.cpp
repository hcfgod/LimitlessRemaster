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

        thread_local bool s_IsDispatchingSceneTransitionBridge = false;

        class ScopedSceneTransitionBridgeDispatch final
        {
        public:
            ScopedSceneTransitionBridgeDispatch()
                : m_PreviousValue(s_IsDispatchingSceneTransitionBridge)
            {
                s_IsDispatchingSceneTransitionBridge = true;
            }

            ~ScopedSceneTransitionBridgeDispatch()
            {
                s_IsDispatchingSceneTransitionBridge = m_PreviousValue;
            }

        private:
            bool m_PreviousValue = false;
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

        bool DispatchSceneTransition(SceneManagerRuntimeState& state,
                                     SceneTransitionType transitionType,
                                     const std::string& sceneIdentifier,
                                     LoadSceneMode loadMode)
        {
            SceneTransitionBridgeCallback callback = nullptr;
            {
                std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
                callback = state.TransitionBridgeCallback;
                if (callback == nullptr || s_IsDispatchingSceneTransitionBridge)
                {
                    const auto normalizedSceneIdentifier = NormalizeSceneAssetKey(sceneIdentifier);
                    if (!normalizedSceneIdentifier.has_value())
                        return false;

                    return EnqueueSceneTransition(state, transitionType, *normalizedSceneIdentifier, loadMode);
                }
            }

            ScopedSceneTransitionBridgeDispatch dispatchScope;
            return callback(transitionType, sceneIdentifier.c_str(), loadMode);
        }

        bool DispatchSceneTransition(SceneManagerRuntimeState& state,
                                     SceneTransitionType transitionType,
                                     LoadSceneMode loadMode)
        {
            SceneTransitionBridgeCallback callback = nullptr;
            {
                std::scoped_lock<std::mutex> lock(state.SceneTransitionMutex);
                callback = state.TransitionBridgeCallback;
                if (callback == nullptr || s_IsDispatchingSceneTransitionBridge)
                    return EnqueueSceneTransition(state, transitionType, {}, loadMode);
            }

            ScopedSceneTransitionBridgeDispatch dispatchScope;
            return callback(transitionType, "", loadMode);
        }
    }

    bool SceneManager::LoadScene(const std::string& sceneIdentifier, LoadSceneMode loadMode)
    {
        if (sceneIdentifier.empty())
            return false;

        auto& state = GetSceneManagerRuntimeState();
        return DispatchSceneTransition(state, SceneTransitionType::LoadByAssetKey, sceneIdentifier, loadMode);
    }

    bool SceneManager::ReloadCurrentScene()
    {
        auto& state = GetSceneManagerRuntimeState();
        return DispatchSceneTransition(state, SceneTransitionType::ReloadCurrentScene, LoadSceneMode::Single);
    }

    bool SceneManager::SetActiveScene(const std::string& sceneIdentifier)
    {
        if (sceneIdentifier.empty())
            return false;

        auto& state = GetSceneManagerRuntimeState();
        return DispatchSceneTransition(state, SceneTransitionType::SetActiveSceneByAssetKey, sceneIdentifier, LoadSceneMode::Single);
    }

    bool SceneManager::UnloadScene(const std::string& sceneIdentifier)
    {
        if (sceneIdentifier.empty())
            return false;

        auto& state = GetSceneManagerRuntimeState();
        return DispatchSceneTransition(state, SceneTransitionType::UnloadByAssetKey, sceneIdentifier, LoadSceneMode::Single);
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
