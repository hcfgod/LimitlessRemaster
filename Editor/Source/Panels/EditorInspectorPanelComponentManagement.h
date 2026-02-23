#pragma once

#include "EditorInspectorPanelEntityComponents.h"

namespace Limitless
{
    class Scene;
    class EditorUndoService;
}

namespace Limitless::EditorInspectorPanel
{
    void DrawAddComponentPopup(Scene* scene,
                               entt::registry& registry,
                               entt::entity selectedEntity,
                               EditorUndoService* undoService);

    void ApplyPendingEntityComponentRemovals(Scene* scene,
                                             entt::registry& registry,
                                             entt::entity selectedEntity,
                                             PendingEntityComponentRemovals& pendingRemovals,
                                             bool removeNativeScriptComponent,
                                             EditorUndoService* undoService);
}
