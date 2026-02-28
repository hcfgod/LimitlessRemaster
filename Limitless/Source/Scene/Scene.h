#pragma once

#include "Core/Error.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include "Physics/Physics2DWorld.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>
#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Limitless
{
    class Camera;
    class Framebuffer;
    class Physics2DWorld;
    inline constexpr int kSceneSerializationVersion = 20;

    struct DeferredStructuralMutation
    {
        uint64_t Sequence = 0;
        std::function<void()> Apply;
        std::string DebugName;
    };

    // -----------------------------------------------------------------------------
    // Scene
    // Unity-style scene: owns a registry of entities and components.
    // One scene per level or editor context. Create entities, add components, iterate.
    // Uses EnTT for the ECS runtime.
    // -----------------------------------------------------------------------------
    class Scene
    {
    public:
        enum class RuntimePhase : uint8_t
        {
            Idle = 0,
            Structural = 1,
            ScriptMainThread = 2,
            ScriptParallel = 3,
            Simulation = 4,
            Transform = 5,
            RenderBuild = 6
        };

        enum class LoadState : uint8_t
        {
            Loading = 0,
            Ready = 1
        };

        struct EditorCameraBookmark
        {
            glm::vec3 Position{ 0.0f, 0.0f, 0.0f };
            float YawDegrees = -90.0f;
            float PitchDegrees = 0.0f;
        };

        Scene();
        ~Scene();

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;

        /// Create a new entity with TagComponent. Returns entity handle.
        entt::entity CreateEntity(const std::string& name = "Entity");

        /// Create a new entity and return an Entity wrapper (for scripts / new code).
        Entity CreateEntityWrapped(const std::string& name = "Entity");

        /// Instantiate a prefab asset and parent the resulting root entity.
        /// Returns entt::null on failure.
        entt::entity InstantiatePrefab(const std::string& prefabAssetKey, entt::entity parentEntity = entt::null);

        /// Destroy an entity and all its components.
        void DestroyEntity(entt::entity entity);

        /// Check if entity exists.
        bool IsValid(entt::entity entity) const;
        bool IsEntityEnabledInHierarchy(entt::entity entity) const;

        /// Set parent-child relation. Use entt::null parent to make entity root-level.
        bool SetParent(entt::entity child, entt::entity parent);

        /// Reorder entity to appear before target sibling in hierarchy.
        bool SetSiblingOrderBefore(entt::entity entity, entt::entity targetSibling);

        /// Reorder entity to appear after target sibling in hierarchy.
        bool SetSiblingOrderAfter(entt::entity entity, entt::entity targetSibling);

        /// Get entity parent or entt::null when root-level.
        entt::entity GetParent(entt::entity entity) const;

        /// Returns true when entity is a descendant of potentialAncestor.
        bool IsDescendantOf(entt::entity entity, entt::entity potentialAncestor) const;

        /// Get all direct children of parent. Use entt::null for root-level entities.
        std::vector<entt::entity> GetChildren(entt::entity parent) const;

        /// Get world transform matrix with hierarchy applied.
        glm::mat4 GetWorldTransformMatrix(entt::entity entity) const;
        glm::mat4 GetWorldTransformMatrixForRendering(entt::entity entity, float interpolationAlpha) const;
        void MarkTransformDirty(entt::entity entity);
        void UpdateTransforms();
        void FlushDeferredStructuralMutations();
        bool EnqueueDeferredStructuralMutation(std::function<void(Scene&)> mutation, const char* debugName = nullptr);

        RuntimePhase GetRuntimePhase() const { return m_RuntimePhase; }
        void SetRuntimePhase(RuntimePhase phase) { m_RuntimePhase = phase; }
        bool IsApplyingDeferredStructuralMutations() const { return m_IsApplyingDeferredStructuralMutations; }
        bool ShouldDeferStructuralMutations() const;

        static bool IsCurrentThreadParallelScriptExecution();
        static void SetCurrentThreadParallelScriptExecution(bool enabled);

        /// Runtime update for script-driven entity behavior.
        void Update(float deltaTime);
        void FixedUpdate(float fixedDeltaTime);
        void StepPhysics2D(float fixedDeltaTime);

        void BeginLoadingState();
        void MarkSceneObjectsInitialized();
        bool InitializePhysicsWorldForLoading();
        void SetLoadStateReady();

        LoadState GetLoadState() const { return m_LoadState; }
        bool IsReady() const { return m_LoadState == LoadState::Ready; }
        bool IsSceneObjectsInitialized() const { return m_SceneObjectsInitialized; }
        bool IsPhysicsWorldInitializedForLoading() const { return m_PhysicsWorldInitializedForLoading; }
        bool IsUiPointerOverInteractiveElement() const { return m_RuntimeUiPointerOverInteractiveElement; }
        void SetUiPointerOverInteractiveElement(bool isOver) { m_RuntimeUiPointerOverInteractiveElement = isOver; }

        void SetPhysics2DSettings(const Physics2DWorldSettings& settings);
        const Physics2DWorldSettings& GetPhysics2DSettings() const { return m_Physics2DSettings; }
        /// Returns the physics world by slot (0 by default).
        Physics2DWorld* GetPhysics2DWorld(uint16_t worldSlot = 0);
        const Physics2DWorld* GetPhysics2DWorld(uint16_t worldSlot = 0) const;
        uint16_t GetPhysics2DWorldCount() const;
        const Physics2DContactListener* GetPhysics2DContactEvents() const;
        const Physics2DContactListener* GetPhysics2DContactEventsForEntity(entt::entity entity) const;
        bool TryGetPhysics2DBodyDiagnostics(entt::entity entity, Physics2DBodyDiagnostics& outDiagnostics) const;
        Physics2DRaycastHit RaycastClosestAcrossPhysicsWorlds(const glm::vec2& origin,
                                                              const glm::vec2& direction,
                                                              float maxDistance,
                                                              uint64_t collisionMask) const;
        entt::entity ResolveEntityReference(entt::entity entity) const;

        /// Get the EnTT registry for custom queries (views, groups, etc.).
        /// Mutable access is an escape hatch and can bypass scene structural/deferred safeguards.
        /// Prefer Scene/Entity structural APIs for runtime mutations.
        entt::registry& GetRegistry();
        const entt::registry& GetRegistry() const { return m_Registry; }

        /// Create a deep copy of this scene for runtime simulation.
        /// Used by editor Play Mode to keep edit-time data isolated.
        std::unique_ptr<Scene> Clone() const;

        /// Save scene to disk as a scene asset file.
        Result<void> SaveToFile(const std::filesystem::path& path) const;

        /// Load scene from disk scene asset file.
        static Result<std::unique_ptr<Scene>> LoadFromFile(const std::filesystem::path& path);

        /// Store editor camera transform so returning to this scene restores the same view.
        void SetEditorCameraBookmark(const EditorCameraBookmark& bookmark) { m_EditorCameraBookmark = bookmark; }
        void ClearEditorCameraBookmark() { m_EditorCameraBookmark.reset(); }
        const std::optional<EditorCameraBookmark>& GetEditorCameraBookmark() const { return m_EditorCameraBookmark; }

    private:
        void ResetPhysicsRuntimeState();
        void ResetPhysicsRuntimeState(uint16_t worldSlot);
        void EnsurePhysics2DWorldCount(uint16_t worldCount);
        entt::entity AllocateDeferredEntityReference();
        void BindDeferredEntityReference(entt::entity deferredEntity, entt::entity resolvedEntity);
        void ForgetDeferredEntityReference(entt::entity deferredEntity);
        void RemoveDeferredEntityReferencesFor(entt::entity resolvedEntity);

    private:
        entt::registry m_Registry;
        std::optional<EditorCameraBookmark> m_EditorCameraBookmark;
        Physics2DWorldSettings m_Physics2DSettings{};
        std::vector<std::unique_ptr<Physics2DWorld>> m_Physics2DWorlds;
        LoadState m_LoadState = LoadState::Ready;
        bool m_SceneObjectsInitialized = true;
        bool m_PhysicsWorldInitializedForLoading = true;
        bool m_RuntimeUiPointerOverInteractiveElement = false;
        uint64_t m_AnimationDispatchFrameCounter = 0;
        bool m_TransformsDirty = true;
        bool m_HierarchyDepthDirty = true;
        uint16_t m_MaxHierarchyDepth = 0;
        RuntimePhase m_RuntimePhase = RuntimePhase::Idle;
        bool m_IsApplyingDeferredStructuralMutations = false;
        mutable std::atomic<uint32_t> m_WarnedUnsafeMutableRegistryAccessPhases{ 0 };

        static constexpr size_t kDeferredStructuralMutationQueueSize = 8192;
        static constexpr size_t kDeferredStructuralMutationOverflowQueueSize = 65536;
        static constexpr uint32_t kDefaultDeferredStructuralMutationFlushBudget = 1024;
        mutable std::unique_ptr<Concurrency::LockFreeMPMCQueue<DeferredStructuralMutation, kDeferredStructuralMutationQueueSize>> m_DeferredStructuralMutationQueue;
        mutable std::unique_ptr<Concurrency::LockFreeMPMCQueue<DeferredStructuralMutation, kDeferredStructuralMutationOverflowQueueSize>> m_DeferredStructuralMutationOverflowQueue;
        mutable std::mutex m_DeferredStructuralMutationsOverflowMutex;
        std::deque<DeferredStructuralMutation> m_DeferredStructuralMutationsOverflow;
        std::atomic<uint64_t> m_NextDeferredStructuralMutationSequence{ 1 };
        std::atomic<bool> m_WarnedDeferredStructuralMutationQueueOverflow{ false };
        std::atomic<bool> m_WarnedDeferredStructuralMutationFlushBudgetExceeded{ false };
        mutable std::mutex m_DeferredEntityReferencesMutex;
        std::unordered_map<entt::entity, entt::entity> m_DeferredEntityReferences;
        std::atomic<uint64_t> m_NextDeferredEntityReferenceSequence{ 1 };
    };

    // -----------------------------------------------------------------------------
    // SceneRenderer
    // Renders scene entities to the given camera. Uses Renderer2D for SpriteComponent.
    // -----------------------------------------------------------------------------
    class SceneRenderer
    {
    public:
        /// Render scene to the given camera (draws to whatever framebuffer is currently bound).
        static void Render(Scene& scene, const Camera& camera);

        /// Sets the viewport background clear color used before scene rendering.
        static void SetViewportClearColor(const glm::vec4& clearColor);
        /// Gets the currently configured viewport background clear color.
        static glm::vec4 GetViewportClearColor();
        /// Sets the UI input viewport rectangle in window-space pixels.
        /// When enabled, Canvas UI pointer hit-testing uses this rectangle instead of the full window.
        static void SetUiInputViewportRectPixels(float minX, float minY, float width, float height, bool enabled = true);

        /// Render scene to a viewport framebuffer (binds, clears, draws, unbinds).
        /// Use this for editor viewports or off-screen rendering.
        static void RenderToViewport(Scene& scene, const Camera& camera,
            const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height);
    };
}
