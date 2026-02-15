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
        bool RemoveSpriteComponent = false;
        bool RemoveCameraComponent = false;
        bool RemoveMaterialComponent = false;
        bool RemoveAudioSourceComponent = false;
        bool RemoveTextComponent = false;
        bool RemoveRigidbody2DComponent = false;
        bool RemoveBoxCollider2DComponent = false;
        bool RemoveCircleCollider2DComponent = false;
        bool RemoveJoint2DComponent = false;
        bool RemoveDirectionalLight2DComponent = false;
        bool RemovePointLight2DComponent = false;
        bool RemoveShadowOccluder2DComponent = false;
    };

    void DrawStandardEntityComponentSections(entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* audioPayloadId,
                                             const char* materialPayloadId,
                                             const char* fontPayloadId,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             Limitless::EditorUndoService* undoService);
}
