#pragma once

#include <optional>
 #include <string>

namespace Limitless
{
    enum class LoadSceneMode
    {
        Single = 0,
        Additive = 1
    };

    enum class SceneTransitionType
    {
        LoadByAssetKey,
        ReloadCurrentScene,
        SetActiveSceneByAssetKey,
        UnloadByAssetKey
    };

    struct SceneTransitionRequest
    {
        SceneTransitionType Type = SceneTransitionType::LoadByAssetKey;
        std::string SceneIdentifier;
        LoadSceneMode LoadMode = LoadSceneMode::Single;
    };

    using SceneTransitionBridgeCallback = bool (*)(SceneTransitionType transitionType, const char* sceneIdentifier, LoadSceneMode loadSceneMode);

    /// Unity-style scene transition API for gameplay scripts.
    ///
    /// Supports single-scene replacement, additive loading, explicit active-scene
    /// selection, and unloading. Hosts consume queued transitions at a safe point.
    ///
    /// `LoadScene`, `ReloadCurrentScene`, `SetActiveScene`, and `UnloadScene`
    /// queue transition requests. The host application is expected to consume and
    /// apply them at a safe point (typically after scene/script update for the
    /// current frame).
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
        static bool LoadScene(const std::string& sceneIdentifier, LoadSceneMode loadMode = LoadSceneMode::Single);

        /// Queue a request to reload the currently active scene.
        static bool ReloadCurrentScene();

        /// Queue a request to make a loaded scene the active gameplay/script scene.
        static bool SetActiveScene(const std::string& sceneIdentifier);

        /// Queue a request to unload a loaded scene.
        static bool UnloadScene(const std::string& sceneIdentifier);

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
