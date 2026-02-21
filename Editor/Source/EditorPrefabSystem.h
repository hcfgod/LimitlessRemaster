#pragma once

#include "Limitless.h"

#include <memory>
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

    /// Re-instantiates all prefab instances in the scene that reference the given prefab asset key.
    /// This is the "Apply to Instances" behavior used by prefab editing workflows.
    bool ApplyPrefabAssetToInstancesInScene(Scene& scene, const std::string& prefabAssetKey);

    /// Creates a detached scene containing a deep copy of a root entity subtree.
    /// Returns nullptr on failure.
    std::unique_ptr<Scene> CreateDetachedEntitySubtree(const Scene& sourceScene, entt::entity sourceRootEntity);

    /// Instantiates a detached entity subtree scene into the destination scene.
    /// The source scene should contain one root-level entity.
    entt::entity InstantiateDetachedEntitySubtree(Scene& destinationScene, const Scene& sourceSubtreeScene, entt::entity parentEntity);
}
