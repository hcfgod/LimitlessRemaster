#include "Graphics/Camera/PerspectiveCamera3D.h"

#include "Core/Debug/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Limitless
{
    static float ClampPitch(float pitchDegrees)
    {
        // Avoid gimbal singularities in simple yaw/pitch camera.
        const float maxPitch = 89.0f;
        if (pitchDegrees > maxPitch) { return maxPitch; }
        if (pitchDegrees < -maxPitch) { return -maxPitch; }
        return pitchDegrees;
    }

    PerspectiveCamera3D::PerspectiveCamera3D(CameraId id, std::string name, CameraUsage usage, uint32_t widthPixels, uint32_t heightPixels, Settings settings)
        : Camera(id, std::move(name), CameraType::Perspective3D, usage)
        , m_ViewportWidth(widthPixels)
        , m_ViewportHeight(heightPixels)
        , m_Settings(settings)
    {
        if (m_ViewportWidth == 0) { m_ViewportWidth = 1; }
        if (m_ViewportHeight == 0) { m_ViewportHeight = 1; }

        if (m_Settings.FieldOfViewYDegrees <= 1.0f) { m_Settings.FieldOfViewYDegrees = 60.0f; }
        if (m_Settings.NearPlane <= 0.0f) { m_Settings.NearPlane = 0.1f; }
        if (m_Settings.FarPlane <= m_Settings.NearPlane) { m_Settings.FarPlane = m_Settings.NearPlane + 1000.0f; }

        m_PitchDegrees = ClampPitch(m_PitchDegrees);

        RecomputeProjection();
        RecomputeView();
    }

    void PerspectiveCamera3D::SetViewportSize(uint32_t widthPixels, uint32_t heightPixels)
    {
        m_ViewportWidth = (widthPixels == 0) ? 1 : widthPixels;
        m_ViewportHeight = (heightPixels == 0) ? 1 : heightPixels;
        RecomputeProjection();
    }

    void PerspectiveCamera3D::SetPosition(const glm::vec3& position)
    {
        m_Position = position;
        RecomputeView();
    }

    void PerspectiveCamera3D::SetYawPitchDegrees(float yawDegrees, float pitchDegrees)
    {
        m_YawDegrees = yawDegrees;
        m_PitchDegrees = ClampPitch(pitchDegrees);
        RecomputeView();
    }

    void PerspectiveCamera3D::SetPerspective(float fieldOfViewYDegrees, float nearPlane, float farPlane)
    {
        if (fieldOfViewYDegrees <= 1.0f)
        {
            LT_CORE_WARN("PerspectiveCamera3D: invalid FOV (requested {})", fieldOfViewYDegrees);
            return;
        }
        if (nearPlane <= 0.0f || farPlane <= nearPlane)
        {
            LT_CORE_WARN("PerspectiveCamera3D: invalid near/far (near={}, far={})", nearPlane, farPlane);
            return;
        }

        m_Settings.FieldOfViewYDegrees = fieldOfViewYDegrees;
        m_Settings.NearPlane = nearPlane;
        m_Settings.FarPlane = farPlane;
        RecomputeProjection();
    }

    glm::vec3 PerspectiveCamera3D::GetForwardDirection() const
    {
        const float yawRad = glm::radians(m_YawDegrees);
        const float pitchRad = glm::radians(m_PitchDegrees);

        glm::vec3 forward;
        forward.x = std::cos(yawRad) * std::cos(pitchRad);
        forward.y = std::sin(pitchRad);
        forward.z = std::sin(yawRad) * std::cos(pitchRad);

        return glm::normalize(forward);
    }

    void PerspectiveCamera3D::RecomputeProjection()
    {
        const float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);
        m_Projection = glm::perspective(glm::radians(m_Settings.FieldOfViewYDegrees), aspect, m_Settings.NearPlane, m_Settings.FarPlane);
        m_ViewProjection = m_Projection * m_View;
    }

    void PerspectiveCamera3D::RecomputeView()
    {
        const glm::vec3 forward = GetForwardDirection();
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        m_View = glm::lookAt(m_Position, m_Position + forward, up);
        m_ViewProjection = m_Projection * m_View;
    }
}

