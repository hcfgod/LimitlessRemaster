#include "Assets/LoadingScreen.h"
#include "Assets/AssetLoadProgress.h"
#include "Scene/Scene.h"

#include <algorithm>

namespace Limitless
{
    LoadingScreen::Context LoadingScreen::BuildContext(const Scene* scene, bool shaderReady, const char* shaderProgressKey)
    {
        Context ctx;
        ctx.ShaderReady = shaderReady;
        ctx.ShaderProgressKey = shaderProgressKey;
        if (scene)
        {
            ctx.SceneLoading = (scene->GetLoadState() == Scene::LoadState::Loading);
            ctx.SceneObjectsReady = scene->IsSceneObjectsInitialized();
            ctx.PhysicsWorldReady = scene->IsPhysicsWorldInitializedForLoading();
        }
        return ctx;
    }

    LoadingScreen::State LoadingScreen::GetState(const Context& context)
    {
        State state;
        const std::vector<std::string> activeKeys = Assets::AssetLoadProgress::GetActiveKeys();

        const bool sceneBusy = context.SceneLoading || !context.SceneObjectsReady || !context.PhysicsWorldReady;
        const bool shaderBusy = !context.ShaderReady;
        const bool assetsBusy = !activeKeys.empty();

        state.IsLoading = sceneBusy || shaderBusy || assetsBusy;
        if (!state.IsLoading)
        {
            state.Progress = 1.0f;
            state.StatusText.clear();
            return state;
        }

        // Weights: scene phases (3 slots), shader (1), assets (1). Total 5 slots, each 0..1.
        float sceneObjectsProgress = context.SceneObjectsReady ? 1.0f : 0.0f;
        float physicsProgress = context.PhysicsWorldReady ? 1.0f : 0.0f;
        float sceneLoadingProgress = context.SceneLoading ? 0.0f : 1.0f;
        float shaderProgress = 1.0f;
        if (!context.ShaderReady && context.ShaderProgressKey && context.ShaderProgressKey[0] != '\0')
        {
            const auto info = Assets::AssetLoadProgress::GetProgress(context.ShaderProgressKey);
            if (info.has_value())
                shaderProgress = std::clamp(info->Progress, 0.0f, 1.0f);
        }
        else if (!context.ShaderReady)
        {
            shaderProgress = 0.0f;
        }

        float assetProgressAverage = 1.0f;
        std::string assetStatusText;
        if (!activeKeys.empty())
        {
            float sum = 0.0f;
            for (const std::string& key : activeKeys)
            {
                const auto info = Assets::AssetLoadProgress::GetProgress(key);
                if (info.has_value())
                {
                    sum += std::clamp(info->Progress, 0.0f, 1.0f);
                    if (assetStatusText.empty() && !info->Status.empty())
                        assetStatusText = info->Status;
                }
                else
                {
                    sum += 1.0f;
                }
            }
            assetProgressAverage = sum / static_cast<float>(activeKeys.size());
        }

        state.Progress = (sceneObjectsProgress + physicsProgress + sceneLoadingProgress + shaderProgress + assetProgressAverage) / 5.0f;
        state.Progress = std::clamp(state.Progress, 0.0f, 1.0f);

        // Status text priority: scene objects -> physics -> shader -> assets -> generic
        if (!context.SceneObjectsReady)
            state.StatusText = "Initializing scene objects...";
        else if (!context.PhysicsWorldReady)
            state.StatusText = "Initializing physics world...";
        else if (!context.ShaderReady)
        {
            if (context.ShaderProgressKey && context.ShaderProgressKey[0] != '\0')
            {
                const auto info = Assets::AssetLoadProgress::GetProgress(context.ShaderProgressKey);
                state.StatusText = (info.has_value() && !info->Status.empty()) ? info->Status : "Compiling shaders...";
            }
            else
                state.StatusText = "Compiling shaders...";
        }
        else if (!assetStatusText.empty())
            state.StatusText = assetStatusText;
        else if (assetsBusy)
            state.StatusText = "Loading assets...";
        else
            state.StatusText = "Loading...";

        return state;
    }
}
