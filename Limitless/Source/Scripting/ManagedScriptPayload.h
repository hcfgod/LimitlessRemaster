#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Limitless::ManagedScriptPayload
{
    inline constexpr uint32_t PayloadManifestFormatVersion = 1;
    inline constexpr uint32_t HostApiVersion = 1;
    inline constexpr const char* ManagedDirectoryName = "Managed";
    inline constexpr const char* PayloadManifestFileName = "Limitless.Managed.payload.json";
    inline constexpr const char* CoralManagedAssemblyFileName = "Coral.Managed.dll";
    inline constexpr const char* CoralManagedRuntimeConfigFileName = "Coral.Managed.runtimeconfig.json";
    inline constexpr const char* ContractAssemblyFileName = "Limitless.Managed.dll";
    inline constexpr const char* ContractRuntimeConfigFileName = "Limitless.Managed.runtimeconfig.json";

    struct PayloadManifest final
    {
        uint32_t FormatVersion = 0;
        uint32_t ApiVersion = 0;
        std::string CoralManagedAssembly;
        std::string CoralManagedRuntimeConfig;
        std::string ContractAssembly;
        std::string ContractRuntimeConfig;
        std::vector<std::string> ScriptAssemblies;
        std::string TargetOS;
        std::string TargetArchitecture;
        std::string BuildConfiguration;
    };

    std::filesystem::path GetPayloadManifestPath(const std::filesystem::path& managedDirectory);
    bool LoadPayloadManifest(const std::filesystem::path& managedDirectory,
                             PayloadManifest& outManifest,
                             std::string* errorMessage = nullptr);
    bool ValidatePayloadDirectory(const std::filesystem::path& managedDirectory,
                                  PayloadManifest* outManifest = nullptr,
                                  std::string* errorMessage = nullptr);
}
