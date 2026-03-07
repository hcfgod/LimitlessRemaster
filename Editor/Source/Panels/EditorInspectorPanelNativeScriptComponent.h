#pragma once

#include "Limitless.h"

namespace Limitless
{
    class Scene;
    class EditorUndoService;
}

namespace Limitless::EditorInspectorPanel
{
    void DrawScriptComponentSections(Scene* scene,
                                     entt::registry& registry,
                                     entt::entity selectedEntity,
                                     EditorUndoService* undoService);
}
