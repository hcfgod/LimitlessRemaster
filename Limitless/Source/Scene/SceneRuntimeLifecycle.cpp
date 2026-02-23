#include "Scene/Scene.h"

#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"

namespace Limitless
{
    void Scene::BeginLoadingState()
    {
        m_LoadState = LoadState::Loading;
        m_SceneObjectsInitialized = false;
        m_PhysicsWorldInitializedForLoading = false;
    }

    void Scene::MarkSceneObjectsInitialized()
    {
        m_SceneObjectsInitialized = true;
    }

    bool Scene::InitializePhysicsWorldForLoading()
    {
        if (m_PhysicsWorldInitializedForLoading)
            return true;

        if (!m_Physics2DWorld)
            m_Physics2DWorld = std::make_unique<Physics2DWorld>();

        if (!m_Physics2DWorld->IsInitialized())
            m_Physics2DWorld->Initialize(m_Physics2DSettings);

        m_Physics2DWorld->SetSettings(m_Physics2DSettings);
        m_Physics2DWorld->RebuildScene(*this);
        m_PhysicsWorldInitializedForLoading = true;
        return true;
    }

    void Scene::SetLoadStateReady()
    {
        m_LoadState = LoadState::Ready;
        m_SceneObjectsInitialized = true;
        m_PhysicsWorldInitializedForLoading = true;
    }

    void Scene::StepPhysics2D(float fixedDeltaTime)
    {
        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (!m_Physics2DWorld)
            m_Physics2DWorld = std::make_unique<Physics2DWorld>();
        if (!m_Physics2DWorld->IsInitialized())
            m_Physics2DWorld->Initialize(m_Physics2DSettings);
        m_Physics2DWorld->SetSettings(m_Physics2DSettings);
        m_Physics2DWorld->Step(*this, fixedDeltaTime);
        m_PhysicsWorldInitializedForLoading = true;
    }

    void Scene::SetPhysics2DSettings(const Physics2DWorldSettings& settings)
    {
        m_Physics2DSettings = settings;
        if (m_Physics2DWorld)
            m_Physics2DWorld->SetSettings(m_Physics2DSettings);
    }

    Physics2DWorld* Scene::GetPhysics2DWorld()
    {
        return m_Physics2DWorld.get();
    }

    const Physics2DWorld* Scene::GetPhysics2DWorld() const
    {
        return m_Physics2DWorld.get();
    }

    const Physics2DContactListener* Scene::GetPhysics2DContactEvents() const
    {
        if (!m_Physics2DWorld)
            return nullptr;
        return &m_Physics2DWorld->GetContactListener();
    }

    void Scene::ResetPhysicsRuntimeState()
    {
        if (m_Physics2DWorld)
            m_Physics2DWorld->Shutdown(*this);
    }
}
