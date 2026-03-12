#pragma once

#include "Limitless.h"

#include <string_view>
#include <vector>

namespace Limitless
{
    class Scene;
    class EditorUndoService;
}

namespace Limitless::EditorInspectorPanel
{
    std::vector<std::string> CollectScriptComponentSectionKeys(Scene* scene, entt::entity selectedEntity);

    void DrawScriptComponentSections(Scene* scene,
                                     entt::registry& registry,
                                     entt::entity selectedEntity,
                                     EditorUndoService* undoService,
                                     std::string_view onlySectionKey = {},
                                     const std::vector<std::string>* orderedSectionKeys = nullptr);
}
