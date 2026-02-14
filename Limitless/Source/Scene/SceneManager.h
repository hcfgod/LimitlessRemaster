#pragma once

#include <optional>
#include <string>

namespace Limitless
{
    enum class SceneTransitionType
    {
        LoadByAssetKey,
        ReloadCurrentScene
    };

    struct SceneTransitionRequest
    {
        SceneTransitionType Type = SceneTransitionType::LoadByAssetKey;
        std::string SceneIdentifier;
    };

    using SceneTransitionBridgeCallback = bool (*)(SceneTransitionType transitionType, const char* sceneIdentifier);

    /**
     * @brief Unity-style scene transition API for gameplay scripts.
     *
     * `LoadScene` and `ReloadCurrentScene` queue a transition request. The host
     * application is expected to consume and apply the request at a safe point
     * (typically after scene/script update for the current frame).
     */
    class SceneManager final
    {
    public:
        SceneManager() = delete;

        /// Queue a request to load a scene by asset key or scene name.
        ///
        /// Supported inputs:
        /// - Full asset key: `Assets/Scenes/MainMenu.scene.json`
        /// - Asset key without extension: `Assets/Scenes/MainMenu`
        /// - Scene name only: `MainMenu` (resolved via AssetDatabase scene records)
        static bool LoadScene(const std::string& sceneIdentifier);

        /// Queue a request to reload the currently active scene.
        static bool ReloadCurrentScene();

        /// Set a host bridge callback used when SceneManager is called from ScriptCore.
        static void SetTransitionBridgeCallback(SceneTransitionBridgeCallback callback);

        /// Returns true when a scene transition request is pending.
        static bool HasPendingSceneTransition();

        /// Returns and clears the pending scene transition request.
        static std::optional<SceneTransitionRequest> ConsumePendingSceneTransition();

        /// Clears any pending scene transition request.
        static void ClearPendingSceneTransition();
    };
}
