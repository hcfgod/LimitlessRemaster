#include "GameLayer.h"
#include "GameLayerInternal.h"

#include "Audio/SceneAudioSystem.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Camera/CameraManager.h"
#include "Physics/Physics2DQueries.h"
#include "Scene/Scene.h"

namespace Limitless
{

    GameLayer::GameLayer()
        : Layer("GameLayer")
        , m_Scene(m_SceneCollection, m_SceneHandle)
    {
    }

    GameLayer::~GameLayer() = default;

    void GameLayer::OnAttach()
    {
        LT_INFO("GameLayer: attaching (shipped game mode).");
        Physics2DQueries::SetActiveSceneCollectionForScriptQueries(&m_SceneCollection, ToSceneRoleMask(SceneRole::ScriptQueryTarget));

        if (!LoadBootstrap())
        {
            LT_ERROR("GameLayer: Failed to load GameBootstrap.json. Cannot start game.");
            Application::GetInstance().SetRunning(false);
            return;
        }

        GameLayerInternal::ApplyRuntimeProjectSettingsFromBundle();

        if (!m_ProjectName.empty())
            Application::GetInstance().GetWindow().SetTitle(m_ProjectName);

        InitializeScriptCore();

        if (!LoadScene(m_StartupSceneKey))
        {
            LT_ERROR("GameLayer: Failed to load startup scene '{}'.", m_StartupSceneKey);
            Application::GetInstance().SetRunning(false);
            return;
        }

        LT_INFO("GameLayer: game started with scene '{}'.", m_StartupSceneKey);
    }

    void GameLayer::OnDetach()
    {
        ClearLoadedScenes();

        Physics2DQueries::SetActiveSceneCollectionForScriptQueries(nullptr);

        ShutdownScriptCore();
        LT_INFO("GameLayer: detached.");
    }

    void GameLayer::OnUpdate(float deltaTime)
    {
        for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::RuntimeUpdate)))
        {
            Scene* scene = m_SceneCollection.GetScene(handle);
            if (scene && scene->IsReady())
                scene->Update(deltaTime);
        }

        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);

        for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::AudioPlayback)))
        {
            Scene* scene = m_SceneCollection.GetScene(handle);
            if (scene && scene->IsReady())
                Audio::UpdateSceneAudioSources(scene, deltaTime);
        }

        ProcessPendingSceneTransitions();
    }

    void GameLayer::OnFixedUpdate(float fixedDeltaTime)
    {
        for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(ToSceneRoleMask(SceneRole::FixedUpdate)))
        {
            Scene* scene = m_SceneCollection.GetScene(handle);
            if (!scene || !scene->IsReady())
                continue;

            scene->FixedUpdate(fixedDeltaTime);
            scene->StepPhysics2D(fixedDeltaTime);
        }
        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
    }

    void GameLayer::OnRender()
    {
        if (!m_Scene || !m_Scene->IsReady())
            return;

        Camera* camera = ResolveGameplayCamera();
        if (camera)
        {
            m_LoggedMissingGameplayCamera = false;
            RenderLoadedScenes(*camera);
        }
        else if (!m_LoggedMissingGameplayCamera)
        {
            LT_WARN("GameLayer: no gameplay camera resolved for render.");
            m_LoggedMissingGameplayCamera = true;
        }
    }

    void GameLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        m_ViewportWidth = event.GetWidth();
        m_ViewportHeight = event.GetHeight();

        // Update the gameplay camera viewport if it exists.
        if (auto* camera = m_CameraManager.GetOrthographic2D(m_GameplayCameraId))
            camera->SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }
}
