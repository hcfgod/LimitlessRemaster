#pragma once

#include "Limitless.h"

namespace Limitless
{
    class Scene;
    class EditorUndoService;
}

namespace Limitless::EditorInspectorPanel
{
    void DrawNativeScriptComponentSection(Scene* scene,
                                          entt::registry& registry,
                                          entt::entity selectedEntity,
                                          EditorUndoService* undoService,
                                          bool& outRemoveNativeScriptComponent);
}
