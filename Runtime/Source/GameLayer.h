#pragma once

#include "Limitless.h"
#include "Scene/SceneCollection.h"

#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    class Scene;

    // -------------------------------------------------------------------------
    // GameLayer
    //
    // Runtime game layer for shipped builds. Reads GameBootstrap.json on attach,
    // loads the startup scene, runs scripts/physics/animation, and renders using
    // the primary in-scene CameraComponent. No ImGui, no editor panels.
    //
    // Activated by RuntimeApp when GameBootstrap.json is detected next to the
    // executable (shipped game mode).
    // -------------------------------------------------------------------------
    class GameLayer : public Layer
    {
    public:
        GameLayer();
        ~GameLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTime) override;
        void OnFixedUpdate(float fixedDeltaTime) override;
        void OnRender() override;

    protected:
        void OnWindowResize(Events::WindowResizeEvent& event) override;

    private:
        /// Read GameBootstrap.json from the executable directory.
        bool LoadBootstrap();

        /// Load a scene by asset key from the AssetBundle.
        bool LoadScene(const std::string& sceneAssetKey, LoadSceneMode loadMode = LoadSceneMode::Single);

        bool ActivateLoadedScene(SceneCollection::Handle handle);
        bool ActivateLoadedSceneByAssetKey(const std::string& sceneAssetKey);
        bool UnloadLoadedSceneByAssetKey(const std::string& sceneAssetKey);
        void ClearLoadedScenes();
        SceneCollection::Handle FindLoadedSceneHandleByAssetKey(const std::string& sceneAssetKey) const;
        std::string ResolveBuildSceneAssetKey(const std::string& sceneIdentifier) const;
        void ResetGameplayCameraState();
        void RenderLoadedScenes(Camera& camera);

        /// Initialize the ScriptCore DLL (register scripts, connect bridges).
        void InitializeScriptCore();

        /// Shut down the ScriptCore DLL.
        void ShutdownScriptCore();

        /// Process pending SceneManager::LoadScene() transitions.
        void ProcessPendingSceneTransitions();

        /// Find and cache the primary gameplay camera from the scene.
        Camera* ResolveGameplayCamera();

        std::string m_ProjectName;
        std::string m_StartupSceneKey;
        std::vector<std::string> m_BuildScenes;

        SceneCollection m_SceneCollection;
        SceneCollection::Handle m_SceneHandle = SceneCollection::InvalidHandle;
        SceneCollectionSlot m_Scene;
        std::string m_CurrentSceneAssetKey;

        CameraManager m_CameraManager;
        CameraId m_GameplayCameraId{};

        uint32_t m_ViewportWidth = 1280;
        uint32_t m_ViewportHeight = 720;

        void* m_ScriptCoreLibraryHandle = nullptr;
        bool m_LoggedMissingGameplayCamera = false;
    };
}
