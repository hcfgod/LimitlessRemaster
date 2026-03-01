#include "Scene/Scene.h"

#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"
#include "Scripting/ScriptableEntity.h"

#include <algorithm>
#include <exception>
#include <unordered_map>
#include <unordered_set>

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

        RuntimeContactPairKey MakeContactPairKey(entt::entity entityA, entt::entity entityB, bool isSensor)
        {
            const uint32_t encodedA = static_cast<uint32_t>(entityA);
            const uint32_t encodedB = static_cast<uint32_t>(entityB);
            RuntimeContactPairKey key{};
            key.EntityA = std::min(encodedA, encodedB);
            key.EntityB = std::max(encodedA, encodedB);
            key.IsSensor = isSensor;
            return key;
        }

        void DispatchScriptContactCallbackForEntity(Scene& scene,
                                                    entt::entity selfEntity,
                                                    entt::entity otherEntity,
                                                    bool isSensor,
                                                    bool dispatchEnter,
                                                    bool dispatchStay,
                                                    bool dispatchExit)
        {
            if (!scene.IsValid(selfEntity) || !scene.IsValid(otherEntity))
                return;
            if (!scene.IsEntityEnabledInHierarchy(selfEntity))
                return;

            auto& registry = scene.GetRegistry();
            auto* nativeScriptComponent = registry.try_get<NativeScriptComponent>(selfEntity);
            if (!nativeScriptComponent)
                return;

            const Entity other(&registry, otherEntity);
            for (auto& scriptEntry : nativeScriptComponent->Scripts)
            {
                if (!scriptEntry.Enabled || !scriptEntry.RuntimeInstance || !scriptEntry.RuntimeInitialized)
                    continue;

                const auto* tag = registry.try_get<TagComponent>(selfEntity);
                try
                {
                    if (isSensor)
                    {
                        if (dispatchEnter)
                            scriptEntry.RuntimeInstance->DispatchTriggerEnter(other);
                        if (dispatchStay)
                            scriptEntry.RuntimeInstance->DispatchTriggerStay(other);
                        if (dispatchExit)
                            scriptEntry.RuntimeInstance->DispatchTriggerExit(other);
                    }
                    else
                    {
                        if (dispatchEnter)
                            scriptEntry.RuntimeInstance->DispatchCollisionEnter(other);
                        if (dispatchStay)
                            scriptEntry.RuntimeInstance->DispatchCollisionStay(other);
                        if (dispatchExit)
                            scriptEntry.RuntimeInstance->DispatchCollisionExit(other);
                    }
                }
                catch (const std::exception& exception)
                {
                    LT_WARN("Script '{}' on entity '{}' threw during {} callback: {}",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            isSensor ? "trigger" : "collision",
                            exception.what());
                }
                catch (...)
                {
                    LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during {} callback",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            isSensor ? "trigger" : "collision");
                }
            }
        }
    }

    void Scene::BeginLoadingState()
    {
        m_LoadState = LoadState::Loading;
        m_SceneObjectsInitialized = false;
        m_PhysicsWorldInitializedForLoading = false;
        m_RuntimeActiveContactPairs.clear();
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

        // Dispatch Unity-style collision/trigger callbacks for runtime scripts.
        // This keeps contact state persistent across steps for Enter/Stay/Exit semantics.
        if (!m_RuntimeActiveContactPairs.empty())
        {
            for (auto it = m_RuntimeActiveContactPairs.begin(); it != m_RuntimeActiveContactPairs.end();)
            {
                const entt::entity entityA = static_cast<entt::entity>(it->EntityA);
                const entt::entity entityB = static_cast<entt::entity>(it->EntityB);
                if (!IsValid(entityA) || !IsValid(entityB))
                {
                    it = m_RuntimeActiveContactPairs.erase(it);
                    continue;
                }
                ++it;
            }
        }

        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> beginPairs;
        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> endPairs;
        for (Physics2DWorld* physicsWorld : activeWorlds)
        {
            if (!physicsWorld)
                continue;

            const auto& events = physicsWorld->GetContactListener().GetEvents();
            for (const auto& eventData : events)
            {
                if (eventData.EntityA == entt::null || eventData.EntityB == entt::null)
                    continue;
                const RuntimeContactPairKey key = MakeContactPairKey(eventData.EntityA, eventData.EntityB, eventData.IsSensor);
                if (eventData.IsBegin)
                    beginPairs.insert(key);
                else
                    endPairs.insert(key);
            }
        }

        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> changedPairs = beginPairs;
        changedPairs.insert(endPairs.begin(), endPairs.end());
        m_ForceDeferredEntityDestruction = true;
        for (const RuntimeContactPairKey& key : changedPairs)
        {
            const bool hasBegin = beginPairs.find(key) != beginPairs.end();
            const bool hasEnd = endPairs.find(key) != endPairs.end();
            const bool wasActive = m_RuntimeActiveContactPairs.find(key) != m_RuntimeActiveContactPairs.end();

            const entt::entity entityA = static_cast<entt::entity>(key.EntityA);
            const entt::entity entityB = static_cast<entt::entity>(key.EntityB);

            if (!wasActive)
            {
                if (hasBegin && !hasEnd)
                {
                    m_RuntimeActiveContactPairs.insert(key);
                    DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, true, false, false);
                    DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, true, false, false);
                }
                continue;
            }

            if (hasEnd && !hasBegin)
            {
                DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, false, false, true);
                DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, false, false, true);
                m_RuntimeActiveContactPairs.erase(key);
            }
        }

        for (const RuntimeContactPairKey& key : m_RuntimeActiveContactPairs)
        {
            const entt::entity entityA = static_cast<entt::entity>(key.EntityA);
            const entt::entity entityB = static_cast<entt::entity>(key.EntityB);
            DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, false, true, false);
            DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, false, true, false);
        }
        m_ForceDeferredEntityDestruction = false;
        FlushDeferredStructuralMutations();

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
        m_RuntimeActiveContactPairs.clear();
        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->Shutdown(*this);
        }
    }

    void Scene::ResetPhysicsRuntimeState(uint16_t worldSlot)
    {
        m_RuntimeActiveContactPairs.clear();
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
