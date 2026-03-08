#include "Scripting/ManagedScriptPayload.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace Limitless::ManagedScriptPayload
{
    namespace
    {
        using json = nlohmann::json;

        bool FileExists(const std::filesystem::path& filePath)
        {
            std::error_code errorCode;
            return std::filesystem::exists(filePath, errorCode) && !errorCode && std::filesystem::is_regular_file(filePath, errorCode);
        }

        bool DirectoryExists(const std::filesystem::path& directoryPath)
        {
            std::error_code errorCode;
            return std::filesystem::exists(directoryPath, errorCode) && !errorCode && std::filesystem::is_directory(directoryPath, errorCode);
        }

        void SetError(std::string* errorMessage, const std::string& value)
        {
            if (errorMessage != nullptr)
                *errorMessage = value;
        }
    }

    std::filesystem::path GetPayloadManifestPath(const std::filesystem::path& managedDirectory)
    {
        return managedDirectory / PayloadManifestFileName;
    }

    bool LoadPayloadManifest(const std::filesystem::path& managedDirectory,
                             PayloadManifest& outManifest,
                             std::string* errorMessage)
    {
        outManifest = {};

        if (!DirectoryExists(managedDirectory))
        {
            SetError(errorMessage, "Managed payload directory does not exist: " + managedDirectory.string());
            return false;
        }

        const std::filesystem::path manifestPath = GetPayloadManifestPath(managedDirectory);
        if (!FileExists(manifestPath))
        {
            SetError(errorMessage, "Managed payload manifest not found: " + manifestPath.string());
            return false;
        }

        try
        {
            std::ifstream input(manifestPath, std::ios::in | std::ios::binary);
            if (!input.is_open())
            {
                SetError(errorMessage, "Failed to open managed payload manifest: " + manifestPath.string());
                return false;
            }

            json root;
            input >> root;

            outManifest.FormatVersion = root.value("formatVersion", 0u);
            outManifest.ApiVersion = root.value("apiVersion", 0u);
            outManifest.CoralManagedAssembly = root.value("coralManagedAssembly", std::string{});
            outManifest.CoralManagedRuntimeConfig = root.value("coralManagedRuntimeConfig", std::string{});
            outManifest.ContractAssembly = root.value("contractAssembly", std::string{});
            outManifest.ContractRuntimeConfig = root.value("contractRuntimeConfig", std::string{});
            outManifest.TargetOS = root.value("targetOS", std::string{});
            outManifest.TargetArchitecture = root.value("targetArchitecture", std::string{});
            outManifest.BuildConfiguration = root.value("buildConfiguration", std::string{});

            if (root.contains("scriptAssemblies") && root["scriptAssemblies"].is_array())
            {
                for (const auto& item : root["scriptAssemblies"])
                {
                    if (item.is_string())
                        outManifest.ScriptAssemblies.push_back(item.get<std::string>());
                }
            }
        }
        catch (const std::exception& exception)
        {
            SetError(errorMessage, std::string("Failed to parse managed payload manifest: ") + exception.what());
            return false;
        }

        if (errorMessage != nullptr)
            errorMessage->clear();
        return true;
    }

    bool ValidatePayloadDirectory(const std::filesystem::path& managedDirectory,
                                  PayloadManifest* outManifest,
                                  std::string* errorMessage)
    {
        PayloadManifest manifest{};
        if (!LoadPayloadManifest(managedDirectory, manifest, errorMessage))
            return false;

        if (manifest.FormatVersion != PayloadManifestFormatVersion)
        {
            SetError(errorMessage,
                     "Managed payload manifest format mismatch for '" + managedDirectory.string() +
                         "'. Expected " + std::to_string(PayloadManifestFormatVersion) +
                         ", found " + std::to_string(manifest.FormatVersion) + ".");
            return false;
        }

        if (manifest.ApiVersion != HostApiVersion)
        {
            SetError(errorMessage,
                     "Managed payload API version mismatch for '" + managedDirectory.string() +
                         "'. Expected " + std::to_string(HostApiVersion) +
                         ", found " + std::to_string(manifest.ApiVersion) + ".");
            return false;
        }

        if (manifest.CoralManagedAssembly.empty())
            manifest.CoralManagedAssembly = CoralManagedAssemblyFileName;
        if (manifest.CoralManagedRuntimeConfig.empty())
            manifest.CoralManagedRuntimeConfig = CoralManagedRuntimeConfigFileName;
        if (manifest.ContractAssembly.empty())
            manifest.ContractAssembly = ContractAssemblyFileName;
        if (manifest.ContractRuntimeConfig.empty())
            manifest.ContractRuntimeConfig = ContractRuntimeConfigFileName;

        const std::vector<std::filesystem::path> requiredFiles = {
            managedDirectory / manifest.CoralManagedAssembly,
            managedDirectory / manifest.CoralManagedRuntimeConfig,
            managedDirectory / manifest.ContractAssembly,
            managedDirectory / manifest.ContractRuntimeConfig,
        };
        for (const auto& requiredFile : requiredFiles)
        {
            if (!FileExists(requiredFile))
            {
                SetError(errorMessage, "Managed payload is missing required file: " + requiredFile.string());
                return false;
            }
        }

        for (const std::string& assemblyName : manifest.ScriptAssemblies)
        {
            if (assemblyName.empty())
                continue;

            const std::filesystem::path assemblyPath = managedDirectory / assemblyName;
            if (!FileExists(assemblyPath))
            {
                SetError(errorMessage, "Managed payload is missing declared script assembly: " + assemblyPath.string());
                return false;
            }
        }

        if (outManifest != nullptr)
            *outManifest = std::move(manifest);
        if (errorMessage != nullptr)
            errorMessage->clear();
        return true;
    }
}
