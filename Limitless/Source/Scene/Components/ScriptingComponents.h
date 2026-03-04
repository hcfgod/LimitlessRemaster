#pragma once

#include "Scripting/ScriptProperty.h"
#include "Scripting/ScriptableEntity.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
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

    /// Native C++ behavior scripts attached to an entity (Unity-style list).
    struct NativeScriptComponent
    {
        std::vector<NativeScriptEntry> Scripts;
    };
}
