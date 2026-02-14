#pragma once

#include "Limitless.h"

#include <string>

namespace Limitless::EditorPrefabSystem
{
    /// Writes the selected entity hierarchy as a prefab asset and marks the entity as an instance root.
    bool CreateOrUpdatePrefabFromEntity(Scene& scene, entt::entity rootEntity, const std::string& prefabAssetKey);

    /// Instantiates a prefab asset into the destination scene. Returns the new root entity or entt::null on failure.
    entt::entity InstantiatePrefab(Scene& destinationScene, const std::string& prefabAssetKey, entt::entity parentEntity);

    /// Applies an instance root hierarchy back into its source prefab asset.
    bool ApplyPrefabFromInstance(Scene& scene, entt::entity instanceRootEntity);

    /// Reverts an instance root hierarchy from its source prefab asset and returns the new root entity.
    entt::entity RevertPrefabInstance(Scene& scene, entt::entity instanceRootEntity);

    /// Removes prefab linkage from an instance root without changing child objects.
    bool UnpackPrefabInstance(Scene& scene, entt::entity instanceRootEntity);
}
