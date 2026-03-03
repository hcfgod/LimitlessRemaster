#include "Scene/Scene.h"

#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"
#include "Scripting/ScriptableEntity.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <fstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

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

        RuntimeContactPairKey MakeContactPairKey(entt::entity entityA, entt::entity entityB, uint16_t worldSlot, bool isSensor)
        {
            const uint32_t encodedA = static_cast<uint32_t>(entityA);
            const uint32_t encodedB = static_cast<uint32_t>(entityB);
            RuntimeContactPairKey key{};
            key.EntityA = std::min(encodedA, encodedB);
            key.EntityB = std::max(encodedA, encodedB);
            key.WorldSlot = worldSlot;
            key.IsSensor = isSensor;
            return key;
        }

        bool RuntimeContactPairSortLess(const RuntimeContactPairKey& left, const RuntimeContactPairKey& right)
        {
            return std::tie(left.WorldSlot, left.IsSensor, left.EntityA, left.EntityB) <
                   std::tie(right.WorldSlot, right.IsSensor, right.EntityA, right.EntityB);
        }

        int64_t AgentDebugTimestampMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        }

        void AgentAppendDebugLog(std::string_view runId,
                                 std::string_view hypothesisId,
                                 std::string_view location,
                                 std::string_view message,
                                 const nlohmann::json& data)
        {
            try
            {
                const nlohmann::json payload{
                    { "sessionId", "050170" },
                    { "runId", runId },
                    { "hypothesisId", hypothesisId },
                    { "location", location },
                    { "message", message },
                    { "data", data },
                    { "timestamp", AgentDebugTimestampMs() }
                };
                std::ofstream out("debug-050170.log", std::ios::app);
                if (out.is_open())
                    out << payload.dump() << '\n';
            }
            catch (...)
            {
            }
        }

        bool IsContactOrderDebugEntity(const Scene& scene, entt::entity entity)
        {
            if (entity == entt::null || !scene.IsValid(entity))
                return false;

            const auto& registry = scene.GetRegistry();
            const auto* tag = registry.try_get<TagComponent>(entity);
            if (!tag)
                return false;

            return tag->Tag == "TriggerA" || tag->Tag == "TriggerB" || tag->Tag == "Visitor";
        }

        bool IsContactOrderDebugScene(const Scene& scene)
        {
            const auto& registry = scene.GetRegistry();
            auto tagView = registry.view<TagComponent>();
            bool hasTriggerA = false;
            bool hasTriggerB = false;
            bool hasVisitor = false;
            for (entt::entity entity : tagView)
            {
                const std::string& tag = tagView.get<TagComponent>(entity).Tag;
                if (tag == "TriggerA")
                    hasTriggerA = true;
                else if (tag == "TriggerB")
                    hasTriggerB = true;
                else if (tag == "Visitor")
                    hasVisitor = true;
            }
            return hasTriggerA && hasTriggerB && hasVisitor;
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

            if (IsContactOrderDebugEntity(scene, selfEntity))
            {
                size_t enabledScriptCount = 0;
                size_t runtimeInitializedScriptCount = 0;
                size_t runtimeInstanceScriptCount = 0;
                for (const auto& scriptEntry : nativeScriptComponent->Scripts)
                {
                    if (scriptEntry.Enabled)
                        ++enabledScriptCount;
                    if (scriptEntry.RuntimeInitialized)
                        ++runtimeInitializedScriptCount;
                    if (scriptEntry.RuntimeInstance)
                        ++runtimeInstanceScriptCount;
                }

                const auto* selfTag = registry.try_get<TagComponent>(selfEntity);
                const auto* otherTag = registry.try_get<TagComponent>(otherEntity);
                // #region agent log
                AgentAppendDebugLog(
                    "contact-order-initial",
                    "H5",
                    "Limitless/Source/Scene/SceneRuntimeLifecycle.cpp:139",
                    "Dispatch gate state for contact callback entity",
                    nlohmann::json{
                        { "selfEntity", static_cast<uint32_t>(selfEntity) },
                        { "otherEntity", static_cast<uint32_t>(otherEntity) },
                        { "selfTag", selfTag ? selfTag->Tag : std::string("Entity") },
                        { "otherTag", otherTag ? otherTag->Tag : std::string("Entity") },
                        { "dispatchEnter", dispatchEnter },
                        { "dispatchStay", dispatchStay },
                        { "dispatchExit", dispatchExit },
                        { "isSensor", isSensor },
                        { "scriptSlots", nativeScriptComponent->Scripts.size() },
                        { "enabledScriptCount", enabledScriptCount },
                        { "runtimeInitializedScriptCount", runtimeInitializedScriptCount },
                        { "runtimeInstanceScriptCount", runtimeInstanceScriptCount }
                    });
                // #endregion
            }

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
        size_t rawEventCount = 0;
        size_t rawBeginCount = 0;
        size_t rawSensorBeginCount = 0;
        size_t rawEndCount = 0;
        size_t rawSensorEndCount = 0;
        for (Physics2DWorld* physicsWorld : activeWorlds)
        {
            if (!physicsWorld)
                continue;

            const uint16_t worldSlot = physicsWorld->GetSceneWorldSlot();
            const auto& events = physicsWorld->GetContactListener().GetEvents();
            for (const auto& eventData : events)
            {
                ++rawEventCount;
                if (eventData.IsBegin)
                {
                    ++rawBeginCount;
                    if (eventData.IsSensor)
                        ++rawSensorBeginCount;
                }
                else
                {
                    ++rawEndCount;
                    if (eventData.IsSensor)
                        ++rawSensorEndCount;
                }
                if (eventData.EntityA == entt::null || eventData.EntityB == entt::null)
                    continue;
                const RuntimeContactPairKey key = MakeContactPairKey(eventData.EntityA, eventData.EntityB, worldSlot, eventData.IsSensor);
                if (eventData.IsBegin)
                    beginPairs.insert(key);
                else
                    endPairs.insert(key);
            }
        }

        std::unordered_set<RuntimeContactPairKey, RuntimeContactPairKeyHasher> changedPairs = beginPairs;
        changedPairs.insert(endPairs.begin(), endPairs.end());
        std::vector<RuntimeContactPairKey> orderedChangedPairs(changedPairs.begin(), changedPairs.end());
        std::sort(orderedChangedPairs.begin(), orderedChangedPairs.end(), RuntimeContactPairSortLess);
        if (IsContactOrderDebugScene(*this))
        {
            // #region agent log
            AgentAppendDebugLog(
                "contact-order-initial",
                "H3",
                "Limitless/Source/Scene/SceneRuntimeLifecycle.cpp:325",
                "StepPhysics2D pair extraction state",
                nlohmann::json{
                    { "rawEventCount", rawEventCount },
                    { "rawBeginCount", rawBeginCount },
                    { "rawSensorBeginCount", rawSensorBeginCount },
                    { "rawEndCount", rawEndCount },
                    { "rawSensorEndCount", rawSensorEndCount },
                    { "beginPairs", beginPairs.size() },
                    { "endPairs", endPairs.size() },
                    { "changedPairs", orderedChangedPairs.size() },
                    { "activePairsBeforeDispatch", m_RuntimeActiveContactPairs.size() }
                });
            // #endregion
        }

        [[maybe_unused]] auto forcedDeferredDestroyScope = MakeForcedDeferredEntityDestructionScope();
        size_t enterDispatchCalls = 0;
        size_t exitDispatchCalls = 0;
        size_t stayDispatchCalls = 0;
        for (const RuntimeContactPairKey& key : orderedChangedPairs)
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
                    enterDispatchCalls += 2;
                }
                continue;
            }

            if (hasEnd && !hasBegin)
            {
                DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, false, false, true);
                DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, false, false, true);
                exitDispatchCalls += 2;
                m_RuntimeActiveContactPairs.erase(key);
            }
        }

        std::vector<RuntimeContactPairKey> orderedActivePairs(m_RuntimeActiveContactPairs.begin(), m_RuntimeActiveContactPairs.end());
        std::sort(orderedActivePairs.begin(), orderedActivePairs.end(), RuntimeContactPairSortLess);
        for (const RuntimeContactPairKey& key : orderedActivePairs)
        {
            const entt::entity entityA = static_cast<entt::entity>(key.EntityA);
            const entt::entity entityB = static_cast<entt::entity>(key.EntityB);
            DispatchScriptContactCallbackForEntity(*this, entityA, entityB, key.IsSensor, false, true, false);
            DispatchScriptContactCallbackForEntity(*this, entityB, entityA, key.IsSensor, false, true, false);
            stayDispatchCalls += 2;
        }
        if (IsContactOrderDebugScene(*this))
        {
            // #region agent log
            AgentAppendDebugLog(
                "contact-order-initial",
                "H4",
                "Limitless/Source/Scene/SceneRuntimeLifecycle.cpp:390",
                "StepPhysics2D dispatch outcome state",
                nlohmann::json{
                    { "enterDispatchCalls", enterDispatchCalls },
                    { "stayDispatchCalls", stayDispatchCalls },
                    { "exitDispatchCalls", exitDispatchCalls },
                    { "activePairsAfterDispatch", m_RuntimeActiveContactPairs.size() }
                });
            // #endregion
        }
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
        if (m_Physics2DWorlds.empty())
            return;

        const uint16_t clampedSlot = ClampWorldSlot(worldSlot, static_cast<uint16_t>(m_Physics2DWorlds.size()));
        for (auto it = m_RuntimeActiveContactPairs.begin(); it != m_RuntimeActiveContactPairs.end();)
        {
            if (it->WorldSlot == clampedSlot)
                it = m_RuntimeActiveContactPairs.erase(it);
            else
                ++it;
        }

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
