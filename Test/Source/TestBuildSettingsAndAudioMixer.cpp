#include <doctest/doctest.h>

#include "Audio/AudioMixerAsset.h"
#include "Project/BuildSettings.h"
#include "Scripting/NativeScriptRegistry.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace
{
    std::filesystem::path MakeTempProjectRoot(const std::string& folderName)
    {
        return std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / folderName;
    }
}

TEST_SUITE("BuildSettings and AudioMixer")
{
    TEST_CASE("BuildSettings persists and sanitizes script compile failure policy")
    {
        const std::filesystem::path projectRoot = MakeTempProjectRoot("BuildSettingsScriptPolicy");
        std::error_code errorCode;
        std::filesystem::remove_all(projectRoot, errorCode);
        std::filesystem::create_directories(projectRoot / "Project" / "Settings", errorCode);
        REQUIRE_FALSE(errorCode);

        Limitless::Project::BuildSettings settings{};
        settings.ScriptCompileFailurePolicy = Limitless::Project::ScriptCompileFailurePolicy::BlockPlay;
        const auto saveResult = Limitless::Project::SaveBuildSettings(projectRoot, settings);
        REQUIRE(saveResult.IsSuccess());

        const auto loadedResult = Limitless::Project::LoadBuildSettings(projectRoot);
        REQUIRE(loadedResult.IsSuccess());
        CHECK(loadedResult.GetValue().ScriptCompileFailurePolicy ==
              Limitless::Project::ScriptCompileFailurePolicy::BlockPlay);

        const std::filesystem::path settingsPath = Limitless::Project::GetBuildSettingsPath(projectRoot);
        std::ifstream input(settingsPath, std::ios::in | std::ios::binary);
        REQUIRE(input.is_open());
        nlohmann::json rootJson{};
        input >> rootJson;
        rootJson["scriptCompileFailurePolicy"] = "NotAValidPolicy";
        input.close();

        std::ofstream output(settingsPath, std::ios::out | std::ios::binary | std::ios::trunc);
        REQUIRE(output.is_open());
        output << rootJson.dump(2);
        output.close();

        const auto sanitizedLoadResult = Limitless::Project::LoadBuildSettings(projectRoot);
        REQUIRE(sanitizedLoadResult.IsSuccess());
        CHECK(sanitizedLoadResult.GetValue().ScriptCompileFailurePolicy ==
              Limitless::Project::ScriptCompileFailurePolicy::SafeMode);

        std::filesystem::remove_all(projectRoot, errorCode);
    }

    TEST_CASE("AudioMixerDefinition normalizes and saves ReverbSend field")
    {
        Limitless::Audio::AudioMixerDefinition definition{};
        definition.Groups.push_back({ "SFX", 0.75f, 1.6f });
        definition.Groups.push_back({ "Music", 0.50f, -0.2f });
        Limitless::Audio::NormalizeAudioMixerDefinition(definition);

        const auto sfxGroupIt = std::find_if(
            definition.Groups.begin(),
            definition.Groups.end(),
            [](const Limitless::Audio::AudioMixerGroupEntry& group) {
                return group.Name == "SFX";
            });
        REQUIRE(sfxGroupIt != definition.Groups.end());
        CHECK(sfxGroupIt->ReverbSend == doctest::Approx(1.0f));

        const auto musicGroupIt = std::find_if(
            definition.Groups.begin(),
            definition.Groups.end(),
            [](const Limitless::Audio::AudioMixerGroupEntry& group) {
                return group.Name == "Music";
            });
        REQUIRE(musicGroupIt != definition.Groups.end());
        CHECK(musicGroupIt->ReverbSend == doctest::Approx(0.0f));

        const std::filesystem::path mixerPath =
            std::filesystem::temp_directory_path() / "LimitlessRemasterTests" / "AudioMixerWithReverb.json";
        REQUIRE(Limitless::Audio::SaveAudioMixerDefinitionToPath(mixerPath, definition));

        std::ifstream input(mixerPath, std::ios::in | std::ios::binary);
        REQUIRE(input.is_open());
        nlohmann::json rootJson{};
        input >> rootJson;
        REQUIRE(rootJson.contains("Groups"));
        REQUIRE(rootJson["Groups"].is_array());

        bool foundSerializedReverbSend = false;
        for (const auto& groupJson : rootJson["Groups"])
        {
            if (groupJson.value("Name", std::string{}) == "SFX")
            {
                foundSerializedReverbSend = groupJson.contains("ReverbSend");
                if (foundSerializedReverbSend)
                    CHECK(groupJson.value("ReverbSend", 0.0f) == doctest::Approx(1.0f));
            }
        }
        CHECK(foundSerializedReverbSend);

        std::error_code errorCode;
        std::filesystem::remove(mixerPath, errorCode);
    }

    TEST_CASE("NativeScriptRegistry execution blocked flag is mutable")
    {
        Limitless::NativeScriptRegistry::SetExecutionBlocked(true);
        CHECK(Limitless::NativeScriptRegistry::IsExecutionBlocked());

        Limitless::NativeScriptRegistry::SetExecutionBlocked(false);
        CHECK_FALSE(Limitless::NativeScriptRegistry::IsExecutionBlocked());
    }
}
