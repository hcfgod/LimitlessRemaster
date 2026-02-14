#pragma once

#include "Audio/AudioEngine.h"
#include "Limitless.h"
#include "Scene/Scene.h"

namespace Limitless::EditorInspectorPanel
{
    struct PendingEntityComponentRemovals
    {
        bool RemoveSpriteComponent = false;
        bool RemoveCameraComponent = false;
        bool RemoveMaterialComponent = false;
        bool RemoveAudioSourceComponent = false;
        bool RemoveTextComponent = false;
    };

    void DrawStandardEntityComponentSections(entt::registry& registry,
                                             entt::entity selectedEntity,
                                             const char* audioPayloadId,
                                             const char* materialPayloadId,
                                             const char* fontPayloadId,
                                             PendingEntityComponentRemovals& pendingRemovals);
}
