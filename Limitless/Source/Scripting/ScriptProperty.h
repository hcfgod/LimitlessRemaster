#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace Limitless
{
    struct ScriptEntityReference
    {
        std::string Tag;
        std::string PrefabAssetKey;
        std::string SceneEntityId;
    };

    struct Prefab
    {
        std::string AssetKey;
    };

    // Backward compatibility alias for older scripts.
    // Prefer Limitless::Entity fields in new scripts for Unity-style object references.
    using ScriptPrefabReference = Prefab;

    enum class ScriptPropertyType : uint32_t
    {
        Float = 0,
        Integer = 1,
        Boolean = 2,
        Vector3 = 3,
        String = 4,
        Entity = 5,
        Prefab = 6
    };

    using ScriptPropertyValue = std::variant<float, int32_t, bool, glm::vec3, std::string, ScriptEntityReference, Prefab>;
}
