#include "Scripting/ManagedScriptHostInternal.h"

#include "Core/Debug/Log.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <unordered_set>

namespace Limitless::ManagedScriptHost
{
    using namespace Internal;

    namespace Internal
    {
        struct InvocationScope final
        {
            InvocationScope(Scene* scene, uint64_t runtimeInstanceId)
            {
                s_HostState.ActiveScene = scene;
                s_HostState.ActiveRuntimeInstanceId = runtimeInstanceId;
                s_HostState.LastManagedExceptionMessage.clear();
            }

            ~InvocationScope()
            {
                s_HostState.ActiveScene = nullptr;
                s_HostState.ActiveRuntimeInstanceId = 0;
            }
        };

        bool ConsumeInvocationStatus(std::string* errorMessage)
        {
            if (!s_HostState.LastManagedExceptionMessage.empty())
            {
                if (errorMessage != nullptr)
                    *errorMessage = s_HostState.LastManagedExceptionMessage;
                return false;
            }

            if (errorMessage != nullptr)
                errorMessage->clear();
            return true;
        }

        template<typename... TArgs>
        bool InvokeRuntimeMethod(uint64_t instanceId,
                                 Scene* scene,
                                 std::string_view methodName,
                                 const char* nonStandardMessage,
                                 std::string* errorMessage,
                                 TArgs... args)
        {
            RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId);
            if (runtimeInstance == nullptr)
            {
                if (errorMessage != nullptr)
                    *errorMessage = "managed runtime instance was not found";
                return false;
            }

            InvocationScope invocationScope(scene, instanceId);
            try
            {
                runtimeInstance->Object.InvokeMethod(methodName, args...);
            }
            catch (const std::exception& exception)
            {
                if (errorMessage != nullptr)
                    *errorMessage = exception.what();
                return false;
            }
            catch (...)
            {
                if (errorMessage != nullptr)
                    *errorMessage = nonStandardMessage;
                return false;
            }

            return ConsumeInvocationStatus(errorMessage);
        }

        void ClearState()
        {
            for (auto& [instanceId, runtimeInstance] : s_HostState.RuntimeInstances)
            {
                (void)instanceId;
                if (runtimeInstance.Object.IsValid())
                    runtimeInstance.Object.Destroy();
            }
            s_HostState.RuntimeInstances.clear();
            s_HostState.DiscoveredTypes.clear();
            s_HostState.ContractAssembly = nullptr;
            s_HostState.PayloadManifest = {};
            s_HostState.NextRuntimeInstanceId = 1;
            s_HostState.ActiveScene = nullptr;
            s_HostState.ActiveRuntimeInstanceId = 0;
            s_HostState.LastManagedExceptionMessage.clear();

            if (s_HostState.Context && s_HostState.Host)
            {
                s_HostState.Host->UnloadAssemblyLoadContext(*s_HostState.Context);
                s_HostState.Context.reset();
            }

            if (s_HostState.Snapshot.HostInitialized && s_HostState.Host)
                s_HostState.Host->Shutdown();

            s_HostState.Host.reset();
            CleanupLoadedManagedPayloadDirectory();

            s_HostState.Snapshot = {};
        }

