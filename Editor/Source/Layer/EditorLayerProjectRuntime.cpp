#include "PrecompiledHeader.h"
#include "EditorLayer.h"

#include "Audio/AudioEngine.h"
#include "Audio/AudioMixerAsset.h"
#include "Assets/AssetDatabase.h"
#include "Assets/AssetImportPipeline.h"
#include "Assets/MaterialAsset.h"
#include "Assets/TextureAsset.h"
#include "Assets/TileAsset.h"
#include "Core/Application.h"
#include "Core/Debug/Log.h"
#include "Graphics/Lighting2DRenderer.h"
#include "Graphics/Renderer2D.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSettings.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <sstream>
#include <vector>

namespace Limitless
{
    namespace
    {
        int64_t GetLastWriteTimeTicksOrZero(const std::filesystem::path& path)
        {
            std::error_code errorCode;
            const auto lastWriteTime = std::filesystem::last_write_time(path, errorCode);
            if (errorCode)
                return 0;
            return static_cast<int64_t>(lastWriteTime.time_since_epoch().count());
        }
    }

    void EditorLayer::RefreshProjectAudioSettings()
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return;

        const auto audioSettingsResult = Project::LoadAudioSettings(projectManager.GetProjectRoot());
        if (audioSettingsResult.IsSuccess())
        {
            m_ProjectAudioSettings = audioSettingsResult.GetValue();
            m_ProjectAudioSettingsLoaded = true;
            m_ProjectSettingsPanelState.Audio = m_ProjectAudioSettings;
            m_ProjectAppliedAudioMixerAssetKey.clear();
            m_ProjectAppliedAudioMixerLastWriteTimeTicks = 0;
            ApplyProjectAudioSettings();
        }
        else
        {
            LT_WARN("Failed to load project audio settings: {}", audioSettingsResult.GetError().GetErrorMessage());
            m_ProjectAudioSettingsLoaded = false;
        }
    }

    void EditorLayer::RefreshProjectRenderSettings()
    {
        const auto& projectManager = Project::ProjectManager::GetInstance();
        if (!projectManager.HasOpenProject())
            return;

        const auto renderSettingsResult = Project::LoadRenderSettings(projectManager.GetProjectRoot());
        if (renderSettingsResult.IsSuccess())
        {
            m_ProjectRenderSettings = renderSettingsResult.GetValue();
            m_ProjectRenderSettingsLoaded = true;
            m_ProjectSettingsPanelState.Render = m_ProjectRenderSettings;
            ApplyProjectRenderSettings();
        }
        else
        {
            LT_WARN("Failed to load project render settings: {}", renderSettingsResult.GetError().GetErrorMessage());
            m_ProjectRenderSettingsLoaded = false;
        }
    }

    void EditorLayer::ApplyProjectRenderSettings()
    {
        if (!m_ProjectRenderSettingsLoaded)
            return;

        const glm::vec4 clearColor(
            std::clamp(m_ProjectRenderSettings.ClearColor[0], 0.0f, 1.0f),
            std::clamp(m_ProjectRenderSettings.ClearColor[1], 0.0f, 1.0f),
            std::clamp(m_ProjectRenderSettings.ClearColor[2], 0.0f, 1.0f),
            std::clamp(m_ProjectRenderSettings.ClearColor[3], 0.0f, 1.0f));

        if (!m_ProjectRenderVSyncApplied || m_ProjectRenderAppliedVSyncValue != m_ProjectRenderSettings.VSync)
        {
            auto& window = Application::GetInstance().GetWindow();
            window.SetVSync(m_ProjectRenderSettings.VSync);
            m_ProjectRenderAppliedVSyncValue = m_ProjectRenderSettings.VSync;
            m_ProjectRenderVSyncApplied = true;
        }

        const auto clearColorChanged = [](const glm::vec4& a, const glm::vec4& b) {
            constexpr float epsilon = 0.0001f;
            return std::abs(a.r - b.r) > epsilon ||
                   std::abs(a.g - b.g) > epsilon ||
                   std::abs(a.b - b.b) > epsilon ||
                   std::abs(a.a - b.a) > epsilon;
        };

        if (clearColorChanged(clearColor, m_ProjectRenderAppliedClearColor))
        {
            SceneRenderer::SetViewportClearColor(clearColor);
            m_ProjectRenderAppliedClearColor = clearColor;
        }
    }

    void EditorLayer::ApplyProjectAudioSettings()
    {
        if (!m_ProjectAudioSettingsLoaded)
            return;

        Audio::AudioEngine& audioEngine = Audio::AudioEngine::GetInstance();
        const float masterVolume = m_ProjectAudioSettings.Muted
            ? 0.0f
            : std::max(0.0f, m_ProjectAudioSettings.MasterVolume);
        audioEngine.SetMasterVolume(masterVolume);

        if (m_ProjectAudioSettings.MixerAssetKey.empty())
            return;

        std::filesystem::path mixerResolvedPath;
        Audio::AudioMixerDefinition mixerDefinition{};
        if (!Audio::LoadAudioMixerDefinitionFromAssetKey(
                m_ProjectAudioSettings.MixerAssetKey,
                mixerDefinition,
                &mixerResolvedPath))
        {
            return;
        }

        const int64_t lastWriteTicks = GetLastWriteTimeTicksOrZero(mixerResolvedPath);
        const bool shouldApplyMixer =
            m_ProjectAppliedAudioMixerAssetKey != m_ProjectAudioSettings.MixerAssetKey ||
            m_ProjectAppliedAudioMixerLastWriteTimeTicks != lastWriteTicks;
        if (!shouldApplyMixer)
            return;

        for (const auto& group : mixerDefinition.Groups)
        {
            audioEngine.SetMixerGroupVolume(group.Name, group.Volume);
            audioEngine.SetMixerGroupReverbSend(group.Name, group.ReverbSend);
        }

        m_ProjectAppliedAudioMixerAssetKey = m_ProjectAudioSettings.MixerAssetKey;
        m_ProjectAppliedAudioMixerLastWriteTimeTicks = lastWriteTicks;
    }

    void EditorLayer::RefreshProjectPhysics2DSettings()
    {
        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
            return;

        const auto physicsSettingsResult = Project::LoadPhysics2DSettings(pm.GetProjectRoot());
        if (physicsSettingsResult.IsSuccess())
        {
            m_ProjectPhysics2DSettings = physicsSettingsResult.GetValue();
            m_ProjectPhysics2DSettingsLoaded = true;
            m_ProjectSettingsPanelState.Physics2D = m_ProjectPhysics2DSettings;
        }
        else
        {
            LT_WARN("Failed to load project physics settings: {}", physicsSettingsResult.GetError().GetErrorMessage());
            m_ProjectPhysics2DSettingsLoaded = false;
        }
    }

    void EditorLayer::RefreshProjectLighting2DSettings()
    {
        const auto& pm = Project::ProjectManager::GetInstance();
        if (!pm.HasOpenProject())
            return;

        const auto lightingSettingsResult = Project::LoadLighting2DSettings(pm.GetProjectRoot());
        if (lightingSettingsResult.IsSuccess())
        {
            m_ProjectLighting2DSettings = lightingSettingsResult.GetValue();
            m_ProjectLighting2DSettingsLoaded = true;
            m_ProjectSettingsPanelState.Lighting2D = m_ProjectLighting2DSettings;
            ApplyProjectLighting2DSettings();
        }
        else
        {
            LT_WARN("Failed to load project lighting settings: {}", lightingSettingsResult.GetError().GetErrorMessage());
            m_ProjectLighting2DSettingsLoaded = false;
        }
    }

    void EditorLayer::ApplyProjectPhysics2DSettingsToScenes()
    {
        if (!m_ProjectPhysics2DSettingsLoaded)
            return;

        auto applyProjectSettings = [this](Scene* targetScene)
        {
            if (!targetScene)
                return;

            Physics2DWorldSettings runtimeSettings = targetScene->GetPhysics2DSettings();
            runtimeSettings.Gravity = glm::vec2(m_ProjectPhysics2DSettings.GravityX, m_ProjectPhysics2DSettings.GravityY);
            runtimeSettings.VelocitySubSteps = std::max(1, m_ProjectPhysics2DSettings.VelocitySubSteps);
            runtimeSettings.EnableSleep = m_ProjectPhysics2DSettings.EnableSleep;
            runtimeSettings.EnableContinuousCollision = m_ProjectPhysics2DSettings.EnableContinuousCollision;
            runtimeSettings.HighContactQualityMode = m_ProjectPhysics2DSettings.HighContactQualityMode;
            runtimeSettings.HighContactQualityExtraSubSteps = std::max(0, m_ProjectPhysics2DSettings.HighContactQualityExtraSubSteps);
            runtimeSettings.ContactHertz = m_ProjectPhysics2DSettings.ContactHertz;
            runtimeSettings.ContactDampingRatio = m_ProjectPhysics2DSettings.ContactDampingRatio;
            runtimeSettings.ContactPushSpeed = m_ProjectPhysics2DSettings.ContactPushSpeed;
            targetScene->SetPhysics2DSettings(runtimeSettings);
        };

        applyProjectSettings(m_Scene.get());
        applyProjectSettings(m_EditSceneStored.get());
    }

    void EditorLayer::ApplyProjectLighting2DSettings()
    {
        if (!m_ProjectLighting2DSettingsLoaded)
            return;

        Lighting2DSettings runtimeSettings{};
        runtimeSettings.Enabled = m_ProjectLighting2DSettings.Enabled;
        runtimeSettings.EnableNormalMaps = m_ProjectLighting2DSettings.EnableNormalMaps;
        runtimeSettings.EnableShadows = m_ProjectLighting2DSettings.EnableShadows;
        runtimeSettings.AmbientColor = glm::vec3(
            m_ProjectLighting2DSettings.AmbientColor[0],
            m_ProjectLighting2DSettings.AmbientColor[1],
            m_ProjectLighting2DSettings.AmbientColor[2]);
        runtimeSettings.AmbientIntensity = m_ProjectLighting2DSettings.AmbientIntensity;
        runtimeSettings.ShadowQualityLevel = std::clamp(m_ProjectLighting2DSettings.ShadowQualityLevel, 0, 2);
        runtimeSettings.MaxDirectionalLights = std::max(0, m_ProjectLighting2DSettings.MaxDirectionalLights);
        runtimeSettings.MaxPointLights = std::max(0, m_ProjectLighting2DSettings.MaxPointLights);
        runtimeSettings.MaxShadowSegments = std::max(1, m_ProjectLighting2DSettings.MaxShadowSegments);
        runtimeSettings.ShadowSoftnessScale = std::max(0.0f, m_ProjectLighting2DSettings.ShadowSoftnessScale);
        runtimeSettings.DirectionalShadowBiasScale = std::max(0.0f, m_ProjectLighting2DSettings.DirectionalShadowBiasScale);
        runtimeSettings.ShadowAlphaCutoff = std::clamp(m_ProjectLighting2DSettings.ShadowAlphaCutoff, 0.0f, 1.0f);
        runtimeSettings.ShadowSegmentSnapPixels = std::max(0.0f, m_ProjectLighting2DSettings.ShadowSegmentSnapPixels);
        runtimeSettings.EnableHighAngularVelocityShadowFreeze = m_ProjectLighting2DSettings.EnableHighAngularVelocityShadowFreeze;
        runtimeSettings.ShadowFreezeAngularVelocityDegreesPerSecond = std::max(1.0f, m_ProjectLighting2DSettings.ShadowFreezeAngularVelocityDegreesPerSecond);
        runtimeSettings.ShadowFreezeFrameCount = std::max(1, m_ProjectLighting2DSettings.ShadowFreezeFrameCount);
        runtimeSettings.MaxShadowSamplesPerLight = std::max(1, m_ProjectLighting2DSettings.MaxShadowSamplesPerLight);
        Lighting2DRenderer::SetSettings(runtimeSettings);
    }

    void EditorLayer::LaunchStartupAssetImport()
    {
        if (m_StartupAssetImportInProgress)
            return;

        const auto existingRecords = Assets::AssetDatabase::GetInstance().GetAllRecords();
        size_t tileRecordCount = 0;
        for (const auto& record : existingRecords)
        {
            if (record.Type == Assets::AssetType::Tile)
                ++tileRecordCount;
        }

        // Use a lighter changed-only pass for established projects to avoid an
        // expensive dependent cascade while the editor is becoming interactive.
        const bool includeDependents = existingRecords.empty();
        m_StartupAssetImportInProgress = true;
        m_StartupAssetImportTask = Async::GetAsyncIO().RunAsync([includeDependents]() -> std::string {
            const auto result = Assets::AssetImportPipeline::ReimportChanged(includeDependents);
            if (result.IsFailure())
                return std::string("error: ") + result.GetError().GetErrorMessage();

            const auto& stats = result.GetValue();
            std::ostringstream stream;
            stream << "discovered=" << stats.DiscoveredFiles
                   << " imported=" << stats.Imported
                   << " skipped=" << stats.SkippedUpToDate
                   << " missing=" << stats.MissingOnDisk
                   << " errors=" << stats.Errors;
            return stream.str();
        });

        LT_INFO(
            "Scheduled background startup reimport (records={}, tileRecords={}, includeDependents={}).",
            existingRecords.size(),
            tileRecordCount,
            includeDependents ? "true" : "false");
    }

    void EditorLayer::PumpStartupAssetImport()
    {
        if (!m_StartupAssetImportInProgress || !m_StartupAssetImportTask.IsValid() || !m_StartupAssetImportTask.IsDone())
            return;

        try
        {
            const std::string result = m_StartupAssetImportTask.Get();
            if (result.rfind("error:", 0) == 0)
                LT_WARN("Background startup reimport failed: {}", result);
            else
                LT_INFO("Background startup reimport completed: {}", result);
        }
        catch (const std::exception& exception)
        {
            LT_WARN("Background startup reimport failed with exception: {}", exception.what());
        }
        catch (...)
        {
            LT_WARN("Background startup reimport failed with unknown exception.");
        }

        m_StartupAssetImportTask = Async::Task<std::string>();
        m_StartupAssetImportInProgress = false;
    }

    void EditorLayer::QueueSceneAssetPrewarm()
    {
        m_ActiveSceneTexturePrewarmKeys.clear();
        m_ActiveSceneMaterialPrewarmKeys.clear();

        if (!m_Scene)
            return;

        auto& registry = m_Scene->GetRegistry();

        auto spriteView = registry.view<SpriteComponent>();
        for (entt::entity entity : spriteView)
        {
            const auto& sprite = spriteView.get<SpriteComponent>(entity);
            if (sprite.TextureKey.empty())
                continue;

            m_ActiveSceneTexturePrewarmKeys.insert(sprite.TextureKey);
            if (m_PrewarmedTextureAssets.contains(sprite.TextureKey) || m_PendingTexturePrewarmTasks.contains(sprite.TextureKey))
                continue;

            m_PendingTexturePrewarmTasks.emplace(sprite.TextureKey, Assets::TextureAsset::LoadAsync(sprite.TextureKey));
        }

        auto materialView = registry.view<MaterialComponent>();
        for (entt::entity entity : materialView)
        {
            const auto& material = materialView.get<MaterialComponent>(entity);
            if (material.MaterialKey.empty())
                continue;

            m_ActiveSceneMaterialPrewarmKeys.insert(material.MaterialKey);
            if (m_PrewarmedMaterialAssets.contains(material.MaterialKey) || m_PendingMaterialPrewarmTasks.contains(material.MaterialKey))
                continue;

            m_PendingMaterialPrewarmTasks.emplace(material.MaterialKey, Assets::MaterialAsset::LoadAsync(material.MaterialKey));
        }

        // Prewarm tile textures so the render cache doesn't stall on first frame.
        auto tilemapView = registry.view<TilemapLayerComponent>();
        for (entt::entity entity : tilemapView)
        {
            const auto& layer = tilemapView.get<TilemapLayerComponent>(entity);
            std::vector<bool> usedTileTableEntries(layer.TileTable.size(), false);
            for (uint32_t tileId : layer.Tiles)
            {
                if (tileId == 0u)
                    continue;
                if (static_cast<size_t>(tileId) < usedTileTableEntries.size())
                    usedTileTableEntries[tileId] = true;
            }

            for (size_t tableIndex = 0; tableIndex < layer.TileTable.size(); ++tableIndex)
            {
                if (tableIndex >= usedTileTableEntries.size() || !usedTileTableEntries[tableIndex])
                    continue;

                const std::string& tileKey = layer.TileTable[tableIndex];
                if (tileKey.empty())
                    continue;

                auto tileResult = Assets::LoadTileAssetData(tileKey);
                if (tileResult.IsFailure())
                    continue;

                const std::string& textureKey = tileResult.GetValue().SpriteTextureKey;
                if (textureKey.empty())
                    continue;

                m_ActiveSceneTexturePrewarmKeys.insert(textureKey);
                if (m_PrewarmedTextureAssets.contains(textureKey) || m_PendingTexturePrewarmTasks.contains(textureKey))
                    continue;

                m_PendingTexturePrewarmTasks.emplace(textureKey, Assets::TextureAsset::LoadAsync(textureKey));
            }
        }
    }

    bool EditorLayer::IsSceneAssetPrewarmComplete() const
    {
        for (const std::string& textureKey : m_ActiveSceneTexturePrewarmKeys)
        {
            if (m_PendingTexturePrewarmTasks.contains(textureKey))
                return false;
        }

        for (const std::string& materialKey : m_ActiveSceneMaterialPrewarmKeys)
        {
            if (m_PendingMaterialPrewarmTasks.contains(materialKey))
                return false;
        }

        return true;
    }

    void EditorLayer::UpdateSceneLoadingState()
    {
        if (!m_Scene || m_Scene->GetLoadState() != Scene::LoadState::Loading)
            return;

        // Explicitly initialize physics runtime while the scene is loading so
        // rendering only starts after colliders/bodies are fully prepared.
        (void)m_Scene->InitializePhysicsWorldForLoading();

        const bool shaderReady = Renderer2D::IsShaderReady();
        const bool assetsReady = IsSceneAssetPrewarmComplete();
        const bool objectsReady = m_Scene->IsSceneObjectsInitialized();
        const bool physicsReady = m_Scene->IsPhysicsWorldInitializedForLoading();

        if (shaderReady && assetsReady && objectsReady && physicsReady)
        {
            m_Scene->SetLoadStateReady();
            LT_INFO("Scene load completed and is now ready for rendering.");
        }
    }

    void EditorLayer::PumpSceneAssetPrewarm()
    {
        for (auto it = m_PendingTexturePrewarmTasks.begin(); it != m_PendingTexturePrewarmTasks.end();)
        {
            if (!it->second.IsDone())
            {
                ++it;
                continue;
            }

            try
            {
                if (auto loaded = it->second.Get())
                    m_PrewarmedTextureAssets[it->first] = loaded;
            }
            catch (...) {}

            it = m_PendingTexturePrewarmTasks.erase(it);
        }

        for (auto it = m_PendingMaterialPrewarmTasks.begin(); it != m_PendingMaterialPrewarmTasks.end();)
        {
            if (!it->second.IsDone())
            {
                ++it;
                continue;
            }

            try
            {
                if (auto loaded = it->second.Get())
                    m_PrewarmedMaterialAssets[it->first] = loaded;
            }
            catch (...) {}

            it = m_PendingMaterialPrewarmTasks.erase(it);
        }
    }
}  // namespace Limitless
