#include "GameLayer.h"

#include "Core/Debug/Log.h"
#include "Core/Input/InputSystem.h"
#include "Physics/Physics2DQueries.h"
#include "Platform/Platform.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"
#include "Scripting/Debug.h"
#include "Scripting/Input.h"
#include "Scripting/InputActions.h"
#include "Scripting/ManagedScriptHost.h"
#include "Scripting/NativeScriptRegistry.h"
#include "Scripting/Physics2D.h"
#include "Scripting/ScriptCoreApi.h"
#include "Scripting/ScriptableEntity.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace Limitless
{
    namespace
    {
        using RegisterScriptCoreTypesFunction = void (*)(NativeScriptRegistrationCallback registrationCallback);
        using GetScriptCoreAbiVersionFunction = uint32_t (*)();
        using SetSceneTransitionBridgeFunction = void (*)(SceneTransitionBridgeCallback callback);
        using SetInputActionAxis1DBridgeFunction = void (*)(InputActionAxis1DBridgeCallback callback);
        using SetInputActionAxis2DBridgeFunction = void (*)(InputActionAxis2DBridgeCallback callback);
        using SetInputActionExistsBridgeFunction = void (*)(InputActionExistsBridgeCallback callback);
        using SetInputActionPressedBridgeFunction = void (*)(InputActionPressedBridgeCallback callback);
        using SetInputActionTriggerBridgeFunction = void (*)(InputActionTriggerBridgeCallback callback);
        using SetPhysics2DRaycastBridgeFunction = void (*)(Physics2DRaycastBridgeCallback callback);
        using SetScriptLogBridgeFunction = void (*)(ScriptLogBridgeCallback callback);
        using SetScriptCreateEntityBridgeFunction = void (*)(ScriptCreateEntityBridgeCallback callback);
        using SetScriptDestroyEntityBridgeFunction = void (*)(ScriptDestroyEntityBridgeCallback callback);
        using SetScriptInstantiatePrefabBridgeFunction = void (*)(ScriptInstantiatePrefabBridgeCallback callback);
        using SetScriptResolveEntityReferenceBridgeFunction = void (*)(ScriptResolveEntityReferenceBridgeCallback callback);
        using SetScriptContactEntityHandlesBridgeFunction = void (*)(ScriptGetContactEntityHandlesBridgeCallback callback);

        std::vector<std::filesystem::path> BuildManagedDirectoryCandidates()
        {
            std::vector<std::filesystem::path> candidates;
            auto addCandidate = [&](const std::filesystem::path& candidatePath) {
                if (candidatePath.empty())
                    return;
                if (std::find(candidates.begin(), candidates.end(), candidatePath) != candidates.end())
                    return;
                candidates.push_back(candidatePath);
            };

            const auto& platformInfo = PlatformDetection::GetPlatformInfo();
            if (!platformInfo.executablePath.empty())
                addCandidate(std::filesystem::path(platformInfo.executablePath).parent_path() / "Managed");

            std::error_code errorCode;
            const std::filesystem::path currentPath = std::filesystem::current_path(errorCode);
            if (!errorCode)
                addCandidate(currentPath / "Managed");

            return candidates;
        }

        void InitializeManagedScriptHost()
        {
            for (const auto& candidatePath : BuildManagedDirectoryCandidates())
            {
                std::string validationError;
                if (!ManagedScriptPayload::ValidatePayloadDirectory(candidatePath, nullptr, &validationError))
                    continue;

                if (ManagedScriptHost::Initialize(candidatePath))
                    return;
            }

            LT_INFO("GameLayer: managed scripting payload not found; managed discovery is disabled.");
        }

        bool ForwardSceneTransitionToHost(SceneTransitionType transitionType, const char* sceneIdentifier, LoadSceneMode loadSceneMode)
        {
            switch (transitionType)
            {
                case SceneTransitionType::LoadByAssetKey:
                    if (!sceneIdentifier)
                        return false;
                    return SceneManager::LoadScene(sceneIdentifier, loadSceneMode);
                case SceneTransitionType::ReloadCurrentScene:
                    return SceneManager::ReloadCurrentScene();
                case SceneTransitionType::SetActiveSceneByAssetKey:
                    if (!sceneIdentifier)
                        return false;
                    return SceneManager::SetActiveScene(sceneIdentifier);
                case SceneTransitionType::UnloadByAssetKey:
                    if (!sceneIdentifier)
                        return false;
                    return SceneManager::UnloadScene(sceneIdentifier);
            }

            return false;
        }

        float ForwardInputActionAxis1DToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionAxis1D(safeMap, safeAction);
        }

        glm::vec2 ForwardInputActionAxis2DToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionAxis2D(safeMap, safeAction);
        }

        glm::vec2 ForwardInputMousePositionToHost()
        {
            return InputSystem::GetInstance().GetMousePosition();
        }

        glm::vec2 ForwardInputMouseDeltaToHost()
        {
            return InputSystem::GetInstance().GetMouseDelta();
        }

        glm::vec2 ForwardInputMouseWheelDeltaToHost()
        {
            return InputSystem::GetInstance().GetMouseWheelDelta();
        }

        bool ForwardInputMouseButtonToHost(uint8_t button)
        {
            return InputSystem::GetInstance().IsMouseButtonDown(button);
        }

        bool ForwardInputMouseButtonDownToHost(uint8_t button)
        {
            return InputSystem::GetInstance().WasMouseButtonPressedThisFrame(button);
        }

        bool ForwardInputMouseButtonUpToHost(uint8_t button)
        {
            return InputSystem::GetInstance().WasMouseButtonReleasedThisFrame(button);
        }

        bool ForwardInputActionExistsToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().HasAction(safeMap, safeAction);
        }

        bool ForwardInputActionPressedToHost(const char* mapName, const char* actionName, float deadzone)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().IsActionPressed(safeMap, safeAction, deadzone);
        }

        bool ForwardInputActionStartedToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionStartedThisFrame(safeMap, safeAction);
        }

        bool ForwardInputActionPerformedToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionPerformedThisFrame(safeMap, safeAction);
        }

        bool ForwardInputActionCanceledToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().WasActionCanceledThisFrame(safeMap, safeAction);
        }

        bool ForwardInputActionButtonToHost(const char* mapName, const char* actionName)
        {
            const std::string_view safeMap = mapName ? std::string_view(mapName) : std::string_view();
            const std::string_view safeAction = actionName ? std::string_view(actionName) : std::string_view();
            return InputSystem::GetInstance().ReadActionButton(safeMap, safeAction);
        }

        bool ForwardPhysics2DRaycastToHost(float originX,
                                           float originY,
                                           float directionX,
                                           float directionY,
                                           float maxDistance,
                                           uint64_t collisionMask,
                                           RaycastHit2D* outHit)
        {
            if (!outHit)
                return false;
            *outHit = RaycastHit2D{};

            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene)
                return false;

            const Physics2DRaycastHit nativeHit = Physics2DQueries::RaycastClosest(
                scene,
                glm::vec2(originX, originY),
                glm::vec2(directionX, directionY),
                maxDistance,
                collisionMask);

            if (!nativeHit.HasHit)
                return false;

            outHit->HasHit = true;
            outHit->Entity = nativeHit.Entity;
            outHit->Point = nativeHit.Point;
            outHit->Normal = nativeHit.Normal;
            outHit->Fraction = nativeHit.Fraction;
            return true;
        }

        void ForwardScriptLogToHost(ScriptLogSeverity severity, const char* message)
        {
            const std::string safeMessage = message ? message : "";
            switch (severity)
            {
                case ScriptLogSeverity::Info:
                    LT_INFO("[Script] {}", safeMessage);
                    break;
                case ScriptLogSeverity::Warning:
                    LT_WARN("[Script] {}", safeMessage);
                    break;
                case ScriptLogSeverity::Error:
                    LT_ERROR("[Script] {}", safeMessage);
                    break;
            }
        }

        entt::entity ForwardScriptCreateEntityToHost(const char* name)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene)
                return entt::null;
            if (!name || name[0] == '\0')
                return scene->CreateEntity("Entity");
            return scene->CreateEntity(name);
        }

        void ForwardScriptDestroyEntityToHost(entt::entity entity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene)
                return;
            scene->DestroyEntity(entity);
        }

        entt::entity ForwardScriptInstantiatePrefabToHost(const char* prefabAssetKey, entt::entity parentEntity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene || !prefabAssetKey || prefabAssetKey[0] == '\0')
                return entt::null;
            return scene->InstantiatePrefab(prefabAssetKey, parentEntity);
        }

        entt::entity ForwardScriptResolveEntityReferenceToHost(entt::entity entity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene || entity == entt::null)
                return entity;
            return scene->ResolveEntityReference(entity);
        }

        uint32_t ForwardScriptContactEntityHandlesToHost(entt::entity entity,
                                                         bool includeSensorContacts,
                                                         entt::entity* outHandles,
                                                         uint32_t capacity)
        {
            Scene* scene = Physics2DQueries::GetActiveSceneForScriptQueries();
            if (!scene || entity == entt::null)
                return 0u;
            auto& registry = scene->GetRegistry();
            if (!registry.valid(entity))
                return 0u;

            const Physics2DContactListener* contacts = scene->GetPhysics2DContactEventsForEntity(entity);
            if (!contacts)
                return 0u;

            std::vector<entt::entity> uniqueContacts;
            std::unordered_set<entt::entity> seenContacts;
            const auto& events = contacts->GetEvents();
            for (const auto& eventData : events)
            {
                if (!includeSensorContacts && eventData.IsSensor)
                    continue;

                entt::entity other = entt::null;
                if (eventData.EntityA == entity)
                    other = eventData.EntityB;
                else if (eventData.EntityB == entity)
                    other = eventData.EntityA;

                if (other == entt::null || !registry.valid(other))
                    continue;
                if (seenContacts.insert(other).second)
                    uniqueContacts.push_back(other);
            }

            const uint32_t totalCount = static_cast<uint32_t>(uniqueContacts.size());
            if (!outHandles || capacity == 0u)
                return totalCount;

            const uint32_t toWrite = std::min(totalCount, capacity);
            for (uint32_t index = 0; index < toWrite; ++index)
                outHandles[index] = uniqueContacts[static_cast<size_t>(index)];
            return toWrite;
        }

        bool ForwardScriptParallelExecutionStateToHost()
        {
            return Scene::IsCurrentThreadParallelScriptExecution();
        }

        template<typename TCallback>
        bool ConnectOptionalScriptCoreBridge(void* libraryHandle, const char* exportName, TCallback callback)
        {
            if (!libraryHandle || !exportName || exportName[0] == '\0')
                return false;

            using SetterFunction = void (*)(TCallback callbackValue);
            auto setter = reinterpret_cast<SetterFunction>(PlatformUtils::GetProcAddress(libraryHandle, exportName));
            if (!setter)
                return false;

            setter(callback);
            return true;
        }

        void BindLoadedScriptCoreHostBridges(void* libraryHandle)
        {
            ConnectOptionalScriptCoreBridge<SceneTransitionBridgeCallback>(libraryHandle, "LT_SetSceneTransitionBridge", &ForwardSceneTransitionToHost);
            ConnectOptionalScriptCoreBridge<InputActionAxis1DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis1DBridge", &ForwardInputActionAxis1DToHost);
            ConnectOptionalScriptCoreBridge<InputActionAxis2DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis2DBridge", &ForwardInputActionAxis2DToHost);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMousePositionBridge", &ForwardInputMousePositionToHost);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseDeltaBridge", &ForwardInputMouseDeltaToHost);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseWheelDeltaBridge", &ForwardInputMouseWheelDeltaToHost);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonBridge", &ForwardInputMouseButtonToHost);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonDownBridge", &ForwardInputMouseButtonDownToHost);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonUpBridge", &ForwardInputMouseButtonUpToHost);
            ConnectOptionalScriptCoreBridge<InputActionExistsBridgeCallback>(libraryHandle, "LT_SetInputActionExistsBridge", &ForwardInputActionExistsToHost);
            ConnectOptionalScriptCoreBridge<InputActionPressedBridgeCallback>(libraryHandle, "LT_SetInputActionPressedBridge", &ForwardInputActionPressedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionStartedBridge", &ForwardInputActionStartedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionPerformedBridge", &ForwardInputActionPerformedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionCanceledBridge", &ForwardInputActionCanceledToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionButtonBridge", &ForwardInputActionButtonToHost);
            ConnectOptionalScriptCoreBridge<Physics2DRaycastBridgeCallback>(libraryHandle, "LT_SetPhysics2DRaycastBridge", &ForwardPhysics2DRaycastToHost);
            ConnectOptionalScriptCoreBridge<ScriptLogBridgeCallback>(libraryHandle, "LT_SetScriptLogBridge", &ForwardScriptLogToHost);
            ConnectOptionalScriptCoreBridge<ScriptCreateEntityBridgeCallback>(libraryHandle, "LT_SetScriptCreateEntityBridge", &ForwardScriptCreateEntityToHost);
            ConnectOptionalScriptCoreBridge<ScriptDestroyEntityBridgeCallback>(libraryHandle, "LT_SetScriptDestroyEntityBridge", &ForwardScriptDestroyEntityToHost);
            ConnectOptionalScriptCoreBridge<ScriptInstantiatePrefabBridgeCallback>(libraryHandle, "LT_SetScriptInstantiatePrefabBridge", &ForwardScriptInstantiatePrefabToHost);
            ConnectOptionalScriptCoreBridge<ScriptResolveEntityReferenceBridgeCallback>(libraryHandle, "LT_SetScriptResolveEntityReferenceBridge", &ForwardScriptResolveEntityReferenceToHost);
            ConnectOptionalScriptCoreBridge<ScriptGetContactEntityHandlesBridgeCallback>(libraryHandle, "LT_SetScriptContactEntityHandlesBridge", &ForwardScriptContactEntityHandlesToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonDownBridge", &ForwardInputActionStartedToHost);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonBridge", &ForwardInputActionButtonToHost);
        }

        void UnbindLoadedScriptCoreHostBridges(void* libraryHandle)
        {
            ConnectOptionalScriptCoreBridge<SceneTransitionBridgeCallback>(libraryHandle, "LT_SetSceneTransitionBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionAxis1DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis1DBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionAxis2DBridgeCallback>(libraryHandle, "LT_SetInputActionAxis2DBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMousePositionBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseDeltaBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseVector2BridgeCallback>(libraryHandle, "LT_SetInputMouseWheelDeltaBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonDownBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputMouseButtonBridgeCallback>(libraryHandle, "LT_SetInputMouseButtonUpBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionExistsBridgeCallback>(libraryHandle, "LT_SetInputActionExistsBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionPressedBridgeCallback>(libraryHandle, "LT_SetInputActionPressedBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionStartedBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionPerformedBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionCanceledBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputActionButtonBridge", nullptr);
            ConnectOptionalScriptCoreBridge<Physics2DRaycastBridgeCallback>(libraryHandle, "LT_SetPhysics2DRaycastBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptLogBridgeCallback>(libraryHandle, "LT_SetScriptLogBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptCreateEntityBridgeCallback>(libraryHandle, "LT_SetScriptCreateEntityBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptDestroyEntityBridgeCallback>(libraryHandle, "LT_SetScriptDestroyEntityBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptInstantiatePrefabBridgeCallback>(libraryHandle, "LT_SetScriptInstantiatePrefabBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptResolveEntityReferenceBridgeCallback>(libraryHandle, "LT_SetScriptResolveEntityReferenceBridge", nullptr);
            ConnectOptionalScriptCoreBridge<ScriptGetContactEntityHandlesBridgeCallback>(libraryHandle, "LT_SetScriptContactEntityHandlesBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonDownBridge", nullptr);
            ConnectOptionalScriptCoreBridge<InputActionTriggerBridgeCallback>(libraryHandle, "LT_SetInputButtonBridge", nullptr);
        }

        void BindNativeScriptHostBridges()
        {
            ScriptableEntity::SetCreateEntityBridgeCallback(&ForwardScriptCreateEntityToHost);
            ScriptableEntity::SetDestroyEntityBridgeCallback(&ForwardScriptDestroyEntityToHost);
            ScriptableEntity::SetInstantiatePrefabBridgeCallback(&ForwardScriptInstantiatePrefabToHost);
            ScriptableEntity::SetResolveEntityReferenceBridgeCallback(&ForwardScriptResolveEntityReferenceToHost);
            ScriptableEntity::SetContactEntityHandlesBridgeCallback(&ForwardScriptContactEntityHandlesToHost);
            ScriptableEntity::SetParallelScriptExecutionBridgeCallback(&ForwardScriptParallelExecutionStateToHost);
            Entity::SetDestroyBridgeCallback(&ForwardScriptDestroyEntityToHost);
            Entity::SetParallelExecutionBridgeCallback(&ForwardScriptParallelExecutionStateToHost);
        }

        void UnbindNativeScriptHostBridges()
        {
            ScriptableEntity::SetCreateEntityBridgeCallback(nullptr);
            ScriptableEntity::SetDestroyEntityBridgeCallback(nullptr);
            ScriptableEntity::SetInstantiatePrefabBridgeCallback(nullptr);
            ScriptableEntity::SetResolveEntityReferenceBridgeCallback(nullptr);
            ScriptableEntity::SetContactEntityHandlesBridgeCallback(nullptr);
            ScriptableEntity::SetParallelScriptExecutionBridgeCallback(nullptr);
            Entity::SetDestroyBridgeCallback(nullptr);
            Entity::SetParallelExecutionBridgeCallback(nullptr);
        }

        void RegisterScriptFromModule(const char* className, NativeScriptCreateFunction createFunction)
        {
            if (className && createFunction)
                NativeScriptRegistry::RegisterScript(className, createFunction);
        }

        std::string GetScriptCoreLibraryFileName()
        {
#if defined(LT_PLATFORM_WINDOWS)
            return "ScriptCore.dll";
#elif defined(LT_PLATFORM_MACOS)
            return "libScriptCore.dylib";
#else
            return "libScriptCore.so";
#endif
        }
    }

    void GameLayer::InitializeScriptCore()
    {
        NativeScriptRegistry::Clear();
        InitializeManagedScriptHost();
        BindNativeScriptHostBridges();

        std::filesystem::path libraryPath;
        const auto& platformInfo = PlatformDetection::GetPlatformInfo();
        if (!platformInfo.executablePath.empty())
            libraryPath = std::filesystem::path(platformInfo.executablePath).parent_path() / GetScriptCoreLibraryFileName();

        if (libraryPath.empty() || !std::filesystem::exists(libraryPath))
            libraryPath = std::filesystem::current_path() / GetScriptCoreLibraryFileName();

        if (!std::filesystem::exists(libraryPath))
        {
            LT_WARN("GameLayer: ScriptCore library not found. Running without scripts.");
            return;
        }

        m_ScriptCoreLibraryHandle = PlatformUtils::LoadLibrary(libraryPath.string());
        if (!m_ScriptCoreLibraryHandle)
        {
            LT_ERROR("GameLayer: failed to load ScriptCore library.");
            return;
        }

        const auto getAbiVersionFunction = reinterpret_cast<GetScriptCoreAbiVersionFunction>(
            PlatformUtils::GetProcAddress(m_ScriptCoreLibraryHandle, "LT_GetScriptCoreAbiVersion"));
        if (!getAbiVersionFunction)
        {
            LT_ERROR("GameLayer: ScriptCore missing LT_GetScriptCoreAbiVersion export.");
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
            return;
        }

        const uint32_t reportedAbiVersion = getAbiVersionFunction();
        if (reportedAbiVersion != kScriptCoreAbiVersion)
        {
            LT_ERROR("GameLayer: ScriptCore ABI mismatch. expected={}, got={}. Rebuild project scripts/ScriptCore for this engine revision.",
                     kScriptCoreAbiVersion,
                     reportedAbiVersion);
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
            return;
        }

        const auto registerFunction = reinterpret_cast<RegisterScriptCoreTypesFunction>(
            PlatformUtils::GetProcAddress(m_ScriptCoreLibraryHandle, "LT_RegisterScriptCoreTypes"));
        if (!registerFunction)
        {
            LT_ERROR("GameLayer: ScriptCore missing LT_RegisterScriptCoreTypes export.");
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
            return;
        }

        BindLoadedScriptCoreHostBridges(m_ScriptCoreLibraryHandle);

        registerFunction(&RegisterScriptFromModule);

        LT_INFO("GameLayer: ScriptCore loaded with {} script(s).",
                 NativeScriptRegistry::GetRegisteredScriptNames().size());
    }

    void GameLayer::ShutdownScriptCore()
    {
        ManagedScriptHost::Shutdown();

        if (m_ScriptCoreLibraryHandle)
        {
            UnbindLoadedScriptCoreHostBridges(m_ScriptCoreLibraryHandle);
            PlatformUtils::FreeLibrary(m_ScriptCoreLibraryHandle);
            m_ScriptCoreLibraryHandle = nullptr;
        }

        UnbindNativeScriptHostBridges();

        NativeScriptRegistry::Clear();
    }
}