        bool DiscoverFromManagedDirectory(const std::filesystem::path& managedDirectory)
        {
            ManagedScriptPayload::PayloadManifest payloadManifest{};
            std::string payloadError;
            if (!ManagedScriptPayload::ValidatePayloadDirectory(managedDirectory, &payloadManifest, &payloadError))
            {
                LT_WARN("Managed scripting: invalid managed payload at '{}': {}",
                        managedDirectory.string(),
                        payloadError.empty() ? "unknown validation error" : payloadError.c_str());
                return false;
            }

            std::filesystem::path loadedManagedDirectory;
            if (!ShadowCopyManagedPayload(managedDirectory, loadedManagedDirectory, &payloadError))
            {
                LT_WARN("Managed scripting: failed to stage managed payload from '{}': {}",
                        managedDirectory.string(),
                        payloadError.empty() ? "unknown staging error" : payloadError.c_str());
                return false;
            }

            s_HostState.PayloadManifest = payloadManifest;
            s_HostState.Snapshot.ManagedDirectory = NormalizeManagedDirectoryPath(managedDirectory);
            s_HostState.Snapshot.LoadedManagedDirectory = NormalizeManagedDirectoryPath(loadedManagedDirectory);
            s_HostState.Snapshot.PayloadApiVersion = payloadManifest.ApiVersion;

            Coral::HostSettings hostSettings{};
            hostSettings.CoralDirectory = loadedManagedDirectory.string();
            hostSettings.MessageCallback = &LogCoralMessage;
            hostSettings.MessageFilter = Coral::MessageLevel::All;
            hostSettings.ExceptionCallback = &CaptureManagedException;

            s_HostState.Host = std::make_unique<Coral::HostInstance>();
            const Coral::CoralInitStatus initStatus = s_HostState.Host->Initialize(std::move(hostSettings));
            if (initStatus != Coral::CoralInitStatus::Success)
            {
                LT_WARN("Managed scripting: failed to initialize Coral host from '{}'. status={}",
                        loadedManagedDirectory.string(),
                        static_cast<int>(initStatus));
                return false;
            }

            s_HostState.Snapshot.HostInitialized = true;
            s_HostState.Context = std::make_unique<Coral::AssemblyLoadContext>(
                s_HostState.Host->CreateAssemblyLoadContext("Limitless.ManagedScripts"));

            const std::filesystem::path contractAssemblyPath = loadedManagedDirectory / payloadManifest.ContractAssembly;
            if (!std::filesystem::exists(contractAssemblyPath))
            {
                LT_WARN("Managed scripting: managed contract assembly not found at '{}'.", contractAssemblyPath.string());
                return false;
            }

            Coral::ManagedAssembly& contractAssembly = s_HostState.Context->LoadAssembly(contractAssemblyPath.string());
            s_HostState.ContractAssembly = &contractAssembly;
            if (contractAssembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
            {
                LT_WARN("Managed scripting: failed to load managed contract assembly '{}'. status={}",
                        contractAssemblyPath.string(),
                        static_cast<int>(contractAssembly.GetLoadStatus()));
                return false;
            }

            RegisterInternalCalls(contractAssembly);

            Coral::Type& baseType = contractAssembly.GetLocalType(kScriptBaseTypeName);
            if (!baseType)
            {
                LT_WARN("Managed scripting: base type '{}' was not found in '{}'.",
                        kScriptBaseTypeName,
                        contractAssemblyPath.string());
                return false;
            }

            std::unordered_set<std::string> discoveredNames;
            std::error_code errorCode;
            for (const auto& entry : std::filesystem::directory_iterator(loadedManagedDirectory, errorCode))
            {
                if (errorCode)
                    break;
                if (!entry.is_regular_file())
                    continue;
                if (entry.path().extension() != ".dll")
                    continue;

                const std::string fileNameLower = ToLower(entry.path().filename().string());
                if (fileNameLower == ToLower(kCoralManagedAssemblyFileName) ||
                    fileNameLower == ToLower(payloadManifest.ContractAssembly))
                {
                    continue;
                }

                Coral::ManagedAssembly& assembly = s_HostState.Context->LoadAssembly(entry.path().string());
                if (assembly.GetLoadStatus() != Coral::AssemblyLoadStatus::Success)
                {
                    LT_TRACE("Managed scripting: skipping assembly '{}' due to load status {}.",
                             entry.path().string(),
                             static_cast<int>(assembly.GetLoadStatus()));
                    continue;
                }

                for (const Coral::Type& discoveredType : assembly.GetLocalTypes())
                {
                    Coral::Type& type = const_cast<Coral::Type&>(discoveredType);
                    if (!type || !type.IsSubclassOf(baseType))
                        continue;

                    const std::string fullName = ToUtf8(type.GetFullName());
                    if (fullName.empty() || !discoveredNames.insert(fullName).second)
                        continue;

                    DiscoveredScriptClass discovered{};
                    discovered.FullName = fullName;
                    discovered.AssemblyName = std::string(assembly.GetName());
                    discovered.AssemblyPath = entry.path();
                    discovered.ReflectedFields = ReflectManagedFields(type);
                    s_HostState.Snapshot.Classes.push_back(std::move(discovered));
                    s_HostState.DiscoveredTypes.emplace(fullName, &type);
                }
            }

            std::sort(s_HostState.Snapshot.Classes.begin(),
                      s_HostState.Snapshot.Classes.end(),
                      [](const DiscoveredScriptClass& left, const DiscoveredScriptClass& right) {
                          return left.FullName < right.FullName;
                      });

            LT_INFO("Managed scripting: initialized Coral host from source '{}' via loaded shadow '{}' and discovered {} managed script class(es).",
                    s_HostState.Snapshot.ManagedDirectory.string(),
                    s_HostState.Snapshot.LoadedManagedDirectory.string(),
                    s_HostState.Snapshot.Classes.size());
            for (const auto& discoveredClass : s_HostState.Snapshot.Classes)
            {
                LT_INFO("Managed scripting: discovered '{}' from '{}'.",
                        discoveredClass.FullName,
                        discoveredClass.AssemblyPath.filename().string());
            }
            return true;
        }
    }

