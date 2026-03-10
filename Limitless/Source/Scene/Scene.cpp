#include "Scene/Scene.h"
#include "Scene/Components/PhysicsComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/ScriptingComponents.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetBundle.h"
#include "Assets/AssetManager.h"
#include "Assets/AssetPaths.h"
#include "Assets/AssetTypes.h"
#include "Assets/AssetUtils.h"
#include "Assets/AnimationClipAsset.h"
#include "Assets/AnimationClipAssetImporter.h"
#include "Assets/AnimatorControllerAsset.h"
#include "Assets/AnimatorControllerAssetImporter.h"
#include "Assets/MaterialAsset.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/TileAsset.h"
#include "Assets/SpriteImportSettings.h"
#include "Assets/TextureAsset.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"
#include "Core/Application.h"
#include "Core/Input/InputSystem.h"
#include "Core/Concurrency/AsyncIO.h"
#include "Core/Time.h"
#include "Platform/Window.h"
#include "Physics/Physics2DQueries.h"
#include "Physics/Physics2DWorld.h"
#include "Scene/ParticleEmitterSystem.h"
#include "Scripting/Coroutine.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/NativeScriptRegistry.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Limitless
{
    namespace
    {
        std::string GetUnqualifiedScriptClassName(std::string_view className)
        {
            const size_t separator = className.rfind("::");
            if (separator == std::string_view::npos)
                return std::string(className);
            return std::string(className.substr(separator + 2));
        }

        std::string ResolveRegisteredScriptClassName(const std::string& requestedClassName,
                                                     const std::string& scriptAssetRelativePath)
        {
            if (requestedClassName.empty())
                return {};
            if (NativeScriptRegistry::HasScript(requestedClassName))
                return requestedClassName;

            const auto registeredScriptNames = NativeScriptRegistry::GetRegisteredScriptNames();
            auto resolveByToken = [&](const std::string& classToken) -> std::string {
                if (classToken.empty())
                    return {};
                if (NativeScriptRegistry::HasScript(classToken))
                    return classToken;

                std::string matchedClassName;
                for (const auto& candidate : registeredScriptNames)
                {
                    if (candidate == classToken || GetUnqualifiedScriptClassName(candidate) == classToken)
                    {
                        if (!matchedClassName.empty())
                            return {};
                        matchedClassName = candidate;
                    }
                }
                return matchedClassName;
            };

            if (const std::string fromRequested = resolveByToken(requestedClassName); !fromRequested.empty())
                return fromRequested;

            if (!scriptAssetRelativePath.empty())
            {
                const std::string stem = std::filesystem::path(scriptAssetRelativePath).stem().string();
                if (const std::string fromAssetPath = resolveByToken(stem); !fromAssetPath.empty())
                    return fromAssetPath;
            }

            if (registeredScriptNames.size() == 1)
                return registeredScriptNames.front();

            return {};
        }

        void SortScriptComponentEntities(const entt::registry& registry, std::vector<entt::entity>& scriptEntities)
        {
            std::sort(scriptEntities.begin(), scriptEntities.end(), [&registry](entt::entity left, entt::entity right) {
                const auto* leftScript = registry.try_get<ScriptComponent>(left);
                const auto* rightScript = registry.try_get<ScriptComponent>(right);
                if (!leftScript || !rightScript)
                    return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
                if (leftScript->OwnerEntity != rightScript->OwnerEntity)
                    return static_cast<uint32_t>(leftScript->OwnerEntity) < static_cast<uint32_t>(rightScript->OwnerEntity);
                if (leftScript->ComponentOrder != rightScript->ComponentOrder)
                    return leftScript->ComponentOrder < rightScript->ComponentOrder;
                return static_cast<uint32_t>(left) < static_cast<uint32_t>(right);
            });
        }

        std::vector<entt::entity> CollectScriptComponentEntities(const entt::registry& registry,
                                                                  std::optional<entt::entity> ownerFilter = std::nullopt)
        {
            std::vector<entt::entity> scriptEntities;
            auto view = registry.view<ScriptComponent>();
            for (entt::entity scriptEntity : view)
            {
                const auto& scriptComponent = view.get<ScriptComponent>(scriptEntity);
                if (ownerFilter.has_value() && scriptComponent.OwnerEntity != ownerFilter.value())
                    continue;

                scriptEntities.push_back(scriptEntity);
            }
            SortScriptComponentEntities(registry, scriptEntities);
            return scriptEntities;
        }

        void RenormalizeScriptComponentOrder(entt::registry& registry, entt::entity owner)
        {
            auto scriptEntities = CollectScriptComponentEntities(registry, owner);
            for (size_t index = 0; index < scriptEntities.size(); ++index)
            {
                auto* scriptComponent = registry.try_get<ScriptComponent>(scriptEntities[index]);
                if (!scriptComponent)
                    continue;
                scriptComponent->ComponentOrder = static_cast<int32_t>(index);
            }
        }
    }

    Scene::Scene()
        : m_DeferredStructuralMutationQueue(std::make_unique<Concurrency::LockFreeMPMCQueue<DeferredStructuralMutation, kDeferredStructuralMutationQueueSize>>())
        , m_DeferredStructuralMutationOverflowQueue(std::make_unique<Concurrency::LockFreeMPMCQueue<DeferredStructuralMutation, kDeferredStructuralMutationOverflowQueueSize>>())
    {
        m_Registry.on_destroy<Rigidbody2DComponent>().connect<&Scene::OnRigidbody2DComponentDestroyed>(*this);
        m_Registry.on_destroy<Joint2DComponent>().connect<&Scene::OnJoint2DComponentDestroyed>(*this);
    }

    Scene::~Scene()
    {
        m_IsShuttingDown = true;
        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);

        m_Registry.on_destroy<Rigidbody2DComponent>().disconnect<&Scene::OnRigidbody2DComponentDestroyed>(*this);
        m_Registry.on_destroy<Joint2DComponent>().disconnect<&Scene::OnJoint2DComponentDestroyed>(*this);

        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->Shutdown(*this);
        }

        const auto scriptEntities = CollectScriptComponentEntities(m_Registry);
        for (entt::entity scriptEntity : scriptEntities)
        {
            auto* scriptComponent = m_Registry.try_get<ScriptComponent>(scriptEntity);
            if (!scriptComponent)
                continue;
            const entt::entity ownerEntity = scriptComponent->OwnerEntity;

            const auto* tag = m_Registry.try_get<TagComponent>(ownerEntity);

            if (NativeScriptEntry* scriptEntry = scriptComponent->TryGetNativeEntry())
            {
                if (!scriptEntry->RuntimeInstance)
                    continue;

                if (scriptEntry->RuntimeInitialized)
                {
                    try
                    {
                        scriptEntry->RuntimeInstance->OnDestroy();
                    }
                    catch (const std::exception& exception)
                    {
                        LT_ERROR("Script '{}' on entity '{}' threw during OnDestroy in Scene destructor: {}",
                                 scriptEntry->ScriptClassName,
                                 tag ? tag->Tag : "Entity",
                                 exception.what());
                    }
                    catch (...)
                    {
                        LT_ERROR("Script '{}' on entity '{}' threw a non-standard exception during OnDestroy in Scene destructor",
                                 scriptEntry->ScriptClassName,
                                 tag ? tag->Tag : "Entity");
                    }
                }

                scriptComponent = m_Registry.try_get<ScriptComponent>(scriptEntity);
                if (!scriptComponent)
                    continue;
                NativeScriptEntry* refreshedScriptEntry = scriptComponent->TryGetNativeEntry();
                if (!refreshedScriptEntry || !refreshedScriptEntry->RuntimeInstance)
                    continue;

                try
                {
                    Coroutine::StopAll(*refreshedScriptEntry->RuntimeInstance);
                }
                catch (const std::exception& exception)
                {
                    LT_WARN("Script '{}' on entity '{}' threw during coroutine cleanup in Scene destructor: {}",
                            refreshedScriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity",
                            exception.what());
                }
                catch (...)
                {
                    LT_WARN("Script '{}' on entity '{}' threw a non-standard exception during coroutine cleanup in Scene destructor",
                            refreshedScriptEntry->ScriptClassName,
                            tag ? tag->Tag : "Entity");
                }

                refreshedScriptEntry->RuntimeInstance.reset();
                refreshedScriptEntry->RuntimeInitialized = false;
                refreshedScriptEntry->RuntimeUpdateCount = 0;
                refreshedScriptEntry->RuntimeWarnedOnUpdateTransformMutation = false;
                refreshedScriptEntry->RuntimeWarnedMissingAccessDeclaration = false;
                refreshedScriptEntry->RuntimeWarnedAccessMaskMismatch = false;
                continue;
            }

            ManagedScriptEntry* managedScriptEntry = scriptComponent->TryGetManagedEntry();
            if (!managedScriptEntry || managedScriptEntry->RuntimeInstanceId == 0)
                continue;

            if (managedScriptEntry->RuntimeInitialized)
            {
                std::string managedError;
                if (!ManagedScriptHost::InvokeScriptOnDestroy(managedScriptEntry->RuntimeInstanceId, this, &managedError))
                {
                    LT_ERROR("Managed script '{}' on entity '{}' failed during OnDestroy in Scene destructor: {}",
                             managedScriptEntry->ScriptClassName,
                             tag ? tag->Tag : "Entity",
                             managedError.empty() ? "unknown error" : managedError.c_str());
                }
            }

            ManagedScriptHost::DestroyScriptInstance(managedScriptEntry->RuntimeInstanceId);
            managedScriptEntry->RuntimeInstanceId = 0;
            managedScriptEntry->RuntimeInitialized = false;
            managedScriptEntry->RuntimeUpdateCount = 0;
            managedScriptEntry->RuntimeWarnedMissingHost = false;
            managedScriptEntry->RuntimeWarnedMissingClass = false;
        }
    }

    entt::entity Scene::AttachScriptComponent(entt::entity owner)
    {
        return AttachScriptComponent(owner, NativeScriptEntry{});
    }

    entt::entity Scene::AttachScriptComponent(entt::entity owner, NativeScriptEntry scriptEntry)
    {
        owner = ResolveEntityReference(owner);
        if (!IsValid(owner))
            return entt::null;

        entt::entity scriptEntity = m_Registry.create();
        ScriptComponent component{};
        component.OwnerEntity = owner;
        component.ComponentOrder = static_cast<int32_t>(GetScriptComponentEntities(owner).size());
        component.Backend = ScriptBackend::Native;
        component.Script = std::move(scriptEntry);
        m_Registry.emplace<ScriptComponent>(scriptEntity, std::move(component));
        return scriptEntity;
    }

    entt::entity Scene::AttachManagedScriptComponent(entt::entity owner)
    {
        return AttachManagedScriptComponent(owner, ManagedScriptEntry{});
    }

    entt::entity Scene::AttachManagedScriptComponent(entt::entity owner, ManagedScriptEntry scriptEntry)
    {
        owner = ResolveEntityReference(owner);
        if (!IsValid(owner))
            return entt::null;

        entt::entity scriptEntity = m_Registry.create();
        ScriptComponent component{};
        component.OwnerEntity = owner;
        component.ComponentOrder = static_cast<int32_t>(GetScriptComponentEntities(owner).size());
        component.Backend = ScriptBackend::Managed;
        component.ManagedScript = std::move(scriptEntry);
        m_Registry.emplace<ScriptComponent>(scriptEntity, std::move(component));
        return scriptEntity;
    }

    bool Scene::RemoveScriptComponent(entt::entity scriptComponentEntity)
    {
        if (!m_Registry.valid(scriptComponentEntity) || !m_Registry.all_of<ScriptComponent>(scriptComponentEntity))
            return false;

        const auto& scriptComponent = m_Registry.get<ScriptComponent>(scriptComponentEntity);
        if (const ManagedScriptEntry* managedEntry = scriptComponent.TryGetManagedEntry())
        {
            if (managedEntry->RuntimeInstanceId != 0)
                ManagedScriptHost::DestroyScriptInstance(managedEntry->RuntimeInstanceId);
        }

        const entt::entity ownerEntity = scriptComponent.OwnerEntity;
        m_Registry.destroy(scriptComponentEntity);
        if (ownerEntity != entt::null && IsValid(ownerEntity))
            RenormalizeScriptComponentOrder(m_Registry, ownerEntity);
        return true;
    }

    std::vector<entt::entity> Scene::GetScriptComponentEntities(entt::entity owner) const
    {
        owner = ResolveEntityReference(owner);
        if (owner == entt::null || !m_Registry.valid(owner))
            return {};
        return CollectScriptComponentEntities(m_Registry, owner);
    }

    ScriptComponent* Scene::GetScriptComponent(entt::entity scriptComponentEntity)
    {
        return m_Registry.try_get<ScriptComponent>(scriptComponentEntity);
    }

    const ScriptComponent* Scene::GetScriptComponent(entt::entity scriptComponentEntity) const
    {
        return m_Registry.try_get<ScriptComponent>(scriptComponentEntity);
    }

    void Scene::OnRigidbody2DComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!registry.valid(entity) || !registry.all_of<Rigidbody2DComponent>(entity))
            return;
        const auto& rigidbody = registry.get<Rigidbody2DComponent>(entity);

        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->TeardownRuntimeBodyForRemovedComponent(registry, entity, rigidbody);
        }
    }

    void Scene::OnJoint2DComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!registry.valid(entity) || !registry.all_of<Joint2DComponent>(entity))
            return;
        const auto& joint = registry.get<Joint2DComponent>(entity);

        for (auto& physicsWorld : m_Physics2DWorlds)
        {
            if (physicsWorld)
                physicsWorld->TeardownRuntimeJointForRemovedComponent(registry, entity, joint);
        }
    }

    std::string ResolveRegisteredScriptClassNameForSceneRuntime(const std::string& requestedClassName,
                                                                const std::string& scriptAssetRelativePath)
    {
        return ResolveRegisteredScriptClassName(requestedClassName, scriptAssetRelativePath);
    }

    void ProcessUiInteractionSystemForSceneRuntimeBridge(Scene& scene, uint32_t windowWidth, uint32_t windowHeight);

    void ProcessUiInteractionSystemForSceneRuntime(Scene& scene, uint32_t windowWidth, uint32_t windowHeight)
    {
        ProcessUiInteractionSystemForSceneRuntimeBridge(scene, windowWidth, windowHeight);
    }
}