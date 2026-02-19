#pragma once

#include "Audio/AudioEngine.h"
#include "Limitless.h"
#include "Scene/Scene.h"

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
        bool RemoveMaterialComponent = false;
        bool RemoveAudioSourceComponent = false;
        bool RemoveRigidbody2DComponent = false;
        bool RemoveBoxCollider2DComponent = false;
        bool RemoveCircleCollider2DComponent = false;
        bool RemoveJoint2DComponent = false;
        bool RemoveDirectionalLight2DComponent = false;
        bool RemovePointLight2DComponent = false;
        bool RemoveShadowOccluder2DComponent = false;
        bool RemoveTilemapComponent = false;
        bool RemoveTilemapCollider2DComponent = false;
        bool RemoveAnimatorComponent = false;
        bool RemoveAnimationEventReceiverComponent = false;
        bool RemoveParticleEmitterComponent = false;
    };

    void DrawStandardEntityComponentSections(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* texturePayloadId,
                                             const char* audioPayloadId,
                                             const char* materialPayloadId,
                                             const char* fontPayloadId,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             Limitless::EditorUndoService* undoService);
}
