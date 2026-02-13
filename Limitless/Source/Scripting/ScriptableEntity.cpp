#include "Scripting/ScriptableEntity.h"

namespace Limitless
{
    float ScriptableEntity::GetExposedFloat(const std::string& name, float fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<float>(&found->second))
            return *value;
        return fallbackValue;
    }

    int32_t ScriptableEntity::GetExposedInteger(const std::string& name, int32_t fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<int32_t>(&found->second))
            return *value;
        return fallbackValue;
    }

    bool ScriptableEntity::GetExposedBoolean(const std::string& name, bool fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<bool>(&found->second))
            return *value;
        return fallbackValue;
    }

    glm::vec3 ScriptableEntity::GetExposedVector3(const std::string& name, const glm::vec3& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<glm::vec3>(&found->second))
            return *value;
        return fallbackValue;
    }

    std::string ScriptableEntity::GetExposedString(const std::string& name, const std::string& fallbackValue) const
    {
        if (!m_ExposedProperties)
            return fallbackValue;
        const auto found = m_ExposedProperties->find(name);
        if (found == m_ExposedProperties->end())
            return fallbackValue;
        if (const auto* value = std::get_if<std::string>(&found->second))
            return *value;
        return fallbackValue;
    }

    void ScriptableEntity::SetExposedFloat(const std::string& name, float value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedInteger(const std::string& name, int32_t value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedBoolean(const std::string& name, bool value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedVector3(const std::string& name, const glm::vec3& value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }

    void ScriptableEntity::SetExposedString(const std::string& name, const std::string& value)
    {
        if (m_ExposedProperties)
            (*m_ExposedProperties)[name] = value;
    }
}
