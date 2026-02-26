#include "Scene/Scene.h"

#include "Core/Application.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ConfigManager.h"
#include "Physics/Physics2DQueries.h"
#include "Platform/Window.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scene/SceneSystemScheduler.h"
#include "Scripting/Coroutine.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>
#include <vector>

namespace Limitless
{
    std::string ResolveRegisteredScriptClassNameForSceneRuntime(const std::string& requestedClassName,
                                                                const std::string& scriptAssetRelativePath);
    void ProcessUiInteractionSystemForSceneRuntime(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);
    void UpdateAnimation2DSystemForSceneRuntime(Scene& scene, float deltaTime, uint64_t dispatchFrame);

    void Scene::Update(float deltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            ProcessUiInteractionSystemForSceneRuntime(*this, window.GetWidth(), window.GetHeight());
        }
        else
        {
            ProcessUiInteractionSystemForSceneRuntime(*this, 0, 0);
        }

        auto runScheduledSimulationSystems = [&]() {
            SetRuntimePhase(RuntimePhase::Simulation);
            const bool enableSystemScheduler = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_system_scheduler", true);
            if (!enableSystemScheduler)
            {
                UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
                UpdateParticleEmitterSystem(m_Registry, deltaTime);
                return;
            }

            std::vector<ScheduledSceneSystem> scheduledSystems;
            ScheduledSceneSystem animationSystem{};
            animationSystem.Name = "Animation2D";
            animationSystem.Access.Reads = ToAccessMask(SceneSystemAccessComponent::Animator);
            animationSystem.Access.Writes = ToAccessMask(SceneSystemAccessComponent::Animator) |
                                           ToAccessMask(SceneSystemAccessComponent::Transform);
            animationSystem.Execute = [this, deltaTime]() {
                UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
            };
            animationSystem.AllowParallel = true;
            scheduledSystems.push_back(std::move(animationSystem));

            ScheduledSceneSystem particleSystem{};
            particleSystem.Name = "ParticleEmitter";
            particleSystem.Access.Reads = ToAccessMask(SceneSystemAccessComponent::Transform);
            particleSystem.Access.Writes = ToAccessMask(SceneSystemAccessComponent::ParticleEmitter);
            particleSystem.Execute = [this, deltaTime]() {
                UpdateParticleEmitterSystem(m_Registry, deltaTime);
            };
            particleSystem.AllowParallel = true;
            scheduledSystems.push_back(std::move(particleSystem));
            SceneSystemScheduler::Run(Concurrency::GetJobSystem(), scheduledSystems);
        };

        if (NativeScriptRegistry::IsExecutionBlocked())
        {
            SetRuntimePhase(RuntimePhase::ScriptMainThread);
            auto scriptView = m_Registry.view<NativeScriptComponent>();
            for (entt::entity entity : scriptView)
            {
                auto& nativeScript = scriptView.get<NativeScriptComponent>(entity);
                for (auto& scriptEntry : nativeScript.Scripts)
                {
                    if (scriptEntry.RuntimeInstance)
                    {
                        if (scriptEntry.RuntimeInitialized)
                        {
                            try
                            {
                                scriptEntry.RuntimeInstance->OnDestroy();
                            }
                            catch (...)
                            {
                            }
                        }

                        try
                        {
                            Coroutine::StopAll(*scriptEntry.RuntimeInstance);
                        }
                        catch (...)
                        {
                        }
                        scriptEntry.RuntimeInstance.reset();
                    }

                    scriptEntry.RuntimeInitialized = false;
                    scriptEntry.RuntimeUpdateCount = 0;
                    scriptEntry.RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry.RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = false;
                }
            }

            runScheduledSimulationSystems();
            SetRuntimePhase(RuntimePhase::Transform);
            UpdateTransforms();
            SetRuntimePhase(RuntimePhase::Idle);
            return;
        }

        std::vector<std::pair<entt::entity, size_t>> scriptSlots;
        {
            auto snapshotView = m_Registry.view<NativeScriptComponent>();
            for (entt::entity entity : snapshotView)
            {
                const auto& nativeScript = snapshotView.get<NativeScriptComponent>(entity);
                for (size_t scriptIndex = 0; scriptIndex < nativeScript.Scripts.size(); ++scriptIndex)
                    scriptSlots.emplace_back(entity, scriptIndex);
            }
        }

