#pragma once

#include "EnTT/entt.hpp"
#include "Scripting/ScriptProperty.h"
#include "Scripting/ScriptableEntity.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    enum class ScriptBackend : uint8_t
    {
        Native = 0,
        Managed = 1
    };

    enum class ScriptExecutionPolicy : uint8_t
    {
        MainThread = 0,
        ParallelSafe = 1
    };

    /// Single native C++ behavior script entry attached to an entity.
    struct NativeScriptEntry
    {
        std::string ScriptClassName;
        std::string ScriptAssetRelativePath; ///< Relative to project Assets root without extension (example: "Gameplay/Player/PlayerController")
        bool Enabled = true;
        ScriptExecutionPolicy ExecutionPolicy = ScriptExecutionPolicy::MainThread;
        uint64_t DeclaredReadAccessMask = 0;
        uint64_t DeclaredWriteAccessMask = 0;
        std::unordered_map<std::string, ScriptPropertyValue> ExposedProperties;
        uint64_t RuntimeExposedPropertiesRevision = 1;

        // Runtime-only state (not serialized).
        std::unique_ptr<ScriptableEntity> RuntimeInstance;
        bool RuntimeInitialized = false;
        uint64_t RuntimeUpdateCount = 0;
        bool RuntimeWarnedOnUpdateTransformMutation = false;
        bool RuntimeWarnedMissingCompiledScript = false;
        bool RuntimeWarnedMissingAccessDeclaration = false;
        bool RuntimeWarnedAccessMaskMismatch = false;

        NativeScriptEntry() = default;

        NativeScriptEntry(const NativeScriptEntry& other)
            : ScriptClassName(other.ScriptClassName),
              ScriptAssetRelativePath(other.ScriptAssetRelativePath),
              Enabled(other.Enabled),
              ExecutionPolicy(other.ExecutionPolicy),
              DeclaredReadAccessMask(other.DeclaredReadAccessMask),
              DeclaredWriteAccessMask(other.DeclaredWriteAccessMask),
              ExposedProperties(other.ExposedProperties),
              RuntimeExposedPropertiesRevision(1),
              RuntimeInstance(nullptr),
              RuntimeInitialized(false),
              RuntimeUpdateCount(0),
              RuntimeWarnedOnUpdateTransformMutation(false),
              RuntimeWarnedMissingCompiledScript(false),
              RuntimeWarnedMissingAccessDeclaration(false),
              RuntimeWarnedAccessMaskMismatch(false)
        {
        }

        NativeScriptEntry& operator=(const NativeScriptEntry& other)
        {
            if (this == &other)
                return *this;
            ScriptClassName = other.ScriptClassName;
            ScriptAssetRelativePath = other.ScriptAssetRelativePath;
            Enabled = other.Enabled;
            ExecutionPolicy = other.ExecutionPolicy;
            DeclaredReadAccessMask = other.DeclaredReadAccessMask;
            DeclaredWriteAccessMask = other.DeclaredWriteAccessMask;
            ExposedProperties = other.ExposedProperties;
            RuntimeExposedPropertiesRevision = 1;
            RuntimeInstance.reset();
            RuntimeInitialized = false;
            RuntimeUpdateCount = 0;
            RuntimeWarnedOnUpdateTransformMutation = false;
            RuntimeWarnedMissingCompiledScript = false;
            RuntimeWarnedMissingAccessDeclaration = false;
            RuntimeWarnedAccessMaskMismatch = false;
            return *this;
        }

        NativeScriptEntry(NativeScriptEntry&&) noexcept = default;
        NativeScriptEntry& operator=(NativeScriptEntry&&) noexcept = default;
    };

    struct ManagedScriptEntry
    {
        std::string ScriptClassName;
        std::string ScriptAssetRelativePath;
        bool Enabled = true;
        std::unordered_map<std::string, ScriptPropertyValue> ExposedProperties;
        uint64_t RuntimeExposedPropertiesRevision = 1;
        uint64_t RuntimeInstanceId = 0;
        bool RuntimeInitialized = false;
        uint64_t RuntimeUpdateCount = 0;
        bool RuntimeWarnedOnUpdateTransformMutation = false;
        bool RuntimeWarnedMissingHost = false;
        bool RuntimeWarnedMissingClass = false;

        ManagedScriptEntry() = default;

        ManagedScriptEntry(const ManagedScriptEntry& other)
            : ScriptClassName(other.ScriptClassName),
              ScriptAssetRelativePath(other.ScriptAssetRelativePath),
              Enabled(other.Enabled),
              ExposedProperties(other.ExposedProperties),
              RuntimeExposedPropertiesRevision(1),
              RuntimeInstanceId(0),
              RuntimeInitialized(false),
              RuntimeUpdateCount(0),
              RuntimeWarnedOnUpdateTransformMutation(false),
              RuntimeWarnedMissingHost(false),
              RuntimeWarnedMissingClass(false)
        {
        }

        ManagedScriptEntry& operator=(const ManagedScriptEntry& other)
        {
            if (this == &other)
                return *this;
            ScriptClassName = other.ScriptClassName;
            ScriptAssetRelativePath = other.ScriptAssetRelativePath;
            Enabled = other.Enabled;
            ExposedProperties = other.ExposedProperties;
            RuntimeExposedPropertiesRevision = 1;
            RuntimeInstanceId = 0;
            RuntimeInitialized = false;
            RuntimeUpdateCount = 0;
            RuntimeWarnedOnUpdateTransformMutation = false;
            RuntimeWarnedMissingHost = false;
            RuntimeWarnedMissingClass = false;
            return *this;
        }

        ManagedScriptEntry(ManagedScriptEntry&&) noexcept = default;
        ManagedScriptEntry& operator=(ManagedScriptEntry&&) noexcept = default;
    };

    /// Native C++ behavior scripts attached to an entity (Unity-style list).
    struct ScriptComponent
    {
        entt::entity OwnerEntity = entt::null;
        int32_t ComponentOrder = 0;
        ScriptBackend Backend = ScriptBackend::Native;
        NativeScriptEntry Script;
        ManagedScriptEntry ManagedScript;

        bool IsNativeBackend() const
        {
            return Backend == ScriptBackend::Native;
        }

        bool IsManagedBackend() const
        {
            return Backend == ScriptBackend::Managed;
        }

        NativeScriptEntry* TryGetNativeEntry()
        {
            return IsNativeBackend() ? &Script : nullptr;
        }

        const NativeScriptEntry* TryGetNativeEntry() const
        {
            return IsNativeBackend() ? &Script : nullptr;
        }

        ManagedScriptEntry* TryGetManagedEntry()
        {
            return IsManagedBackend() ? &ManagedScript : nullptr;
        }

        const ManagedScriptEntry* TryGetManagedEntry() const
        {
            return IsManagedBackend() ? &ManagedScript : nullptr;
        }

        std::unordered_map<std::string, ScriptPropertyValue>* TryGetExposedProperties()
        {
            if (IsManagedBackend())
                return &ManagedScript.ExposedProperties;
            return &Script.ExposedProperties;
        }

        const std::unordered_map<std::string, ScriptPropertyValue>* TryGetExposedProperties() const
        {
            if (IsManagedBackend())
                return &ManagedScript.ExposedProperties;
            return &Script.ExposedProperties;
        }

        uint64_t* TryGetRuntimeExposedPropertiesRevision()
        {
            if (IsManagedBackend())
                return &ManagedScript.RuntimeExposedPropertiesRevision;
            return &Script.RuntimeExposedPropertiesRevision;
        }

        const uint64_t* TryGetRuntimeExposedPropertiesRevision() const
        {
            if (IsManagedBackend())
                return &ManagedScript.RuntimeExposedPropertiesRevision;
            return &Script.RuntimeExposedPropertiesRevision;
        }

        const std::string& GetScriptClassName() const
        {
            return IsManagedBackend() ? ManagedScript.ScriptClassName : Script.ScriptClassName;
        }

        const std::string& GetScriptAssetRelativePath() const
        {
            return IsManagedBackend() ? ManagedScript.ScriptAssetRelativePath : Script.ScriptAssetRelativePath;
        }

        bool IsEnabled() const
        {
            return IsManagedBackend() ? ManagedScript.Enabled : Script.Enabled;
        }
    };
}
