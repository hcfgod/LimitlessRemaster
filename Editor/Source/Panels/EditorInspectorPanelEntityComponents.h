#pragma once

#include "Audio/AudioEngine.h"
#include "Limitless.h"
#include "Scene/Scene.h"

#include <string_view>
#include <vector>

namespace Limitless
{
    class EditorUndoService;
}

namespace Limitless::EditorInspectorPanel
{
    struct PendingEntityComponentRemovals
    {
        bool RemoveCanvasComponent = false;
        bool RemoveRectTransformComponent = false;
        bool RemoveUIImageComponent = false;
        bool RemoveUIPanelComponent = false;
        bool RemoveUITextComponent = false;
        bool RemoveUIButtonComponent = false;
        bool RemoveUISliderComponent = false;
        bool RemoveSpriteComponent = false;
        bool RemoveCameraComponent = false;
        bool RemoveAudioListener2DComponent = false;
        bool RemoveAudioListener3DComponent = false;
        bool RemoveMaterialComponent = false;
        bool RemoveAudioSourceComponent = false;
        bool RemoveRigidbody2DComponent = false;
        bool RemoveBoxCollider2DComponent = false;
        bool RemoveCircleCollider2DComponent = false;
        bool RemovePolygonCollider2DComponent = false;
        bool RemoveEdgeCollider2DComponent = false;
        bool RemoveCapsuleCollider2DComponent = false;
        bool RemoveJoint2DComponent = false;
        bool RemoveDirectionalLight2DComponent = false;
        bool RemovePointLight2DComponent = false;
        bool RemoveShadowOccluder2DComponent = false;
        bool RemoveAnimatorComponent = false;
        bool RemoveAnimationEventReceiverComponent = false;
        bool RemoveParticleEmitterComponent = false;
        bool RemoveGrid2DComponent = false;
        bool RemoveTilemapLayerComponent = false;
    };

    void DrawEntityHeaderSection(Scene* scene,
                                 entt::registry& registry,
                                 entt::entity selectedEntity,
                                 EditorUndoService* undoService);

    std::vector<std::string> CollectStandardEntityComponentSectionKeys(entt::registry& registry,
                                                                       entt::entity selectedEntity);

    void DrawStandardEntityComponentSections(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* texturePayloadId,
                                             const char* audioPayloadId,
                                             const char* materialPayloadId,
                                             const char* fontPayloadId,
                                             std::string& selectedAnimationClipAssetKey,
                                             std::string& selectedAnimatorControllerAssetKey,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             Limitless::EditorUndoService* undoService,
                                             std::string_view onlySectionKey = {},
                                             const std::vector<std::string>* orderedSectionKeys = nullptr);
}