        auto tryGetScriptEntry = [&](entt::entity scriptEntity, size_t scriptIndex) -> NativeScriptEntry* {
            auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(scriptEntity);
            if (!nativeScript || scriptIndex >= nativeScript->Scripts.size())
                return nullptr;
            return &nativeScript->Scripts[scriptIndex];
        };

        auto handleScriptCallbackFailure = [&](entt::entity scriptEntity,
                                               size_t scriptIndex,
                                               std::string_view callbackName,
                                               const char* message) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(scriptEntity, scriptIndex);
            const auto* tag = m_Registry.try_get<TagComponent>(scriptEntity);
            LT_ERROR("Script '{}' on entity '{}' failed during {}: {}",
                     scriptEntry ? scriptEntry->ScriptClassName : "<unknown>",
                     tag ? tag->Tag : "Entity",
                     callbackName,
                     message ? message : "unknown error");

            if (!scriptEntry)
                return;

            if (scriptEntry->RuntimeInstance)
            {
                try
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                }
                catch (...)
                {
                }
            }
            scriptEntry->RuntimeInstance.reset();
            scriptEntry->RuntimeInitialized = false;
            scriptEntry->RuntimeUpdateCount = 0;
            scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
        };

        auto executeScriptUpdateSlot = [&](entt::entity entity, size_t scriptIndex) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;

            TransformComponent transformBeforeUpdate{};
            bool hadTransformBeforeUpdate = false;
            bool trackTransformMutation = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeUpdate = *transform;
                hadTransformBeforeUpdate = true;
                if (const auto* rigidbody2D = m_Registry.try_get<Rigidbody2DComponent>(entity))
                {
                    trackTransformMutation = rigidbody2D->Type == Rigidbody2DComponent::BodyType::Dynamic ||
                                            rigidbody2D->Type == Rigidbody2DComponent::BodyType::Kinematic;
                }
            }

            try
            {
                scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry->RuntimeInstance->OnUpdate(deltaTime);
                Coroutine::TickOwner(*scriptEntry->RuntimeInstance, deltaTime);
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", exception.what());
                return;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", "non-standard exception");
                return;
            }

            scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;
            ++scriptEntry->RuntimeUpdateCount;

            constexpr float kTransformDirtyEpsilon = 0.0001f;
            const auto* transformAfterUpdate = m_Registry.try_get<TransformComponent>(entity);
            if (hadTransformBeforeUpdate && transformAfterUpdate)
            {
                const bool positionChanged = glm::length(transformAfterUpdate->Position - transformBeforeUpdate.Position) > kTransformDirtyEpsilon;
                const bool rotationChanged = glm::length(transformAfterUpdate->Rotation - transformBeforeUpdate.Rotation) > kTransformDirtyEpsilon;
                const bool scaleChanged = glm::length(transformAfterUpdate->Scale - transformBeforeUpdate.Scale) > kTransformDirtyEpsilon;
                if (positionChanged || rotationChanged || scaleChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeUpdate && transformAfterUpdate)
            {
                MarkTransformDirty(entity);
            }

            if (trackTransformMutation && !scriptEntry->RuntimeWarnedOnUpdateTransformMutation)
            {
                if (transformAfterUpdate)
                {
                    const bool positionChanged = glm::length(transformAfterUpdate->Position - transformBeforeUpdate.Position) > kTransformDirtyEpsilon;
                    const bool rotationChanged = glm::length(transformAfterUpdate->Rotation - transformBeforeUpdate.Rotation) > kTransformDirtyEpsilon;
                    const bool scaleChanged = glm::length(transformAfterUpdate->Scale - transformBeforeUpdate.Scale) > kTransformDirtyEpsilon;
                    if (positionChanged || rotationChanged || scaleChanged)
                    {
                        const auto* tag = m_Registry.try_get<TagComponent>(entity);
                        LT_WARN("Script '{}' on entity '{}' is mutating Transform in OnUpdate while Rigidbody2D is Dynamic/Kinematic. Move physics-related transform writes to OnFixedUpdate for stable simulation.",
                                scriptEntry->ScriptClassName,
                                tag ? tag->Tag : "Entity");
                        scriptEntry->RuntimeWarnedOnUpdateTransformMutation = true;
                    }
                }
            }
        };

        // Two-phase runtime bootstrapping:
        // 1) Create all script instances for currently active slots.
        // 2) Invoke OnCreate/OnUpdate in slot order.
        // This lets scripts safely reference sibling scripts during OnCreate,
        // even when the referenced script appears later in the list.
        std::vector<std::pair<entt::entity, size_t>> parallelScriptSlots;
        parallelScriptSlots.reserve(scriptSlots.size());
        const bool enableParallelScripts = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool requireParallelScriptAccessDeclarations =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool warnOnImplicitParallelScriptAccess =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        auto tryQueueParallelScriptSlot = [&](entt::entity entity, size_t scriptIndex, NativeScriptEntry& scriptEntry) {
            if (!(enableParallelScripts && scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe))
                return false;

            const bool hasAccessDeclaration = scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0;
            if (!hasAccessDeclaration)
            {
                if (warnOnImplicitParallelScriptAccess && !scriptEntry.RuntimeWarnedMissingAccessDeclaration)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("ParallelSafe script '{}' on entity '{}' has no declared component access mask and will {}. Author DeclaredReadAccessMask/DeclaredWriteAccessMask for deterministic scheduling.",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            requireParallelScriptAccessDeclarations ? "run on the main thread this frame" : "use conservative scheduler barriers");
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = true;
                }

                if (requireParallelScriptAccessDeclarations)
                    return false;
            }

            parallelScriptSlots.emplace_back(entity, scriptIndex);
            return true;
        };
        SetRuntimePhase(RuntimePhase::ScriptMainThread);
        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                continue;

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                }
            }

            if (scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance->m_Scene = this;
                scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
            }
        }

        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
            {
                if (scriptEntry->RuntimeInstance)
                {
                    if (scriptEntry->RuntimeInitialized)
                    {
                        try
                        {
                            scriptEntry->RuntimeInstance->OnDestroy();
                        }
                        catch (const std::exception& exception)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", exception.what());
                        }
                        catch (...)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", "non-standard exception");
                        }
                    }
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry)
                    continue;

                if (scriptEntry->RuntimeInstance)
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                    scriptEntry->RuntimeInstance.reset();
                }
                scriptEntry->RuntimeInitialized = false;
                scriptEntry->RuntimeUpdateCount = 0;
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                continue;
            }

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                }
            }

            if (!scriptEntry->RuntimeInstance)
            {
                if (!scriptEntry->RuntimeWarnedMissingCompiledScript)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingCompiledScript = true;
                }
                continue;
            }

            // Rebind runtime context every frame. NativeScriptEntry objects can move in memory
            // when the scripts vector grows/reorders, so cached pointers must be refreshed.
            scriptEntry->RuntimeInstance->m_Scene = this;
            scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
            scriptEntry->RuntimeInstance->m_EntityHandle = entity;
            scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;

            if (!scriptEntry->RuntimeInitialized)
            {
                try
                {
                    scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry->RuntimeInstance->OnCreate();
                }
                catch (const std::exception& exception)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", exception.what());
                    continue;
                }
                catch (...)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", "non-standard exception");
                    continue;
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry || !scriptEntry->RuntimeInstance)
                    continue;
                scriptEntry->RuntimeInitialized = true;
            }

            if (tryQueueParallelScriptSlot(entity, scriptIndex, *scriptEntry))
            {
                continue;
            }

            executeScriptUpdateSlot(entity, scriptIndex);
        }

        if (!parallelScriptSlots.empty())
        {
            SetRuntimePhase(RuntimePhase::ScriptParallel);
            auto& jobSystem = Concurrency::GetJobSystem();
            auto runParallelSlotAtIndex = [&](size_t slotIndex) {
                struct ScopedParallelScriptThreadFlag final
                {
                    ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(true); }
                    ~ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(false); }
                } scopedThreadFlag;

                const auto& slot = parallelScriptSlots[slotIndex];
                executeScriptUpdateSlot(slot.first, slot.second);
            };

            auto hasAccessHazard = [](const SceneSystemAccess& left, const SceneSystemAccess& right) {
                return (left.Writes & right.Writes) != 0 ||
                       (left.Writes & right.Reads) != 0 ||
                       (left.Reads & right.Writes) != 0;
            };
            auto getScriptAccess = [&](size_t slotIndex) {
                SceneSystemAccess access{};
                const auto& slot = parallelScriptSlots[slotIndex];
                const NativeScriptEntry* scriptEntry = tryGetScriptEntry(slot.first, slot.second);
                if (!scriptEntry)
                {
                    access.Writes = ~0ull;
                    return access;
                }
                access.Reads = scriptEntry->DeclaredReadAccessMask;
                access.Writes = scriptEntry->DeclaredWriteAccessMask;
                if (access.Reads == 0 && access.Writes == 0)
                    access.Writes = ~0ull;
                return access;
            };

            std::vector<size_t> pendingIndices(parallelScriptSlots.size());
            for (size_t index = 0; index < pendingIndices.size(); ++index)
                pendingIndices[index] = index;

            while (!pendingIndices.empty())
            {
                std::vector<size_t> batchIndices;
                std::vector<size_t> nextPendingIndices;
                batchIndices.reserve(pendingIndices.size());
                nextPendingIndices.reserve(pendingIndices.size());

                for (size_t pendingIndex : pendingIndices)
                {
                    const SceneSystemAccess candidateAccess = getScriptAccess(pendingIndex);
                    bool conflicts = false;
                    for (size_t batchIndex : batchIndices)
                    {
                        if (hasAccessHazard(candidateAccess, getScriptAccess(batchIndex)))
                        {
                            conflicts = true;
                            break;
                        }
                    }

                    if (!conflicts)
                        batchIndices.push_back(pendingIndex);
                    else
                        nextPendingIndices.push_back(pendingIndex);
                }

                if (batchIndices.empty())
                {
                    batchIndices.push_back(pendingIndices.front());
                    nextPendingIndices.erase(std::remove(nextPendingIndices.begin(), nextPendingIndices.end(), pendingIndices.front()), nextPendingIndices.end());
                }

                if (jobSystem.IsInitialized() && batchIndices.size() > 1)
                {
                    Concurrency::WaitGroup waitGroup;
                    for (size_t batchIndex : batchIndices)
                    {
                        waitGroup.Add(1);
                        jobSystem.Submit([&runParallelSlotAtIndex, &waitGroup, batchIndex]() {
                            runParallelSlotAtIndex(batchIndex);
                            waitGroup.Done();
                        });
                    }
                    waitGroup.Wait();
                }
                else
                {
                    for (size_t batchIndex : batchIndices)
                        runParallelSlotAtIndex(batchIndex);
                }

                pendingIndices = std::move(nextPendingIndices);
            }
        }

        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        runScheduledSimulationSystems();
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }

    void Scene::FixedUpdate(float fixedDeltaTime)
    {
        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();

        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (NativeScriptRegistry::IsExecutionBlocked())
        {
            SetRuntimePhase(RuntimePhase::Idle);
            return;
        }

        std::vector<std::pair<entt::entity, size_t>> scriptSlots;
        {
            auto snapshotView = m_Registry.view<NativeScriptComponent>();
            for (entt::entity entity : snapshotView)
            {
                const auto& nativeScript = snapshotView.get<NativeScriptComponent>(entity);
                for (size_t scriptIndex = 0; scriptIndex < nativeScript.Scripts.size(); ++scriptIndex)
                    scriptSlots.emplace_back(entity, scriptIndex);
            }
        }

        auto tryGetScriptEntry = [&](entt::entity scriptEntity, size_t scriptIndex) -> NativeScriptEntry* {
            auto* nativeScript = m_Registry.try_get<NativeScriptComponent>(scriptEntity);
            if (!nativeScript || scriptIndex >= nativeScript->Scripts.size())
                return nullptr;
            return &nativeScript->Scripts[scriptIndex];
        };

        auto handleScriptCallbackFailure = [&](entt::entity scriptEntity,
                                               size_t scriptIndex,
                                               std::string_view callbackName,
                                               const char* message) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(scriptEntity, scriptIndex);
            const auto* tag = m_Registry.try_get<TagComponent>(scriptEntity);
            LT_ERROR("Script '{}' on entity '{}' failed during {}: {}",
                     scriptEntry ? scriptEntry->ScriptClassName : "<unknown>",
                     tag ? tag->Tag : "Entity",
                     callbackName,
                     message ? message : "unknown error");

            if (!scriptEntry)
                return;

            if (scriptEntry->RuntimeInstance)
            {
                try
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                }
                catch (...)
                {
                }
            }
            scriptEntry->RuntimeInstance.reset();
            scriptEntry->RuntimeInitialized = false;
            scriptEntry->RuntimeUpdateCount = 0;
            scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
        };

        auto executeScriptFixedUpdateSlot = [&](entt::entity entity, size_t scriptIndex) {
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                return;

            TransformComponent transformBeforeFixedUpdate{};
            bool hadTransformBeforeFixedUpdate = false;
            if (auto* transform = m_Registry.try_get<TransformComponent>(entity))
            {
                transformBeforeFixedUpdate = *transform;
                hadTransformBeforeFixedUpdate = true;
            }

            try
            {
                scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                scriptEntry->RuntimeInstance->OnFixedUpdate(fixedDeltaTime);
            }
            catch (const std::exception& exception)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", exception.what());
                return;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", "non-standard exception");
                return;
            }

            constexpr float kTransformDirtyEpsilon = 0.0001f;
            const auto* transformAfterFixedUpdate = m_Registry.try_get<TransformComponent>(entity);
            if (hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                const bool positionChanged = glm::length(transformAfterFixedUpdate->Position - transformBeforeFixedUpdate.Position) > kTransformDirtyEpsilon;
                const bool rotationChanged = glm::length(transformAfterFixedUpdate->Rotation - transformBeforeFixedUpdate.Rotation) > kTransformDirtyEpsilon;
                const bool scaleChanged = glm::length(transformAfterFixedUpdate->Scale - transformBeforeFixedUpdate.Scale) > kTransformDirtyEpsilon;
                if (positionChanged || rotationChanged || scaleChanged)
                    MarkTransformDirty(entity);
            }
            else if (!hadTransformBeforeFixedUpdate && transformAfterFixedUpdate)
            {
                MarkTransformDirty(entity);
            }
        };

        // Mirror Update() bootstrap so FixedUpdate callbacks can also resolve
        // other scripts during OnCreate regardless of declaration order.
        std::vector<std::pair<entt::entity, size_t>> parallelScriptSlots;
        parallelScriptSlots.reserve(scriptSlots.size());
        const bool enableParallelScripts = ConfigManager::GetInstance().GetValue<bool>("ecs.mt.enable_parallel_scripts", true);
        const bool requireParallelScriptAccessDeclarations =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.require_parallel_script_access_declarations", true);
        const bool warnOnImplicitParallelScriptAccess =
            ConfigManager::GetInstance().GetValue<bool>("ecs.mt.warn_implicit_parallel_script_access", true);
        auto tryQueueParallelScriptSlot = [&](entt::entity entity, size_t scriptIndex, NativeScriptEntry& scriptEntry) {
            if (!(enableParallelScripts && scriptEntry.ExecutionPolicy == ScriptExecutionPolicy::ParallelSafe))
                return false;

            const bool hasAccessDeclaration = scriptEntry.DeclaredReadAccessMask != 0 || scriptEntry.DeclaredWriteAccessMask != 0;
            if (!hasAccessDeclaration)
            {
                if (warnOnImplicitParallelScriptAccess && !scriptEntry.RuntimeWarnedMissingAccessDeclaration)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("ParallelSafe script '{}' on entity '{}' has no declared component access mask and will {}. Author DeclaredReadAccessMask/DeclaredWriteAccessMask for deterministic scheduling.",
                            scriptEntry.ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            requireParallelScriptAccessDeclarations ? "run on the main thread this frame" : "use conservative scheduler barriers");
                    scriptEntry.RuntimeWarnedMissingAccessDeclaration = true;
                }

                if (requireParallelScriptAccessDeclarations)
                    return false;
            }

            parallelScriptSlots.emplace_back(entity, scriptIndex);
            return true;
        };
        SetRuntimePhase(RuntimePhase::ScriptMainThread);
        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
                continue;

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                }
            }

            if (scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance->m_Scene = this;
                scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
            }
        }

        for (const auto& scriptSlot : scriptSlots)
        {
            const entt::entity entity = scriptSlot.first;
            const size_t scriptIndex = scriptSlot.second;
            NativeScriptEntry* scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry)
                continue;

            if (!scriptEntry->Enabled || scriptEntry->ScriptClassName.empty() || !IsEntityEnabledInHierarchy(entity))
            {
                if (scriptEntry->RuntimeInstance)
                {
                    if (scriptEntry->RuntimeInitialized)
                    {
                        try
                        {
                            scriptEntry->RuntimeInstance->OnDestroy();
                        }
                        catch (const std::exception& exception)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", exception.what());
                        }
                        catch (...)
                        {
                            handleScriptCallbackFailure(entity, scriptIndex, "OnDestroy", "non-standard exception");
                        }
                    }
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry)
                    continue;

                if (scriptEntry->RuntimeInstance)
                {
                    Coroutine::StopAll(*scriptEntry->RuntimeInstance);
                    scriptEntry->RuntimeInstance.reset();
                }
                scriptEntry->RuntimeInitialized = false;
                scriptEntry->RuntimeUpdateCount = 0;
                scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                continue;
            }

            if (!scriptEntry->RuntimeInstance)
            {
                scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                if (!scriptEntry->RuntimeInstance)
                {
                    const std::string resolvedClassName = ResolveRegisteredScriptClassNameForSceneRuntime(scriptEntry->ScriptClassName,
                                                                                                           scriptEntry->ScriptAssetRelativePath);
                    if (!resolvedClassName.empty())
                    {
                        scriptEntry->ScriptClassName = resolvedClassName;
                        scriptEntry->RuntimeInstance = NativeScriptRegistry::CreateScript(scriptEntry->ScriptClassName);
                    }
                }
                if (scriptEntry->RuntimeInstance)
                {
                    scriptEntry->RuntimeInstance->m_Scene = this;
                    scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
                    scriptEntry->RuntimeInstance->m_EntityHandle = entity;
                    scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;
                    scriptEntry->RuntimeInitialized = false;
                    scriptEntry->RuntimeUpdateCount = 0;
                    scriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                    scriptEntry->RuntimeWarnedMissingCompiledScript = false;
                    scriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                }
            }

            if (!scriptEntry->RuntimeInstance)
            {
                if (!scriptEntry->RuntimeWarnedMissingCompiledScript)
                {
                    const auto* tag = m_Registry.try_get<TagComponent>(entity);
                    LT_WARN("Script '{}' on entity '{}' is not compiled/registered in ScriptCore. Build project scripts before entering Play Mode.",
                            scriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                    scriptEntry->RuntimeWarnedMissingCompiledScript = true;
                }
                continue;
            }

            scriptEntry->RuntimeInstance->m_Scene = this;
            scriptEntry->RuntimeInstance->m_Registry = &m_Registry;
            scriptEntry->RuntimeInstance->m_EntityHandle = entity;
            scriptEntry->RuntimeInstance->m_ExposedProperties = &scriptEntry->ExposedProperties;

            if (!scriptEntry->RuntimeInitialized)
            {
                try
                {
                    scriptEntry->RuntimeInstance->OnSynchronizeExposedFields();
                    scriptEntry->RuntimeInstance->OnCreate();
                }
                catch (const std::exception& exception)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", exception.what());
                    continue;
                }
                catch (...)
                {
                    handleScriptCallbackFailure(entity, scriptIndex, "OnCreate", "non-standard exception");
                    continue;
                }

                scriptEntry = tryGetScriptEntry(entity, scriptIndex);
                if (!scriptEntry || !scriptEntry->RuntimeInstance)
                    continue;
                scriptEntry->RuntimeInitialized = true;
            }

            if (tryQueueParallelScriptSlot(entity, scriptIndex, *scriptEntry))
            {
                continue;
            }

            executeScriptFixedUpdateSlot(entity, scriptIndex);
        }

        if (!parallelScriptSlots.empty())
        {
            SetRuntimePhase(RuntimePhase::ScriptParallel);
            auto& jobSystem = Concurrency::GetJobSystem();
            auto runParallelSlotAtIndex = [&](size_t slotIndex) {
                struct ScopedParallelScriptThreadFlag final
                {
                    ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(true); }
                    ~ScopedParallelScriptThreadFlag() { Scene::SetCurrentThreadParallelScriptExecution(false); }
                } scopedThreadFlag;

                const auto& slot = parallelScriptSlots[slotIndex];
                executeScriptFixedUpdateSlot(slot.first, slot.second);
            };

            auto hasAccessHazard = [](const SceneSystemAccess& left, const SceneSystemAccess& right) {
                return (left.Writes & right.Writes) != 0 ||
                       (left.Writes & right.Reads) != 0 ||
                       (left.Reads & right.Writes) != 0;
            };
            auto getScriptAccess = [&](size_t slotIndex) {
                SceneSystemAccess access{};
                const auto& slot = parallelScriptSlots[slotIndex];
                const NativeScriptEntry* scriptEntry = tryGetScriptEntry(slot.first, slot.second);
                if (!scriptEntry)
                {
                    access.Writes = ~0ull;
                    return access;
                }
                access.Reads = scriptEntry->DeclaredReadAccessMask;
                access.Writes = scriptEntry->DeclaredWriteAccessMask;
                if (access.Reads == 0 && access.Writes == 0)
                    access.Writes = ~0ull;
                return access;
            };

            std::vector<size_t> pendingIndices(parallelScriptSlots.size());
            for (size_t index = 0; index < pendingIndices.size(); ++index)
                pendingIndices[index] = index;

            while (!pendingIndices.empty())
            {
                std::vector<size_t> batchIndices;
                std::vector<size_t> nextPendingIndices;
                batchIndices.reserve(pendingIndices.size());
                nextPendingIndices.reserve(pendingIndices.size());

                for (size_t pendingIndex : pendingIndices)
                {
                    const SceneSystemAccess candidateAccess = getScriptAccess(pendingIndex);
                    bool conflicts = false;
                    for (size_t batchIndex : batchIndices)
                    {
                        if (hasAccessHazard(candidateAccess, getScriptAccess(batchIndex)))
                        {
                            conflicts = true;
                            break;
                        }
                    }

                    if (!conflicts)
                        batchIndices.push_back(pendingIndex);
                    else
                        nextPendingIndices.push_back(pendingIndex);
                }

                if (batchIndices.empty())
                {
                    batchIndices.push_back(pendingIndices.front());
                    nextPendingIndices.erase(std::remove(nextPendingIndices.begin(), nextPendingIndices.end(), pendingIndices.front()), nextPendingIndices.end());
                }

                if (jobSystem.IsInitialized() && batchIndices.size() > 1)
                {
                    Concurrency::WaitGroup waitGroup;
                    for (size_t batchIndex : batchIndices)
                    {
                        waitGroup.Add(1);
                        jobSystem.Submit([&runParallelSlotAtIndex, &waitGroup, batchIndex]() {
                            runParallelSlotAtIndex(batchIndex);
                            waitGroup.Done();
                        });
                    }
                    waitGroup.Wait();
                }
                else
                {
                    for (size_t batchIndex : batchIndices)
                        runParallelSlotAtIndex(batchIndex);
                }

                pendingIndices = std::move(nextPendingIndices);
            }
        }

        SetRuntimePhase(RuntimePhase::Structural);
        FlushDeferredStructuralMutations();
        SetRuntimePhase(RuntimePhase::Transform);
        UpdateTransforms();
        SetRuntimePhase(RuntimePhase::Idle);
    }
}
