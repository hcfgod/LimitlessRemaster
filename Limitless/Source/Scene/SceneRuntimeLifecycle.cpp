#include "Scene/Scene.h"

#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"

#include <algorithm>

namespace Limitless
{
    namespace
    {
        constexpr uint16_t kMinPhysicsWorldCount = 1;
        constexpr uint16_t kMaxPhysicsWorldCount = 16;

        uint16_t SanitizeWorldCount(uint16_t requestedCount)
        {
            return static_cast<uint16_t>(std::clamp<int>(requestedCount, kMinPhysicsWorldCount, kMaxPhysicsWorldCount));
        }

        uint16_t ClampWorldSlot(uint16_t requestedSlot, uint16_t worldCount)
        {
            if (worldCount == 0)
                return 0;
            return std::min<uint16_t>(requestedSlot, static_cast<uint16_t>(worldCount - 1));
        }
    }

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

        EnsurePhysics2DWorldCount(m_Physics2DSettings.WorldCount);
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (!physicsWorld)
                continue;
            if (!physicsWorld->IsInitialized())
                physicsWorld->Initialize(m_Physics2DSettings);
            physicsWorld->SetSettings(m_Physics2DSettings);
            physicsWorld->RebuildScene(*this);
        }
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
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        EnsurePhysics2DWorldCount(m_Physics2DSettings.WorldCount);
        SetRuntimePhase(RuntimePhase::Simulation);
        std::vector<Physics2DWorld*> activeWorlds;
        activeWorlds.reserve(m_Physics2DWorlds.size());
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (!physicsWorld)
                continue;
            if (!physicsWorld->IsInitialized())
                physicsWorld->Initialize(m_Physics2DSettings);
            physicsWorld->SetSettings(m_Physics2DSettings);
            physicsWorld->PrepareForStep(*this, fixedDeltaTime);
            activeWorlds.push_back(physicsWorld.get());
        }

        const bool enableParallelPhysicsWorldStep =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_physics_world_step", true);
        auto& jobSystem = Concurrency::GetJobSystem();
        if (enableParallelPhysicsWorldStep && jobSystem.IsInitialized() && activeWorlds.size() > 1)
        {
            jobSystem.ParallelFor(0, activeWorlds.size(), 1, [&activeWorlds, fixedDeltaTime](size_t worldIndex) {
                activeWorlds[worldIndex]->StepWorldOnly(fixedDeltaTime);
            });
        }
        else
        {
            for (Physics2DWorld* physicsWorld : activeWorlds)
                physicsWorld->StepWorldOnly(fixedDeltaTime);
        }

        for (Physics2DWorld* physicsWorld : activeWorlds)
        {
            physicsWorld->SyncAfterStep(*this);
        }
        m_PhysicsWorldInitializedForLoading = true;
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }

    void Scene::SetPhysics2DSettings(const Physics2DWorldSettings& settings)
    {
        m_Physics2DSettings = settings;
        m_Physics2DSettings.WorldCount = SanitizeWorldCount(m_Physics2DSettings.WorldCount);
        EnsurePhysics2DWorldCount(m_Physics2DSettings.WorldCount);
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->SetSettings(m_Physics2DSettings);
        }
    }

    Physics2DWorld* Scene::GetPhysics2DWorld(uint16_t worldSlot)
    {
        if (m_Physics2DWorlds.empty())
            return nullptr;
        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        return m_Physics2DWorlds[clampedSlot].get();
    }

    const Physics2DWorld* Scene::GetPhysics2DWorld(uint16_t worldSlot) const
    {
        if (m_Physics2DWorlds.empty())
            return nullptr;
        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        return m_Physics2DWorlds[clampedSlot].get();
    }

    uint16_t Scene::GetPhysics2DWorldCount() const
    {
        if (!m_Physics2DWorlds.empty())
            return static_cast<uint16_t>(m_Physics2DWorlds.size());
        return SanitizeWorldCount(m_Physics2DSettings.WorldCount);
    }

    const Physics2DContactListener* Scene::GetPhysics2DContactEvents() const
    {
        const Physics2DWorld* world = GetPhysics2DWorld(0);
        if (!world)
            return nullptr;
        return &world->GetContactListener();
    }

    const Physics2DContactListener* Scene::GetPhysics2DContactEventsForEntity(entt::entity entity) const
    {
        if (entity == entt::null)
            return GetPhysics2DContactEvents();

        const auto& registry = GetRegistry();
        const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity);
        if (!rigidbody)
            return GetPhysics2DContactEvents();

        const uint16_t worldCount = GetPhysics2DWorldCount();
        const uint16_t worldSlot = ClampWorldSlot(rigidbody->PhysicsWorldSlot, worldCount);
        const Physics2DWorld* world = GetPhysics2DWorld(worldSlot);
        if (!world)
            return nullptr;
        return &world->GetContactListener();
    }

    bool Scene::TryGetPhysics2DBodyDiagnostics(entt::entity entity, Physics2DBodyDiagnostics& outDiagnostics) const
    {
        outDiagnostics = Physics2DBodyDiagnostics{};
        const auto& registry = GetRegistry();
        if (const auto* rigidbody = registry.try_get<Rigidbody2DComponent>(entity))
        {
            const uint16_t worldCount = GetPhysics2DWorldCount();
            const uint16_t worldSlot = ClampWorldSlot(rigidbody->PhysicsWorldSlot, worldCount);
            if (const Physics2DWorld* world = GetPhysics2DWorld(worldSlot))
                return world->TryGetBodyDiagnostics(entity, outDiagnostics);
            return false;
        }

        for (const auto& world : m_Physics2DWorlds)
        {
            if (world && world->TryGetBodyDiagnostics(entity, outDiagnostics))
                return true;
        }
        return false;
    }

    Physics2DRaycastHit Scene::RaycastClosestAcrossPhysicsWorlds(const glm::vec2& origin,
                                                                 const glm::vec2& direction,
                                                                 float maxDistance,
                                                                 uint64_t collisionMask) const
    {
        Physics2DRaycastHit bestHit{};
        float bestFraction = 1.0f;
        for (const auto& world : m_Physics2DWorlds)
        {
            if (!world)
                continue;
            const Physics2DRaycastHit hit = world->RaycastClosest(origin, direction, maxDistance, collisionMask);
            if (!hit.HasHit)
                continue;
            if (!bestHit.HasHit || hit.Fraction < bestFraction)
            {
                bestHit = hit;
                bestFraction = hit.Fraction;
            }
        }
        return bestHit;
    }

    void Scene::ResetPhysicsRuntimeState()
    {
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->Shutdown(*this);
        }
    }

    void Scene::ResetPhysicsRuntimeState(uint16_t worldSlot)
    {
        if (m_Physics2DWorlds.empty())
            return;

        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        auto& physicsWorld = m_Physics2DWorlds[clampedSlot];
        if (physicsWorld)
            physicsWorld->Shutdown(*this);
    }

    void Scene::EnsurePhysics2DWorldCount(uint16_t worldCount)
    {
        const uint16_t sanitizedWorldCount = SanitizeWorldCount(worldCount);
        m_Physics2DSettings.WorldCount = sanitizedWorldCount;

        const size_t existingCount = m_Physics2DWorlds.size();
        if (existingCount > sanitizedWorldCount)
        {
            for (size_t worldIndex = sanitizedWorldCount; worldIndex < existingCount; ++worldIndex)
            {
                if (m_Physics2DWorlds[worldIndex])
                    m_Physics2DWorlds[worldIndex]->Shutdown(*this);
            }
            m_Physics2DWorlds.resize(sanitizedWorldCount);
        }
        else if (existingCount < sanitizedWorldCount)
        {
            m_Physics2DWorlds.resize(sanitizedWorldCount);
        }

        for (uint16_t worldIndex = 0; worldIndex < sanitizedWorldCount; ++worldIndex)
        {
            if (!m_Physics2DWorlds[worldIndex])
                m_Physics2DWorlds[worldIndex] = std::make_unique<Physics2DWorld>(worldIndex);
        }

        auto bodyView = m_Registry.view<Rigidbody2DComponent>();
        const uint16_t maxSlot = static_cast<uint16_t>(sanitizedWorldCount - 1);
        for (entt::entity entity : bodyView)
        {
            auto& rigidbody = bodyView.get<Rigidbody2DComponent>(entity);
            rigidbody.PhysicsWorldSlot = std::min<uint16_t>(rigidbody.PhysicsWorldSlot, maxSlot);
        }
    }
}