    bool Initialize(const std::filesystem::path& managedDirectory)
    {
        if (managedDirectory.empty())
            return false;

        if (IsInitialized())
        {
            const std::filesystem::path requestedDirectory = NormalizeManagedDirectoryPath(managedDirectory);
            const std::filesystem::path activeDirectory = NormalizeManagedDirectoryPath(s_HostState.Snapshot.ManagedDirectory);
            if (requestedDirectory == activeDirectory)
                return true;

            LT_WARN("Managed scripting: host already initialized from '{}' and cannot be reconfigured in-process to '{}'. Continuing to use the existing managed directory.",
                    activeDirectory.string(),
                    requestedDirectory.string());
            return true;
        }

        if (!Internal::DiscoverFromManagedDirectory(managedDirectory))
        {
            Shutdown();
            return false;
        }

        return true;
    }

    void Shutdown()
    {
        Internal::ClearState();
    }

    bool IsInitialized()
    {
        return s_HostState.Snapshot.HostInitialized;
    }

    std::string ResolveDiscoveredClassName(std::string_view className)
    {
        const DiscoveredScriptClass* discoveredClass = ResolveDiscoveredClassMetadata(className);
        return discoveredClass ? discoveredClass->FullName : std::string{};
    }

    bool HasDiscoveredClass(std::string_view className)
    {
        if (className.empty())
            return false;

        return ResolveDiscoveredClassMetadata(className) != nullptr;
    }

    uint64_t CreateScriptInstance(std::string_view className, uint32_t entityHandle, std::string* errorMessage)
    {
        if (!IsInitialized())
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed host is not initialized";
            return 0;
        }

