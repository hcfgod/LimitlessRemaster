#include "GameLayer.h"
#include "GameLayerInternal.h"

#include "Audio/SceneAudioSystem.h"
#include "Core/Debug/Log.h"
#include "Physics/Physics2DQueries.h"
#include "Scene/Components/CoreComponents.h"
#include "Scene/Components/RenderingComponents.h"
#include "Scene/Components/TilemapComponents.h"
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Limitless
{
    namespace
    {
        std::string NormalizeSlashes(std::string text)
        {
            std::replace(text.begin(), text.end(), '\\', '/');
            return text;
        }

        std::string ToLower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return text;
        }

        bool EndsWithCaseInsensitive(const std::string& text, std::string_view suffix)
        {
            if (text.size() < suffix.size())
                return false;

            const size_t offset = text.size() - suffix.size();
            for (size_t index = 0; index < suffix.size(); ++index)
            {
                const char left = static_cast<char>(std::tolower(static_cast<unsigned char>(text[offset + index])));
                const char right = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[index])));
                if (left != right)
                    return false;
            }
            return true;
        }

        bool StartsWithCaseInsensitive(const std::string& text, std::string_view prefix)
        {
            if (text.size() < prefix.size())
                return false;

            for (size_t index = 0; index < prefix.size(); ++index)
            {
                const char left = static_cast<char>(std::tolower(static_cast<unsigned char>(text[index])));
                const char right = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[index])));
                if (left != right)
                    return false;
            }
            return true;
        }

        std::string CanonicalizeSceneKeyForComparison(const std::string& sceneIdentifier)
        {
            std::string canonical = NormalizeSlashes(sceneIdentifier);
            if (canonical.empty())
                return canonical;

            const bool isPathLike = canonical.find('/') != std::string::npos;
            if (isPathLike && !StartsWithCaseInsensitive(canonical, "Assets/"))
                canonical = "Assets/" + canonical;

            if (isPathLike)
            {
                if (!EndsWithCaseInsensitive(canonical, ".scene.json"))
                {
                    if (EndsWithCaseInsensitive(canonical, ".scene"))
                        canonical += ".json";
                    else
                        canonical += ".scene.json";
                }
            }

            return canonical;
        }

        std::string ExtractSceneNameStem(const std::string& sceneIdentifier)
        {
            const std::filesystem::path scenePath(NormalizeSlashes(sceneIdentifier));
            std::string stem = scenePath.stem().string();
            if (EndsWithCaseInsensitive(stem, ".scene"))
                stem.resize(stem.size() - std::string_view(".scene").size());
            return stem;
        }
    }

    bool GameLayer::LoadScene(const std::string& sceneAssetKey, LoadSceneMode loadMode)
    {
        if (sceneAssetKey.empty())
            return false;

        const std::string requestedSceneAssetKey = sceneAssetKey;

        if (loadMode == LoadSceneMode::Additive)
        {
            const SceneCollection::Handle existingHandle = FindLoadedSceneHandleByAssetKey(requestedSceneAssetKey);
            if (existingHandle != SceneCollection::InvalidHandle)
                return true;
        }
        else
        {
            ClearLoadedScenes();
        }

        auto loadResult = Scene::LoadFromFile(requestedSceneAssetKey);
        if (!loadResult.IsSuccess())
        {
            LT_ERROR("GameLayer: scene load failed: {}", loadResult.GetError().GetErrorMessage());
            return false;
        }

        const bool shouldActivate = loadMode == LoadSceneMode::Single || m_SceneHandle == SceneCollection::InvalidHandle;
        const SceneRoleMask sceneRoles = shouldActivate ? GameLayerInternal::RuntimeSceneActiveRoles : GameLayerInternal::RuntimeSceneBaseRoles;
        const SceneCollection::Handle handle = m_SceneCollection.AddScene(
            std::move(loadResult.GetValue()),
            requestedSceneAssetKey,
            SceneCollectionLifecycleState::Loading,
            sceneRoles);
        Scene* loadedScene = m_SceneCollection.GetScene(handle);
        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
        if (!loadedScene)
            return false;

        loadedScene->BeginLoadingState();
        loadedScene->MarkSceneObjectsInitialized();
        loadedScene->InitializePhysicsWorldForLoading();
        loadedScene->SetLoadStateReady();
        m_SceneCollection.SetLifecycleState(handle, SceneCollectionLifecycleState::Active);

        if (shouldActivate)
            (void)ActivateLoadedScene(handle);

        const auto& registry = loadedScene->GetRegistry();
        const auto* cameraStorage = registry.storage<CameraComponent>();
        const auto* spriteStorage = registry.storage<SpriteComponent>();
        const auto* grid2DStorage = registry.storage<Grid2DComponent>();
        const auto* tilemapLayerStorage = registry.storage<TilemapLayerComponent>();
        const size_t cameraCount = cameraStorage ? cameraStorage->size() : 0;
        const size_t spriteCount = spriteStorage ? spriteStorage->size() : 0;
        const size_t grid2DCount = grid2DStorage ? grid2DStorage->size() : 0;
        const size_t tilemapLayerCount = tilemapLayerStorage ? tilemapLayerStorage->size() : 0;
        LT_INFO("GameLayer: scene diagnostics -> cameras={}, sprites={}, grids={}, layers={}",
            cameraCount, spriteCount, grid2DCount, tilemapLayerCount);

        LT_INFO("GameLayer: scene '{}' loaded.", requestedSceneAssetKey);
        return true;
    }

    bool GameLayer::ActivateLoadedScene(SceneCollection::Handle handle)
    {
        SceneCollection::Record* record = m_SceneCollection.GetRecord(handle);
        if (!record || !record->SceneInstance)
            return false;

        const std::vector<SceneCollection::Handle> loadedHandles = m_SceneCollection.CollectHandlesWithRoles(0u);
        for (const SceneCollection::Handle loadedHandle : loadedHandles)
        {
            if (loadedHandle == handle)
                continue;

            m_SceneCollection.RemoveRoles(loadedHandle, ToSceneRoleMask(SceneRole::GameplayPrimary) | ToSceneRoleMask(SceneRole::ScriptQueryTarget));
            m_SceneCollection.SetLifecycleState(loadedHandle, SceneCollectionLifecycleState::Active);
        }

        m_SceneCollection.AddRoles(handle, ToSceneRoleMask(SceneRole::GameplayPrimary) | ToSceneRoleMask(SceneRole::ScriptQueryTarget));
        m_SceneCollection.SetLifecycleState(handle, SceneCollectionLifecycleState::Active);
        m_SceneHandle = handle;
        m_CurrentSceneAssetKey = record->AssetKey;
        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
        ResetGameplayCameraState();
        return true;
    }

    bool GameLayer::ActivateLoadedSceneByAssetKey(const std::string& sceneAssetKey)
    {
        const SceneCollection::Handle handle = FindLoadedSceneHandleByAssetKey(sceneAssetKey);
        return handle != SceneCollection::InvalidHandle && ActivateLoadedScene(handle);
    }

    bool GameLayer::UnloadLoadedSceneByAssetKey(const std::string& sceneAssetKey)
    {
        const SceneCollection::Handle handle = FindLoadedSceneHandleByAssetKey(sceneAssetKey);
        if (handle == SceneCollection::InvalidHandle)
            return false;

        Scene* scene = m_SceneCollection.GetScene(handle);
        if (scene)
            Audio::StopAudioSourcesInScene(scene);

        const bool wasActiveScene = handle == m_SceneHandle;
        m_SceneCollection.RemoveScene(handle);
        if (!wasActiveScene)
            return true;

        m_SceneHandle = SceneCollection::InvalidHandle;
        m_CurrentSceneAssetKey.clear();
        const std::vector<SceneCollection::Handle> remainingHandles = m_SceneCollection.CollectHandlesWithRoles(0u);
        if (!remainingHandles.empty())
            return ActivateLoadedScene(remainingHandles.front());

        ResetGameplayCameraState();
        return true;
    }

    void GameLayer::ClearLoadedScenes()
    {
        for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(0u))
        {
            if (Scene* scene = m_SceneCollection.GetScene(handle))
                Audio::StopAudioSourcesInScene(scene);
        }

        Physics2DQueries::SetActiveSceneForScriptQueries(nullptr);
        m_SceneCollection.Clear();
        m_SceneHandle = SceneCollection::InvalidHandle;
        m_CurrentSceneAssetKey.clear();
        ResetGameplayCameraState();
    }

    SceneCollection::Handle GameLayer::FindLoadedSceneHandleByAssetKey(const std::string& sceneAssetKey) const
    {
        if (sceneAssetKey.empty())
            return SceneCollection::InvalidHandle;

        const std::string requestedCanonicalKeyLower = ToLower(CanonicalizeSceneKeyForComparison(sceneAssetKey));
        const std::string requestedStemLower = ToLower(ExtractSceneNameStem(sceneAssetKey));
        for (const SceneCollection::Handle handle : m_SceneCollection.CollectHandlesWithRoles(0u))
        {
            const SceneCollection::Record* record = m_SceneCollection.GetRecord(handle);
            if (!record || !record->SceneInstance)
                continue;

            const std::string loadedCanonicalKeyLower = ToLower(CanonicalizeSceneKeyForComparison(record->AssetKey));
            const std::string loadedStemLower = ToLower(ExtractSceneNameStem(record->AssetKey));
            if ((!requestedCanonicalKeyLower.empty() && loadedCanonicalKeyLower == requestedCanonicalKeyLower) ||
                (!requestedStemLower.empty() && loadedStemLower == requestedStemLower))
            {
                return handle;
            }
        }

        return SceneCollection::InvalidHandle;
    }

    std::string GameLayer::ResolveBuildSceneAssetKey(const std::string& sceneIdentifier) const
    {
        const std::string requestedCanonicalKeyLower = ToLower(CanonicalizeSceneKeyForComparison(sceneIdentifier));
        const std::string requestedStemLower = ToLower(ExtractSceneNameStem(sceneIdentifier));
        for (const auto& buildSceneKey : m_BuildScenes)
        {
            const std::string buildCanonicalKeyLower = ToLower(CanonicalizeSceneKeyForComparison(buildSceneKey));
            const std::string buildStemLower = ToLower(ExtractSceneNameStem(buildSceneKey));
            if ((!requestedCanonicalKeyLower.empty() && buildCanonicalKeyLower == requestedCanonicalKeyLower) ||
                (!requestedStemLower.empty() && buildStemLower == requestedStemLower))
            {
                return buildSceneKey;
            }
        }

        return {};
    }

    void GameLayer::ProcessPendingSceneTransitions()
    {
        while (true)
        {
            auto transition = SceneManager::ConsumePendingSceneTransition();
            if (!transition.has_value())
                return;

            if (transition->Type == SceneTransitionType::ReloadCurrentScene)
            {
                LT_INFO("GameLayer: reloading current scene '{}'.", m_CurrentSceneAssetKey);
                if (!m_CurrentSceneAssetKey.empty())
                    LoadScene(m_CurrentSceneAssetKey, LoadSceneMode::Single);
                continue;
            }

            const std::string resolvedSceneAssetKey = ResolveBuildSceneAssetKey(transition->SceneIdentifier);
            if (resolvedSceneAssetKey.empty())
            {
                LT_WARN("GameLayer: scene '{}' not found in build scenes list.", transition->SceneIdentifier);
                continue;
            }

            switch (transition->Type)
            {
                case SceneTransitionType::LoadByAssetKey:
                    (void)LoadScene(resolvedSceneAssetKey, transition->LoadMode);
                    break;
                case SceneTransitionType::SetActiveSceneByAssetKey:
                    if (!ActivateLoadedSceneByAssetKey(resolvedSceneAssetKey))
                        LT_WARN("GameLayer: cannot activate scene '{}'; it is not loaded.", resolvedSceneAssetKey);
                    break;
                case SceneTransitionType::UnloadByAssetKey:
                    if (!UnloadLoadedSceneByAssetKey(resolvedSceneAssetKey))
                        LT_WARN("GameLayer: cannot unload scene '{}'; it is not loaded.", resolvedSceneAssetKey);
                    break;
                case SceneTransitionType::ReloadCurrentScene:
                    break;
            }
        }
    }
}
