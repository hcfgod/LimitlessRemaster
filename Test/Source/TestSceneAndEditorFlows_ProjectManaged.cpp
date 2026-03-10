#include "TestSceneAndEditorFlowsShared.h"

using namespace SceneEditorFlowTestSupport;

TEST_SUITE("Scene And Editor Flows")
{
    TEST_CASE("Project lighting settings save-load round-trips and clamps invalid values")
    {
        namespace Project = Limitless::Project;

        const std::filesystem::path projectRoot = MakeTempProjectRoot("LightingProjectSettings");
        std::error_code errorCode;
        std::filesystem::remove_all(projectRoot, errorCode);
        std::filesystem::create_directories(projectRoot, errorCode);
        REQUIRE(!errorCode);

        Project::Lighting2DSettings authoredSettings{};
        authoredSettings.Enabled = true;
        authoredSettings.EnableNormalMaps = true;
        authoredSettings.EnableShadows = true;
        authoredSettings.AmbientColor[0] = 0.2f;
        authoredSettings.AmbientColor[1] = 0.15f;
        authoredSettings.AmbientColor[2] = 0.1f;
        authoredSettings.AmbientIntensity = 0.75f;
        authoredSettings.ShadowQualityLevel = 2;
        authoredSettings.MaxDirectionalLights = 3;
        authoredSettings.MaxPointLights = 24;
        authoredSettings.MaxShadowSegments = 196;
        authoredSettings.ShadowSoftnessScale = 1.35f;
        authoredSettings.DirectionalShadowBiasScale = 1.4f;
        authoredSettings.ShadowAlphaCutoff = 0.62f;
        authoredSettings.ShadowSegmentSnapPixels = 1.0f;
        authoredSettings.EnableHighAngularVelocityShadowFreeze = true;
        authoredSettings.ShadowFreezeAngularVelocityDegreesPerSecond = 240.0f;
        authoredSettings.ShadowFreezeFrameCount = 3;
        authoredSettings.MaxShadowSamplesPerLight = 14;

        const auto saveResult = Project::SaveLighting2DSettings(projectRoot, authoredSettings);
        REQUIRE(saveResult.IsSuccess());

        const auto loadResult = Project::LoadLighting2DSettings(projectRoot);
        REQUIRE(loadResult.IsSuccess());
        const auto& loadedSettings = loadResult.GetValue();
        CHECK(loadedSettings.Enabled == true);
        CHECK(loadedSettings.EnableNormalMaps == true);
        CHECK(loadedSettings.EnableShadows == true);
        CHECK(loadedSettings.AmbientColor[0] == doctest::Approx(0.2f));
        CHECK(loadedSettings.AmbientColor[1] == doctest::Approx(0.15f));
        CHECK(loadedSettings.AmbientColor[2] == doctest::Approx(0.1f));
        CHECK(loadedSettings.AmbientIntensity == doctest::Approx(0.75f));
        CHECK(loadedSettings.ShadowQualityLevel == 2);
        CHECK(loadedSettings.MaxDirectionalLights == 3);
        CHECK(loadedSettings.MaxPointLights == 24);
        CHECK(loadedSettings.MaxShadowSegments == 196);
        CHECK(loadedSettings.ShadowSoftnessScale == doctest::Approx(1.35f));
        CHECK(loadedSettings.DirectionalShadowBiasScale == doctest::Approx(1.4f));
        CHECK(loadedSettings.ShadowAlphaCutoff == doctest::Approx(0.62f));
        CHECK(loadedSettings.ShadowSegmentSnapPixels == doctest::Approx(1.0f));
        CHECK(loadedSettings.EnableHighAngularVelocityShadowFreeze == true);
        CHECK(loadedSettings.ShadowFreezeAngularVelocityDegreesPerSecond == doctest::Approx(240.0f));
        CHECK(loadedSettings.ShadowFreezeFrameCount == 3);
        CHECK(loadedSettings.MaxShadowSamplesPerLight == 14);

        const std::filesystem::path lightingSettingsPath = Project::GetLighting2DSettingsPath(projectRoot);
        {
            nlohmann::json invalidSettings = {
                { "version", 1 },
                { "enabled", true },
                { "enableNormalMaps", true },
                { "enableShadows", true },
                { "ambientColor", { 1.2f, -0.6f, 0.3f } },
                { "ambientIntensity", -8.0f },
                { "shadowQualityLevel", 99 },
                { "maxDirectionalLights", -5 },
                { "maxPointLights", -12 },
                { "maxShadowSegments", 0 },
                { "shadowSoftnessScale", -3.0f },
                { "directionalShadowBiasScale", -4.0f },
                { "shadowAlphaCutoff", 2.0f },
                { "shadowSegmentSnapPixels", -2.0f },
                { "enableHighAngularVelocityShadowFreeze", true },
                { "shadowFreezeAngularVelocityDegreesPerSecond", -20.0f },
                { "shadowFreezeFrameCount", 0 },
                { "maxShadowSamplesPerLight", 0 }
            };

            std::filesystem::create_directories(lightingSettingsPath.parent_path(), errorCode);
            REQUIRE(!errorCode);
            std::ofstream stream(lightingSettingsPath, std::ios::binary);
            REQUIRE(stream.is_open());
            stream << invalidSettings.dump(2);
        }

        const auto clampedResult = Project::LoadLighting2DSettings(projectRoot);
        REQUIRE(clampedResult.IsSuccess());
        const auto& clampedSettings = clampedResult.GetValue();
        CHECK(clampedSettings.AmbientIntensity == doctest::Approx(0.0f));
        CHECK(clampedSettings.ShadowQualityLevel == 2);
        CHECK(clampedSettings.MaxDirectionalLights == 0);
        CHECK(clampedSettings.MaxPointLights == 0);
        CHECK(clampedSettings.MaxShadowSegments == 1);
        CHECK(clampedSettings.ShadowSoftnessScale == doctest::Approx(0.0f));
        CHECK(clampedSettings.DirectionalShadowBiasScale == doctest::Approx(0.0f));
        CHECK(clampedSettings.ShadowAlphaCutoff == doctest::Approx(1.0f));
        CHECK(clampedSettings.ShadowSegmentSnapPixels == doctest::Approx(0.0f));
        CHECK(clampedSettings.EnableHighAngularVelocityShadowFreeze == true);
        CHECK(clampedSettings.ShadowFreezeAngularVelocityDegreesPerSecond == doctest::Approx(1.0f));
        CHECK(clampedSettings.ShadowFreezeFrameCount == 1);
        CHECK(clampedSettings.MaxShadowSamplesPerLight == 1);

        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("Managed payload manifest validates required files and rejects API mismatches")
    {
        const std::filesystem::path payloadDirectory = MakeTempScenePath("ManagedPayloadValidation") / "Managed";
        std::error_code errorCode;
        std::filesystem::remove_all(payloadDirectory.parent_path(), errorCode);
        errorCode.clear();
        std::filesystem::create_directories(payloadDirectory, errorCode);
        REQUIRE(!errorCode);

        auto writeTextFile = [](const std::filesystem::path& filePath, const std::string& contents) {
            std::ofstream output(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
            REQUIRE(output.is_open());
            output << contents;
        };

        writeTextFile(payloadDirectory / "Coral.Managed.dll", "placeholder");
        writeTextFile(payloadDirectory / "Coral.Managed.runtimeconfig.json", "{}");
        writeTextFile(payloadDirectory / "Limitless.Managed.dll", "placeholder");
        writeTextFile(payloadDirectory / "Limitless.Managed.runtimeconfig.json", "{}");
        writeTextFile(payloadDirectory / "Limitless.Managed.TestScripts.dll", "placeholder");
        writeTextFile(payloadDirectory / "Project.ManagedScripts.dll", "placeholder");

        nlohmann::json manifestJson;
        manifestJson["formatVersion"] = Limitless::ManagedScriptPayload::PayloadManifestFormatVersion;
        manifestJson["apiVersion"] = Limitless::ManagedScriptPayload::HostApiVersion;
        manifestJson["coralManagedAssembly"] = "Coral.Managed.dll";
        manifestJson["coralManagedRuntimeConfig"] = "Coral.Managed.runtimeconfig.json";
        manifestJson["contractAssembly"] = "Limitless.Managed.dll";
        manifestJson["contractRuntimeConfig"] = "Limitless.Managed.runtimeconfig.json";
        manifestJson["scriptAssemblies"] = nlohmann::json::array({ "Limitless.Managed.TestScripts.dll", "Project.ManagedScripts.dll" });
        manifestJson["buildConfiguration"] = "Debug";
        manifestJson["targetOS"] = "Windows";
        manifestJson["targetArchitecture"] = "x64";

        writeTextFile(payloadDirectory / Limitless::ManagedScriptPayload::PayloadManifestFileName, manifestJson.dump(2));

        Limitless::ManagedScriptPayload::PayloadManifest loadedManifest{};
        std::string validationError;
        CHECK(Limitless::ManagedScriptPayload::ValidatePayloadDirectory(payloadDirectory, &loadedManifest, &validationError));
        CHECK(validationError.empty());
        CHECK(loadedManifest.ApiVersion == Limitless::ManagedScriptPayload::HostApiVersion);
        CHECK(loadedManifest.ScriptAssemblies.size() == 2);
        CHECK(loadedManifest.ScriptAssemblies[0] == "Limitless.Managed.TestScripts.dll");
        CHECK(loadedManifest.ScriptAssemblies[1] == "Project.ManagedScripts.dll");

        manifestJson["apiVersion"] = Limitless::ManagedScriptPayload::HostApiVersion + 1;
        writeTextFile(payloadDirectory / Limitless::ManagedScriptPayload::PayloadManifestFileName, manifestJson.dump(2));

        CHECK_FALSE(Limitless::ManagedScriptPayload::ValidatePayloadDirectory(payloadDirectory, nullptr, &validationError));
        CHECK_FALSE(validationError.empty());
        CHECK(validationError.find("API version mismatch") != std::string::npos);

        manifestJson["apiVersion"] = Limitless::ManagedScriptPayload::HostApiVersion;
        writeTextFile(payloadDirectory / Limitless::ManagedScriptPayload::PayloadManifestFileName, manifestJson.dump(2));

        std::filesystem::remove(payloadDirectory / "Limitless.Managed.runtimeconfig.json", errorCode);
        errorCode.clear();

        CHECK_FALSE(Limitless::ManagedScriptPayload::ValidatePayloadDirectory(payloadDirectory, nullptr, &validationError));
        CHECK_FALSE(validationError.empty());
        CHECK(validationError.find("missing required file") != std::string::npos);

        std::filesystem::remove_all(payloadDirectory.parent_path(), errorCode);
    }

    TEST_CASE("Scene load accepts legacy singular native and managed script fields")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity root = scene.CreateEntity("LegacyScriptRoot");
        REQUIRE_FALSE(IsNullEntity(root));

        auto& nativeScript = AttachScriptEntry(scene, root);
        nativeScript.ScriptClassName = "LegacyNativeScript";
        nativeScript.ScriptAssetRelativePath = "Gameplay/Legacy/LegacyNativeScript";
        nativeScript.Enabled = false;
        nativeScript.ExecutionPolicy = Limitless::ScriptExecutionPolicy::ParallelSafe;
        nativeScript.DeclaredReadAccessMask = 3;
        nativeScript.DeclaredWriteAccessMask = 5;
        nativeScript.ExposedProperties["Counter"] = int32_t(12);

        Limitless::ManagedScriptEntry managedScript{};
        managedScript.ScriptClassName = "Game.ManagedBootstrap";
        managedScript.ScriptAssetRelativePath = "Gameplay/Legacy/ManagedBootstrap";
        managedScript.Enabled = false;
        managedScript.ExposedProperties["DisplayName"] = std::string("LegacyRoot");

        const entt::entity managedScriptEntity = scene.AttachManagedScriptComponent(root, std::move(managedScript));
        REQUIRE_FALSE(IsNullEntity(managedScriptEntity));
        REQUIRE(registry.all_of<Limitless::ScriptComponent>(managedScriptEntity));

        const std::filesystem::path scenePath = MakeTempScenePath("LegacyScriptCompatibility.scene.json");
        std::error_code errorCode;
        std::filesystem::create_directories(scenePath.parent_path(), errorCode);
        REQUIRE(!errorCode);

        const auto saveResult = scene.SaveToFile(scenePath);
        REQUIRE(saveResult.IsSuccess());

        std::ifstream sceneFile(scenePath, std::ios::binary);
        REQUIRE(sceneFile.is_open());
        nlohmann::json rootJson;
        sceneFile >> rootJson;
        sceneFile.close();

        REQUIRE(rootJson.is_object());
        REQUIRE(rootJson.contains("Entities"));
        REQUIRE(rootJson["Entities"].is_array());

        nlohmann::json* legacyEntityJson = nullptr;
        for (auto& entityJson : rootJson["Entities"])
        {
            if (entityJson.is_object() && entityJson.value("Tag", std::string{}) == "LegacyScriptRoot")
            {
                legacyEntityJson = &entityJson;
                break;
            }
        }

        REQUIRE(legacyEntityJson != nullptr);
        if (legacyEntityJson == nullptr)
            return;
        REQUIRE(legacyEntityJson->contains("NativeScripts"));
        REQUIRE((*legacyEntityJson)["NativeScripts"].is_array());
        REQUIRE((*legacyEntityJson)["NativeScripts"].size() == 1);
        REQUIRE(legacyEntityJson->contains("ManagedScripts"));
        REQUIRE((*legacyEntityJson)["ManagedScripts"].is_array());
        REQUIRE((*legacyEntityJson)["ManagedScripts"].size() == 1);

        (*legacyEntityJson)["NativeScript"] = (*legacyEntityJson)["NativeScripts"].front();
        legacyEntityJson->erase("NativeScripts");
        (*legacyEntityJson)["ManagedScript"] = (*legacyEntityJson)["ManagedScripts"].front();
        legacyEntityJson->erase("ManagedScripts");

        std::ofstream legacySceneFile(scenePath, std::ios::binary | std::ios::trunc);
        REQUIRE(legacySceneFile.is_open());
        legacySceneFile << rootJson.dump(2);
        legacySceneFile.close();

        const auto loadResult = Limitless::Scene::LoadFromFile(scenePath);
        REQUIRE(loadResult.IsSuccess());
        REQUIRE(loadResult.GetValue() != nullptr);
        if (loadResult.GetValue() == nullptr)
            return;

        const auto& loadedScene = *loadResult.GetValue();
        const auto& loadedRegistry = loadedScene.GetRegistry();
        const entt::entity loadedRoot = FindEntityByTag(loadedScene, "LegacyScriptRoot");
        REQUIRE_FALSE(IsNullEntity(loadedRoot));

        const auto loadedScriptEntities = loadedScene.GetScriptComponentEntities(loadedRoot);
        REQUIRE(loadedScriptEntities.size() == 2);

        const Limitless::NativeScriptEntry* loadedNativeScript = nullptr;
        const Limitless::ManagedScriptEntry* loadedManagedScript = nullptr;
        for (const entt::entity scriptEntity : loadedScriptEntities)
        {
            const auto& scriptComponent = loadedRegistry.get<Limitless::ScriptComponent>(scriptEntity);
            if (const auto* nativeEntry = scriptComponent.TryGetNativeEntry())
                loadedNativeScript = nativeEntry;
            else if (const auto* managedEntry = scriptComponent.TryGetManagedEntry())
                loadedManagedScript = managedEntry;
        }

        REQUIRE(loadedNativeScript != nullptr);
        if (loadedNativeScript == nullptr)
            return;
        CHECK(loadedNativeScript->ScriptClassName == "LegacyNativeScript");
        CHECK(loadedNativeScript->ScriptAssetRelativePath == "Gameplay/Legacy/LegacyNativeScript");
        CHECK(loadedNativeScript->Enabled == false);
        CHECK(loadedNativeScript->ExecutionPolicy == Limitless::ScriptExecutionPolicy::ParallelSafe);
        CHECK(loadedNativeScript->DeclaredReadAccessMask == 3);
        CHECK(loadedNativeScript->DeclaredWriteAccessMask == 5);
        const bool hasCounter = loadedNativeScript->ExposedProperties.contains("Counter");
        REQUIRE(hasCounter);
        if (!hasCounter)
            return;
        const auto* loadedCounter = std::get_if<int32_t>(&loadedNativeScript->ExposedProperties.at("Counter"));
        REQUIRE(loadedCounter != nullptr);
        if (loadedCounter == nullptr)
            return;
        CHECK(*loadedCounter == 12);
        CHECK(loadedNativeScript->RuntimeInitialized == false);
        CHECK(loadedNativeScript->RuntimeUpdateCount == 0);
        CHECK(loadedNativeScript->RuntimeWarnedMissingCompiledScript == false);

        REQUIRE(loadedManagedScript != nullptr);
        if (loadedManagedScript == nullptr)
            return;
        CHECK(loadedManagedScript->ScriptClassName == "Game.ManagedBootstrap");
        CHECK(loadedManagedScript->ScriptAssetRelativePath == "Gameplay/Legacy/ManagedBootstrap");
        CHECK(loadedManagedScript->Enabled == false);
        const bool hasDisplayName = loadedManagedScript->ExposedProperties.contains("DisplayName");
        REQUIRE(hasDisplayName);
        if (!hasDisplayName)
            return;
        const auto* loadedDisplayName = std::get_if<std::string>(&loadedManagedScript->ExposedProperties.at("DisplayName"));
        REQUIRE(loadedDisplayName != nullptr);
        if (loadedDisplayName == nullptr)
            return;
        CHECK(*loadedDisplayName == "LegacyRoot");
        CHECK(loadedManagedScript->RuntimeInstanceId == 0);
        CHECK(loadedManagedScript->RuntimeInitialized == false);
        CHECK(loadedManagedScript->RuntimeUpdateCount == 0);
        CHECK(loadedManagedScript->RuntimeWarnedMissingHost == false);
        CHECK(loadedManagedScript->RuntimeWarnedMissingClass == false);

        std::filesystem::remove(scenePath, errorCode);
    }

    TEST_CASE("Mixed native and managed script components preserve authored data across clone serialization and prefab instantiation")
    {
        Limitless::Scene scene;
        auto& registry = scene.GetRegistry();

        const entt::entity root = scene.CreateEntity("MixedScriptRoot");
        REQUIRE_FALSE(IsNullEntity(root));
        const entt::entity target = scene.CreateEntity("MixedScriptTarget");
        REQUIRE_FALSE(IsNullEntity(target));

        auto& nativeScript = AttachScriptEntry(scene, root);
        nativeScript.ScriptClassName = "MixedNativeScript";
        nativeScript.ScriptAssetRelativePath = "Gameplay/Mixed/MixedNativeScript";
        nativeScript.Enabled = true;
        nativeScript.ExecutionPolicy = Limitless::ScriptExecutionPolicy::MainThread;
        nativeScript.ExposedProperties["Speed"] = 3.5f;
        nativeScript.RuntimeInitialized = true;
        nativeScript.RuntimeUpdateCount = 11;
        nativeScript.RuntimeWarnedMissingCompiledScript = true;

        Limitless::ManagedScriptEntry managedScript{};
        managedScript.ScriptClassName = "Game.ManagedBootstrap";
        managedScript.ScriptAssetRelativePath = "Gameplay/Mixed/MixedManagedScript";
        managedScript.Enabled = true;
        managedScript.ExposedProperties["Target"] = Limitless::ScriptEntityReference{ "MixedScriptTarget" };
        managedScript.ExposedProperties["Count"] = int32_t(2);
        managedScript.RuntimeInstanceId = 99;
        managedScript.RuntimeInitialized = true;
        managedScript.RuntimeUpdateCount = 13;
        managedScript.RuntimeWarnedMissingHost = true;
        managedScript.RuntimeWarnedMissingClass = true;

        const entt::entity managedScriptEntity = scene.AttachManagedScriptComponent(root, std::move(managedScript));
        REQUIRE_FALSE(IsNullEntity(managedScriptEntity));
        REQUIRE(registry.all_of<Limitless::ScriptComponent>(managedScriptEntity));

        const auto verifyMixedScripts = [](const Limitless::Scene& inspectedScene, entt::entity ownerEntity) {
            const auto& inspectedRegistry = inspectedScene.GetRegistry();
            const auto scriptEntities = inspectedScene.GetScriptComponentEntities(ownerEntity);
            const auto scriptEntityCount = scriptEntities.size();
            REQUIRE(scriptEntityCount == 2);
            if (scriptEntityCount != 2)
                return;

            const Limitless::NativeScriptEntry* foundNativeScript = nullptr;
            const Limitless::ManagedScriptEntry* foundManagedScript = nullptr;
            for (const entt::entity scriptEntity : scriptEntities)
            {
                const auto& scriptComponent = inspectedRegistry.get<Limitless::ScriptComponent>(scriptEntity);
                if (const auto* nativeEntry = scriptComponent.TryGetNativeEntry())
                    foundNativeScript = nativeEntry;
                else if (const auto* managedEntry = scriptComponent.TryGetManagedEntry())
                    foundManagedScript = managedEntry;
            }

            REQUIRE(foundNativeScript != nullptr);
            if (foundNativeScript == nullptr)
                return;
            CHECK(foundNativeScript->ScriptClassName == "MixedNativeScript");
            CHECK(foundNativeScript->ScriptAssetRelativePath == "Gameplay/Mixed/MixedNativeScript");
            CHECK(foundNativeScript->Enabled == true);
            const bool hasSpeed = foundNativeScript->ExposedProperties.contains("Speed");
            REQUIRE(hasSpeed);
            if (!hasSpeed)
                return;
            const auto* foundSpeed = std::get_if<float>(&foundNativeScript->ExposedProperties.at("Speed"));
            REQUIRE(foundSpeed != nullptr);
            if (foundSpeed == nullptr)
                return;
            CHECK(*foundSpeed == doctest::Approx(3.5f));
            CHECK(foundNativeScript->RuntimeInitialized == false);
            CHECK(foundNativeScript->RuntimeUpdateCount == 0);
            CHECK(foundNativeScript->RuntimeWarnedMissingCompiledScript == false);

            REQUIRE(foundManagedScript != nullptr);
            if (foundManagedScript == nullptr)
                return;
            CHECK(foundManagedScript->ScriptClassName == "Game.ManagedBootstrap");
            CHECK(foundManagedScript->ScriptAssetRelativePath == "Gameplay/Mixed/MixedManagedScript");
            CHECK(foundManagedScript->Enabled == true);
            const bool hasCount = foundManagedScript->ExposedProperties.contains("Count");
            REQUIRE(hasCount);
            if (!hasCount)
                return;
            const auto* foundCount = std::get_if<int32_t>(&foundManagedScript->ExposedProperties.at("Count"));
            REQUIRE(foundCount != nullptr);
            if (foundCount == nullptr)
                return;
            CHECK(*foundCount == 2);
            const bool hasTarget = foundManagedScript->ExposedProperties.contains("Target");
            REQUIRE(hasTarget);
            if (!hasTarget)
                return;
            const auto* foundTarget = std::get_if<Limitless::ScriptEntityReference>(&foundManagedScript->ExposedProperties.at("Target"));
            REQUIRE(foundTarget != nullptr);
            if (foundTarget == nullptr)
                return;
            CHECK(foundTarget->Tag == "MixedScriptTarget");
            CHECK(foundManagedScript->RuntimeInstanceId == 0);
            CHECK(foundManagedScript->RuntimeInitialized == false);
            CHECK(foundManagedScript->RuntimeUpdateCount == 0);
            CHECK(foundManagedScript->RuntimeWarnedMissingHost == false);
            CHECK(foundManagedScript->RuntimeWarnedMissingClass == false);
        };

        auto clone = scene.Clone();
        REQUIRE(clone != nullptr);
        const entt::entity clonedRoot = FindEntityByTag(*clone, "MixedScriptRoot");
        REQUIRE_FALSE(IsNullEntity(clonedRoot));
        verifyMixedScripts(*clone, clonedRoot);

        const std::filesystem::path scenePath = MakeTempScenePath("MixedBackendScripts.scene.json");
        std::error_code errorCode;
        std::filesystem::create_directories(scenePath.parent_path(), errorCode);
        REQUIRE(!errorCode);

        const auto saveResult = scene.SaveToFile(scenePath);
        REQUIRE(saveResult.IsSuccess());

        const auto loadResult = Limitless::Scene::LoadFromFile(scenePath);
        REQUIRE(loadResult.IsSuccess());
        REQUIRE(loadResult.GetValue() != nullptr);
        if (loadResult.GetValue() == nullptr)
            return;
        const entt::entity loadedRoot = FindEntityByTag(*loadResult.GetValue(), "MixedScriptRoot");
        REQUIRE_FALSE(IsNullEntity(loadedRoot));
        verifyMixedScripts(*loadResult.GetValue(), loadedRoot);

        Limitless::Scene destinationScene;
        const entt::entity instantiatedRoot = destinationScene.InstantiatePrefab(scenePath.string());
        REQUIRE_FALSE(IsNullEntity(instantiatedRoot));
        verifyMixedScripts(destinationScene, instantiatedRoot);

        std::filesystem::remove(scenePath, errorCode);
    }
}