        const std::string resolvedClassName = ResolveDiscoveredClassName(className);
        const auto typeIterator = s_HostState.DiscoveredTypes.find(resolvedClassName.empty() ? std::string(className) : resolvedClassName);
        if (typeIterator == s_HostState.DiscoveredTypes.end() || typeIterator->second == nullptr || !(*typeIterator->second))
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed class was not discovered";
            return 0;
        }

        try
        {
            Coral::ManagedObject instance = typeIterator->second->CreateInstance();
            if (!instance.IsValid())
            {
                if (errorMessage != nullptr)
                    *errorMessage = "managed instance creation returned an invalid object";
                return 0;
            }

            instance.SetFieldValue("EntityId", entityHandle);

            RuntimeInstance runtimeInstance{};
            runtimeInstance.Id = s_HostState.NextRuntimeInstanceId++;
            runtimeInstance.ClassName = resolvedClassName.empty() ? std::string(className) : resolvedClassName;
            runtimeInstance.EntityHandle = entityHandle;
            runtimeInstance.Object = std::move(instance);
            runtimeInstance.LastSynchronizedExposedPropertiesRevision = 0;

            const uint64_t instanceId = runtimeInstance.Id;
            s_HostState.RuntimeInstances.emplace(instanceId, std::move(runtimeInstance));
            if (errorMessage != nullptr)
                errorMessage->clear();
            return instanceId;
        }
        catch (const std::exception& exception)
        {
            if (errorMessage != nullptr)
                *errorMessage = exception.what();
        }
        catch (...)
        {
            if (errorMessage != nullptr)
                *errorMessage = "non-standard exception during managed instance creation";
        }

        return 0;
    }

    bool SynchronizeScriptExposedProperties(uint64_t instanceId,
                                            Scene* scene,
                                            const std::unordered_map<std::string, ScriptPropertyValue>& exposedProperties,
                                            uint64_t revision,
                                            std::string* errorMessage)
    {
        RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId);
        if (runtimeInstance == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed runtime instance was not found";
            return false;
        }

        InvocationScope invocationScope(scene, instanceId);

        if (runtimeInstance->LastSynchronizedExposedPropertiesRevision == revision)
        {
            if (errorMessage != nullptr)
                errorMessage->clear();
            return true;
        }

        const DiscoveredScriptClass* discoveredClass = FindDiscoveredClassMetadata(runtimeInstance->ClassName);
        if (discoveredClass == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed class metadata was not found";
            return false;
        }

        for (const ReflectedFieldDefinition& fieldDefinition : discoveredClass->ReflectedFields)
        {
            const auto propertyIterator = exposedProperties.find(fieldDefinition.Name);
            const ScriptPropertyValue& propertyValue = (propertyIterator != exposedProperties.end())
                ? propertyIterator->second
                : fieldDefinition.DefaultValue;

            if (!ApplyManagedFieldValue(*runtimeInstance, scene, fieldDefinition, propertyValue, errorMessage))
            {
                if (errorMessage != nullptr && errorMessage->empty())
                    *errorMessage = "failed synchronizing managed field '" + fieldDefinition.Name + "'";
                return false;
            }
        }

        runtimeInstance->LastSynchronizedExposedPropertiesRevision = revision;
        return Internal::ConsumeInvocationStatus(errorMessage);
    }

    bool ReadBackScriptExposedProperties(uint64_t instanceId,
                                         Scene* scene,
                                         std::unordered_map<std::string, ScriptPropertyValue>& exposedProperties,
                                         uint64_t* revision,
                                         std::string* errorMessage)
    {
        RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId);
        if (runtimeInstance == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed runtime instance was not found";
            return false;
        }

        InvocationScope invocationScope(scene, instanceId);

        const DiscoveredScriptClass* discoveredClass = FindDiscoveredClassMetadata(runtimeInstance->ClassName);
        if (discoveredClass == nullptr)
        {
            if (errorMessage != nullptr)
                *errorMessage = "managed class metadata was not found";
            return false;
        }

        bool changed = false;
        for (const ReflectedFieldDefinition& fieldDefinition : discoveredClass->ReflectedFields)
        {
            ScriptPropertyValue propertyValue = fieldDefinition.DefaultValue;
            if (!TryReadRuntimeFieldValue(*runtimeInstance, scene, fieldDefinition, propertyValue, errorMessage))
            {
                if (errorMessage != nullptr && errorMessage->empty())
                    *errorMessage = "failed reading managed field '" + fieldDefinition.Name + "'";
                return false;
            }

            const auto propertyIterator = exposedProperties.find(fieldDefinition.Name);
            if (propertyIterator == exposedProperties.end())
            {
                exposedProperties.emplace(fieldDefinition.Name, std::move(propertyValue));
                changed = true;
                continue;
            }

            if (ScriptPropertyValuesEqual(propertyIterator->second, propertyValue))
                continue;

            propertyIterator->second = std::move(propertyValue);
            changed = true;
        }

        if (!Internal::ConsumeInvocationStatus(errorMessage))
            return false;

        if (changed)
        {
            if (revision != nullptr)
                ++(*revision);

            runtimeInstance->LastSynchronizedExposedPropertiesRevision = revision != nullptr
                ? *revision
                : runtimeInstance->LastSynchronizedExposedPropertiesRevision + 1;
        }

        if (errorMessage != nullptr)
            errorMessage->clear();
        return true;
    }

    bool InvokeScriptOnCreate(uint64_t instanceId, Scene* scene, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchCreateInternal", "non-standard exception during OnCreate", errorMessage);
    }

    bool InvokeScriptOnFixedUpdate(uint64_t instanceId, Scene* scene, float fixedDeltaTime, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchFixedUpdateInternal", "non-standard exception during OnFixedUpdate", errorMessage, fixedDeltaTime);
    }

    bool InvokeScriptOnUpdate(uint64_t instanceId, Scene* scene, float deltaTime, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchUpdateInternal", "non-standard exception during OnUpdate", errorMessage, deltaTime);
    }

    bool InvokeScriptOnCollisionEnter(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchCollisionEnterInternal", "non-standard exception during OnCollisionEnter", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnCollisionStay(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchCollisionStayInternal", "non-standard exception during OnCollisionStay", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnCollisionExit(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchCollisionExitInternal", "non-standard exception during OnCollisionExit", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnTriggerEnter(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchTriggerEnterInternal", "non-standard exception during OnTriggerEnter", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnTriggerStay(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchTriggerStayInternal", "non-standard exception during OnTriggerStay", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnTriggerExit(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchTriggerExitInternal", "non-standard exception during OnTriggerExit", errorMessage, otherEntityHandle);
    }

    bool InvokeScriptOnDestroy(uint64_t instanceId, Scene* scene, std::string* errorMessage)
    {
        return Internal::InvokeRuntimeMethod(instanceId, scene, "DispatchDestroyInternal", "non-standard exception during OnDestroy", errorMessage);
    }

    void DestroyScriptInstance(uint64_t instanceId)
    {
        if (RuntimeInstance* runtimeInstance = FindMutableRuntimeInstance(instanceId))
        {
            if (runtimeInstance->Object.IsValid())
                runtimeInstance->Object.Destroy();
        }
        s_HostState.RuntimeInstances.erase(instanceId);
    }

    const DiscoverySnapshot& GetSnapshot()
    {
        return s_HostState.Snapshot;
    }
}
