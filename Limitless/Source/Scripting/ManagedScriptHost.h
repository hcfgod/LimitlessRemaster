#pragma once

#include "Scripting/ManagedScriptPayload.h"
#include "Scripting/ScriptProperty.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    class Scene;
}

namespace Limitless::ManagedScriptHost
{
    struct ReflectedFieldDefinition final
    {
        std::string Name;
        ScriptPropertyType Type = ScriptPropertyType::Float;
        ScriptPropertyValue DefaultValue = 0.0f;
    };

    struct DiscoveredScriptClass final
    {
        std::string FullName;
        std::string AssemblyName;
        std::filesystem::path AssemblyPath;
        std::vector<ReflectedFieldDefinition> ReflectedFields;
    };

    struct DiscoverySnapshot final
    {
        bool HostInitialized = false;
        std::filesystem::path ManagedDirectory;
        std::filesystem::path LoadedManagedDirectory;
        uint32_t PayloadApiVersion = 0;
        std::vector<DiscoveredScriptClass> Classes;
    };

    bool Initialize(const std::filesystem::path& managedDirectory);
    void Shutdown();
    bool IsInitialized();
    std::string ResolveDiscoveredClassName(std::string_view className);
    bool HasDiscoveredClass(std::string_view className);
    uint64_t CreateScriptInstance(std::string_view className, uint32_t entityHandle, std::string* errorMessage = nullptr);
    bool SynchronizeScriptExposedProperties(uint64_t instanceId,
                                            Scene* scene,
                                            const std::unordered_map<std::string, ScriptPropertyValue>& exposedProperties,
                                            uint64_t revision,
                                            std::string* errorMessage = nullptr);
    bool InvokeScriptOnCreate(uint64_t instanceId, Scene* scene, std::string* errorMessage = nullptr);
    bool InvokeScriptOnFixedUpdate(uint64_t instanceId, Scene* scene, float fixedDeltaTime, std::string* errorMessage = nullptr);
    bool InvokeScriptOnUpdate(uint64_t instanceId, Scene* scene, float deltaTime, std::string* errorMessage = nullptr);
    bool InvokeScriptOnCollisionEnter(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage = nullptr);
    bool InvokeScriptOnCollisionStay(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage = nullptr);
    bool InvokeScriptOnCollisionExit(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage = nullptr);
    bool InvokeScriptOnTriggerEnter(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage = nullptr);
    bool InvokeScriptOnTriggerStay(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage = nullptr);
    bool InvokeScriptOnTriggerExit(uint64_t instanceId, Scene* scene, uint32_t otherEntityHandle, std::string* errorMessage = nullptr);
    bool InvokeScriptOnDestroy(uint64_t instanceId, Scene* scene, std::string* errorMessage = nullptr);
    void DestroyScriptInstance(uint64_t instanceId);
    const DiscoverySnapshot& GetSnapshot();
}
