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
    };

    enum class ScriptPropertyType : uint32_t
    {
        Float = 0,
        Integer = 1,
        Boolean = 2,
        Vector3 = 3,
        String = 4,
        Entity = 5
    };

    using ScriptPropertyValue = std::variant<float, int32_t, bool, glm::vec3, std::string, ScriptEntityReference>;
}
