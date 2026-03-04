#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "Audio/AudioMixerAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetPaths.h"
#include "Core/Debug/Log.h"
#include "EditorPrefabSystem.h"
#include "Project/ProjectManager.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Limitless
{
    namespace
    {
        constexpr const char* kDefaultSceneFileName = "SampleScene.scene.json";
        constexpr std::string_view kSceneFileSuffix = ".scene.json";

        std::string NormalizeSlashes(std::string pathText)
        {
            std::replace(pathText.begin(), pathText.end(), '\\', '/');
            return pathText;
        }

        bool IsPrefabAssetKey(const std::string& assetKey)
        {
            if (assetKey.empty())
                return false;
            std::string lowerKey = NormalizeSlashes(assetKey);
            std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lowerKey.ends_with(".prefab.json");
        }

        std::string NormalizeSceneFileName(const char* rawName)
        {
            std::string fileName = rawName ? rawName : "";
            while (!fileName.empty() && std::isspace(static_cast<unsigned char>(fileName.front())))
                fileName.erase(fileName.begin());
            while (!fileName.empty() && std::isspace(static_cast<unsigned char>(fileName.back())))
                fileName.pop_back();

            if (fileName.empty())
                fileName = "New Scene";

            for (char& character : fileName)
            {
                if (character == '/' || character == '\\' || character == ':' || character == '*' ||
                    character == '?' || character == '"' || character == '<' || character == '>' || character == '|')
                    character = '_';
            }

            std::string lowerFileName = fileName;
            std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });

            std::string lowerSuffix = std::string(kSceneFileSuffix);
            if (lowerFileName.size() < lowerSuffix.size() ||
                lowerFileName.rfind(lowerSuffix) != (lowerFileName.size() - lowerSuffix.size()))
            {
                fileName += std::string(kSceneFileSuffix);
            }
            return fileName;
        }

        void PopulateDefaultSceneTemplate(Scene& scene)
        {
            const entt::entity cameraEntity = scene.CreateEntity("Main Camera");
            auto& cameraTransform = scene.GetRegistry().get<TransformComponent>(cameraEntity);
            cameraTransform.Position = glm::vec3(0.0f, 0.0f, 5.0f);
            cameraTransform.Rotation = glm::vec3(0.0f);

            auto& camera = scene.GetRegistry().emplace<CameraComponent>(cameraEntity);
            camera.IsPrimary = true;
            camera.Projection = CameraComponent::ProjectionType::Perspective3D;
            camera.FieldOfViewYDegrees = 60.0f;
            camera.NearPlane = 0.1f;
            camera.FarPlane = 1000.0f;
        }
    }

    bool EditorLayer::OpenPrefabAssetForEditing(const std::string& prefabAssetKey)
    {
        if (prefabAssetKey.empty())
            return false;

        if (!EnsureSceneSwitchAllowed([this, prefabAssetKey]() {
                if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
                    m_PrefabModeReturnSceneAssetKey = m_CurrentSceneAssetKey;
                (void)LoadSceneFromAssetKey(prefabAssetKey, true);
            }))
        {
            return false;
        }

        if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
            m_PrefabModeReturnSceneAssetKey = m_CurrentSceneAssetKey;

        return LoadSceneFromAssetKey(prefabAssetKey, true);
    }

    bool EditorLayer::ReturnFromPrefabMode(bool forceWithoutConfirmation)
    {
        if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
            return false;
        if (m_PrefabModeReturnSceneAssetKey.empty())
            return false;

        const std::string returnSceneKey = m_PrefabModeReturnSceneAssetKey;

        if (!forceWithoutConfirmation)
        {
            if (!EnsureSceneSwitchAllowed([this, returnSceneKey]() {
                    m_PrefabModeReturnSceneAssetKey.clear();
                    (void)LoadSceneFromAssetKey(returnSceneKey, true);
                }))
            {
                return false;
            }
        }

        m_PrefabModeReturnSceneAssetKey.clear();
        return LoadSceneFromAssetKey(returnSceneKey, true);
    }

    bool EditorLayer::ApplyPrefabStageChangesToInstances()
    {
        if (!IsPrefabAssetKey(m_CurrentSceneAssetKey))
            return false;
        if (m_PrefabModeReturnSceneAssetKey.empty())
            return false;
        if (!m_Scene)
            return false;

        const std::string prefabAssetKey = m_CurrentSceneAssetKey;
        const std::string returnSceneKey = m_PrefabModeReturnSceneAssetKey;

        // Save the prefab asset before pushing changes to instances.
        if (!SaveSceneToAssetKey(prefabAssetKey))
            return false;

        // Switch back to the scene we came from, then apply to all instances there.
        m_PrefabModeReturnSceneAssetKey.clear();
        if (!LoadSceneFromAssetKey(returnSceneKey, true))
            return false;

        return m_EditorUndoService.ExecuteSceneMutation("Apply Prefab To Instances", [&](Scene& mutableScene) {
            return EditorPrefabSystem::ApplyPrefabAssetToInstancesInScene(mutableScene, prefabAssetKey);
        });
    }

    std::string EditorLayer::CreateSceneAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create scene asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create scene folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string normalizedFileName = preferredFileName.empty()
            ? std::string(kDefaultSceneFileName)
            : NormalizeSceneFileName(preferredFileName.c_str());
        std::filesystem::path scenePath = targetDirectory / normalizedFileName;
        if (preferredFileName.empty() && std::filesystem::exists(scenePath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("SampleScene " + std::to_string(index) + ".scene.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    scenePath = candidate;
                    break;
                }
            }
        }

        if (!std::filesystem::exists(scenePath, errorCode))
        {
            Scene scene;
            PopulateDefaultSceneTemplate(scene);
            const auto saveResult = scene.SaveToFile(scenePath);
            if (saveResult.IsFailure())
            {
                LT_ERROR("Could not create scene asset {}: {}", scenePath.string(), saveResult.GetError().GetErrorMessage());
                return {};
            }
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(scenePath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute scene asset key for {}", scenePath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Scene);
        if (importResult.IsFailure())
        {
            LT_WARN("Created scene asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());
        }

        LT_INFO("Created scene asset {}", assetKey);
        return assetKey;
    }

    std::string EditorLayer::CreateMaterialAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create material asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create material folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Material") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".material.json"))
            finalFileName += ".material.json";

        std::filesystem::path materialPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(materialPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Material " + std::to_string(index) + ".material.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    materialPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json materialJson = {
            { "shader", { { "key", "Assets/Shaders/Renderer2D_TexturedQuad.glsl" } } }
        };

        {
            std::ofstream output(materialPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create material asset {}", materialPath.string());
                return {};
            }
            output << materialJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(materialPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute material asset key for {}", materialPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Material);
        if (importResult.IsFailure())
            LT_WARN("Created material asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created material asset {}", assetKey);
        return assetKey;
    }

    std::string EditorLayer::CreateTilesetAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create tileset asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create tileset folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Tileset") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".tileset.json"))
            finalFileName += ".tileset.json";

        std::filesystem::path tilesetPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(tilesetPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Tileset " + std::to_string(index) + ".tileset.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    tilesetPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json tilesetJson = {
            { "TextureKey", "" },
            { "TileSizePixels", { 16, 16 } }
        };

        {
            std::ofstream output(tilesetPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create tileset asset {}", tilesetPath.string());
                return {};
            }
            output << tilesetJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(tilesetPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute tileset asset key for {}", tilesetPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::Tileset);
        if (importResult.IsFailure())
            LT_WARN("Created tileset asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created tileset asset {}", assetKey);
        m_SelectedTilesetAssetKey = assetKey;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateAudioMixerAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create audio mixer asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create audio mixer folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Audio Mixer") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".audiomixer.json"))
            finalFileName += ".audiomixer.json";

        std::filesystem::path mixerPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(mixerPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Audio Mixer " + std::to_string(index) + ".audiomixer.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    mixerPath = candidate;
                    break;
                }
            }
        }

        Audio::AudioMixerDefinition definition{};
        Audio::NormalizeAudioMixerDefinition(definition);
        if (!Audio::SaveAudioMixerDefinitionToPath(mixerPath, definition))
        {
            LT_ERROR("Could not create audio mixer asset {}", mixerPath.string());
            return {};
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(mixerPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute audio mixer asset key for {}", mixerPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AudioMixer);
        if (importResult.IsFailure())
            LT_WARN("Created audio mixer asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created audio mixer asset {}", assetKey);
        m_SelectedAudioMixerAssetKey = assetKey;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateInputActionsAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create input actions asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create input actions folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Input Actions") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".inputactions.json"))
            finalFileName += ".inputactions.json";

        std::filesystem::path inputActionsPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(inputActionsPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Input Actions " + std::to_string(index) + ".inputactions.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    inputActionsPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json inputActionsJson = {
            { "maps", nlohmann::json::array({
                {
                    { "name", "Gameplay" },
                    { "enabled", true },
                    { "actions", nlohmann::json::array() }
                }
            }) }
        };

        {
            std::ofstream output(inputActionsPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create input actions asset {}", inputActionsPath.string());
                return {};
            }
            output << inputActionsJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(inputActionsPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute input actions asset key for {}", inputActionsPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::InputActions);
        if (importResult.IsFailure())
            LT_WARN("Created input actions asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created input actions asset {}", assetKey);
        m_SelectedInputActionsAssetKey = assetKey;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateAnimationClipAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create animation clip asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create animation clip folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Animation Clip") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".animationclip.json"))
            finalFileName += ".animationclip.json";

        std::filesystem::path clipPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(clipPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Animation Clip " + std::to_string(index) + ".animationclip.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    clipPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json clipJson = {
            {"Version", 1},
            {"Name", clipPath.stem().string()},
            {"Loop", true},
            {"DurationSeconds", 1.0f},
            {"SamplesPerSecond", 30.0f},
            {"SpriteSubRectTrack", nlohmann::json::array()},
            {"SpriteTextureTrack", nlohmann::json::array()},
            {"PositionTrack", nlohmann::json::array()},
            {"ScaleTrack", nlohmann::json::array()},
            {"RotationZTrack", nlohmann::json::array()},
            {"EventTrack", nlohmann::json::array()}
        };

        {
            std::ofstream output(clipPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create animation clip asset {}", clipPath.string());
                return {};
            }
            output << clipJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(clipPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute animation clip asset key for {}", clipPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AnimationClip);
        if (importResult.IsFailure())
            LT_WARN("Created animation clip asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created animation clip asset {}", assetKey);
        m_SelectedAnimationClipAssetKey = assetKey;
        m_SelectedAnimatorControllerAssetKey.clear();
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreateAnimatorControllerAssetInFolder(const std::filesystem::path& relativeFolderPath, const std::string& preferredFileName)
    {
        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
        {
            LT_ERROR("Could not create animator controller asset: {}", rootResult.GetError().GetErrorMessage());
            return {};
        }

        const std::filesystem::path assetsDirectory = rootResult.GetValue() / "Assets";
        const std::filesystem::path targetDirectory = assetsDirectory / relativeFolderPath;
        std::error_code errorCode;
        std::filesystem::create_directories(targetDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not create animator controller folder {}: {}", targetDirectory.string(), errorCode.message());
            return {};
        }

        const std::string baseName = preferredFileName.empty() ? std::string("New Animator Controller") : preferredFileName;
        std::string finalFileName = baseName;
        if (!finalFileName.ends_with(".animcontroller.json"))
            finalFileName += ".animcontroller.json";

        std::filesystem::path controllerPath = targetDirectory / finalFileName;
        if (std::filesystem::exists(controllerPath, errorCode))
        {
            for (int32_t index = 1; index < 1024; ++index)
            {
                const std::filesystem::path candidate = targetDirectory / ("New Animator Controller " + std::to_string(index) + ".animcontroller.json");
                if (!std::filesystem::exists(candidate, errorCode))
                {
                    controllerPath = candidate;
                    break;
                }
            }
        }

        nlohmann::json controllerJson = {
            {"Name", controllerPath.stem().string()},
            {"DefaultStateName", ""},
            {"Parameters", nlohmann::json::array()},
            {"States", nlohmann::json::array()}
        };

        {
            std::ofstream output(controllerPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                LT_ERROR("Could not create animator controller asset {}", controllerPath.string());
                return {};
            }
            output << controllerJson.dump(2);
        }

        std::filesystem::path relativeAssetPath = std::filesystem::relative(controllerPath, assetsDirectory, errorCode);
        if (errorCode)
        {
            LT_ERROR("Could not compute animator controller asset key for {}", controllerPath.string());
            return {};
        }

        const std::string assetKey = "Assets/" + relativeAssetPath.generic_string();
        const auto importResult = Assets::AssetDatabase::GetInstance().ImportOrUpdate(assetKey, Assets::AssetType::AnimatorController);
        if (importResult.IsFailure())
            LT_WARN("Created animator controller asset but failed to import into AssetDatabase ({}): {}", assetKey, importResult.GetError().GetErrorMessage());

        LT_INFO("Created animator controller asset {}", assetKey);
        m_SelectedAnimatorControllerAssetKey = assetKey;
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedEntity = entt::null;
        return assetKey;
    }

    std::string EditorLayer::CreatePrefabAssetPathForEntity(entt::entity entity, const std::filesystem::path& relativeFolderPath) const
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return {};

        std::string baseName = "New Prefab";
        if (const auto* tag = m_Scene->GetRegistry().try_get<TagComponent>(entity))
        {
            if (!tag->Tag.empty())
                baseName = tag->Tag;
        }
        for (char& character : baseName)
        {
            if (character == '/' || character == '\\' || character == ':' || character == '*'
                || character == '?' || character == '"' || character == '<' || character == '>'
                || character == '|')
            {
                character = '_';
            }
        }
        if (baseName.empty())
            baseName = "New Prefab";

        const auto rootResult = Assets::FindProjectRootFromWorkingDirectory();
        if (rootResult.IsFailure())
            return {};

        const std::filesystem::path targetFolder = relativeFolderPath.empty()
            ? std::filesystem::path("Prefabs")
            : relativeFolderPath;
        const std::filesystem::path prefabsFolder = rootResult.GetValue() / "Assets" / targetFolder;
        std::error_code errorCode;
        std::filesystem::create_directories(prefabsFolder, errorCode);
        if (errorCode)
            return {};

        std::filesystem::path candidatePath = prefabsFolder / (baseName + ".prefab.json");
        uint32_t suffix = 1;
        while (std::filesystem::exists(candidatePath, errorCode))
        {
            candidatePath = prefabsFolder / (baseName + " " + std::to_string(suffix) + ".prefab.json");
            ++suffix;
            errorCode.clear();
        }

        std::filesystem::path relativePath = std::filesystem::relative(candidatePath, rootResult.GetValue(), errorCode);
        if (errorCode || relativePath.empty())
            return {};
        return relativePath.generic_string();
    }

    bool EditorLayer::CreatePrefabFromEntity(entt::entity entity)
    {
        return CreatePrefabFromEntityInFolder(entity, std::filesystem::path("Prefabs"));
    }

    bool EditorLayer::CreatePrefabFromEntityInFolder(entt::entity entity, const std::filesystem::path& relativeFolderPath)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        const std::string prefabAssetKey = CreatePrefabAssetPathForEntity(entity, relativeFolderPath);
        if (prefabAssetKey.empty())
            return false;

        const bool success = m_EditorUndoService.ExecuteSceneMutation("Create Prefab", [&](Scene& mutableScene) {
            return EditorPrefabSystem::CreateOrUpdatePrefabFromEntity(mutableScene, entity, prefabAssetKey);
        });

        if (success)
            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(prefabAssetKey, Assets::AssetType::Prefab);
        return success;
    }

    entt::entity EditorLayer::InstantiatePrefabAtParent(const std::string& prefabAssetKey, entt::entity parentEntity)
    {
        if (!m_Scene || prefabAssetKey.empty())
            return entt::null;

        entt::entity createdEntity = entt::null;
        const bool success = m_EditorUndoService.ExecuteSceneMutation("Instantiate Prefab", [&](Scene& mutableScene) {
            createdEntity = EditorPrefabSystem::InstantiatePrefab(mutableScene, prefabAssetKey, parentEntity);
            return createdEntity != entt::null;
        });
        if (!success)
            return entt::null;

        if (createdEntity != entt::null)
        {
            m_SelectedEntity = createdEntity;
            m_SelectedTextureAssetKey.clear();
            m_CachedTextureAsset.reset();
            m_SelectedMaterialAssetKey.clear();
            m_CachedMaterialAsset.reset();
            m_SelectedNativeScriptAssetKey.clear();
            m_SelectedPrefabAssetKey.clear();
            m_SelectedTilesetAssetKey.clear();
            m_SelectedAudioMixerAssetKey.clear();
            m_SelectedInputActionsAssetKey.clear();
            m_SelectedAnimationClipAssetKey.clear();
            m_SelectedAnimatorControllerAssetKey.clear();
        }
        return createdEntity;
    }

    entt::entity EditorLayer::InstantiatePrefabAtWorldPosition(const std::string& prefabAssetKey, const glm::vec3& worldPosition)
    {
        if (!m_Scene || prefabAssetKey.empty())
            return entt::null;

        entt::entity createdEntity = entt::null;
        const bool success = m_EditorUndoService.ExecuteSceneMutation("Instantiate Prefab", [&](Scene& mutableScene) {
            createdEntity = EditorPrefabSystem::InstantiatePrefab(mutableScene, prefabAssetKey, entt::null);
            if (createdEntity == entt::null || !mutableScene.IsValid(createdEntity))
                return false;

            if (auto* transform = mutableScene.GetRegistry().try_get<TransformComponent>(createdEntity))
            {
                transform->Position = worldPosition;
                mutableScene.MarkTransformDirty(createdEntity);
            }
            return true;
        });
        if (!success)
            return entt::null;

        m_SelectedEntity = createdEntity;
        m_SelectedTextureAssetKey.clear();
        m_CachedTextureAsset.reset();
        m_SelectedMaterialAssetKey.clear();
        m_CachedMaterialAsset.reset();
        m_SelectedNativeScriptAssetKey.clear();
        m_SelectedPrefabAssetKey.clear();
        m_SelectedTilesetAssetKey.clear();
        m_SelectedAudioMixerAssetKey.clear();
        m_SelectedInputActionsAssetKey.clear();
        m_SelectedAnimationClipAssetKey.clear();
        m_SelectedAnimatorControllerAssetKey.clear();
        return createdEntity;
    }

    bool EditorLayer::ApplyPrefabFromEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        std::string prefabAssetKey;
        if (const auto* prefabInstance = m_Scene->GetRegistry().try_get<PrefabInstanceComponent>(entity))
            prefabAssetKey = prefabInstance->PrefabAssetKey;

        const bool success = EditorPrefabSystem::ApplyPrefabFromInstance(*m_Scene, entity);
        if (success && !prefabAssetKey.empty())
            (void)Assets::AssetDatabase::GetInstance().ImportOrUpdate(prefabAssetKey, Assets::AssetType::Prefab);
        return success;
    }

    entt::entity EditorLayer::RevertPrefabEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return entt::null;

        entt::entity revertedEntity = entt::null;
        const bool success = m_EditorUndoService.ExecuteSceneMutation("Revert Prefab", [&](Scene& mutableScene) {
            revertedEntity = EditorPrefabSystem::RevertPrefabInstance(mutableScene, entity);
            return revertedEntity != entt::null;
        });
        if (!success)
            return entt::null;

        if (revertedEntity != entt::null)
            m_SelectedEntity = revertedEntity;
        return revertedEntity;
    }

    bool EditorLayer::UnpackPrefabEntity(entt::entity entity)
    {
        if (!m_Scene || !m_Scene->IsValid(entity))
            return false;

        return m_EditorUndoService.ExecuteSceneMutation("Unpack Prefab", [&](Scene& mutableScene) {
            return EditorPrefabSystem::UnpackPrefabInstance(mutableScene, entity);
        });
    }

    void EditorLayer::SetProjectDefaultSceneAssetKey(const std::string& sceneAssetKey)
    {
        if (sceneAssetKey.empty())
        {
            return;
        }

        auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
        {
            LT_WARN("Cannot set default scene: no project is currently open.");
            return;
        }

        const auto saveResult = projectManager.SetDefaultSceneAssetKey(sceneAssetKey);
        if (saveResult.IsFailure())
        {
            LT_ERROR("Failed to set default scene '{}': {}", sceneAssetKey, saveResult.GetError().GetErrorMessage());
            return;
        }

        LT_INFO("Default scene updated to '{}'", sceneAssetKey);
    }
}  // namespace Limitless
