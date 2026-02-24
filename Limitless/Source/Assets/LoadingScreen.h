#pragma once

#include <string>

namespace Limitless
{
    class Scene;

    // -----------------------------------------------------------------------------
    // LoadingScreen
    // Centralized loading state for a single "loading screen" API.
    //
    // Aggregates:
    // - AssetLoadProgress (per-asset progress from loaders)
    // - Optional scene load state (scene objects, physics init)
    // - Optional default shader readiness
    //
    // Usage: Fill LoadingScreenContext from your Scene and Renderer2D (or game state),
    // then call GetState(). Use IsLoading / Progress / StatusText to drive a loading
    // overlay or blocking screen. No ImGui dependency; draw the overlay in your layer.
    // -----------------------------------------------------------------------------
    class LoadingScreen final
    {
    public:
        /// Input: pass current scene and shader state. All optional.
        struct Context
        {
            /// Scene is in Loading state (not Ready).
            bool SceneLoading = false;
            /// Scene objects have been initialized (for loading flow).
            bool SceneObjectsReady = true;
            /// Physics world has been initialized for loading.
            bool PhysicsWorldReady = true;
            /// Default shader (e.g. Renderer2D) is ready.
            bool ShaderReady = true;
            /// Asset key for default shader progress (e.g. Renderer2D::GetDefaultShaderKey()). Can be null.
            const char* ShaderProgressKey = nullptr;
        };

        /// Aggregated state for one frame.
        struct State
        {
            bool IsLoading = false;
            float Progress = 0.0f;   ///< 0.0 to 1.0
            std::string StatusText;  ///< Human-readable status (e.g. "Compiling shaders...")
        };

        /// Build context from a scene and shader-ready flag. Helper for common case.
        static Context BuildContext(const Scene* scene, bool shaderReady, const char* shaderProgressKey = nullptr);

        /// Compute aggregated loading state from asset progress and the given context.
        static State GetState(const Context& context);
    };
}
