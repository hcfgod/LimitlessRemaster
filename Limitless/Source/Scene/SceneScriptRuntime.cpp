#include "Scene/Scene.h"

#include "Core/Application.h"
#include "Physics/Physics2DQueries.h"
#include "Platform/Window.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scripting/Coroutine.h"
#include "Scripting/NativeScriptRegistry.h"

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
        if (NativeScriptRegistry::IsExecutionBlocked())
        {
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
                }
            }

            UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
            UpdateParticleEmitterSystem(m_Registry, deltaTime);
            UpdateTransforms();
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

        // Two-phase runtime bootstrapping:
        // 1) Create all script instances for currently active slots.
        // 2) Invoke OnCreate/OnUpdate in slot order.
        // This lets scripts safely reference sibling scripts during OnCreate,
        // even when the referenced script appears later in the list.
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
                continue;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnUpdate", "non-standard exception");
                continue;
            }

            scriptEntry = tryGetScriptEntry(entity, scriptIndex);
            if (!scriptEntry || !scriptEntry->RuntimeInstance)
                continue;
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
        }

        UpdateAnimation2DSystemForSceneRuntime(*this, deltaTime, ++m_AnimationDispatchFrameCounter);
        UpdateParticleEmitterSystem(m_Registry, deltaTime);
        UpdateTransforms();
    }

    void Scene::FixedUpdate(float fixedDeltaTime)
    {
        Physics2DQueries::SetActiveSceneForScriptQueries(this);
        if (NativeScriptRegistry::IsExecutionBlocked())
            return;

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

        // Mirror Update() bootstrap so FixedUpdate callbacks can also resolve
        // other scripts during OnCreate regardless of declaration order.
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
                scriptEntry->RuntimeWarnedMissingCompiledScript = false;
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
                continue;
            }
            catch (...)
            {
                handleScriptCallbackFailure(entity, scriptIndex, "OnFixedUpdate", "non-standard exception");
                continue;
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
        }

        UpdateTransforms();
    }
}
