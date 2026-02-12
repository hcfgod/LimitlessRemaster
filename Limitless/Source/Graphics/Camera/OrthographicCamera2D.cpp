#include "Graphics/Camera/OrthographicCamera2D.h"

#include "Core/Debug/Log.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Limitless
{
    OrthographicCamera2D::OrthographicCamera2D(CameraId id, std::string name, CameraUsage usage, uint32_t widthPixels, uint32_t heightPixels, Settings settings)
        : Camera(id, std::move(name), CameraType::Orthographic2D, usage)
        , m_ViewportWidth(widthPixels)
        , m_ViewportHeight(heightPixels)
        , m_Settings(settings)
    {
        if (m_ViewportWidth == 0) { m_ViewportWidth = 1; }
        if (m_ViewportHeight == 0) { m_ViewportHeight = 1; }
        if (m_Settings.Zoom <= 0.0f) { m_Settings.Zoom = 1.0f; }

        RecomputeProjection();
        RecomputeView();
    }

    void OrthographicCamera2D::SetViewportSize(uint32_t widthPixels, uint32_t heightPixels)
    {
        m_ViewportWidth = (widthPixels == 0) ? 1 : widthPixels;
        m_ViewportHeight = (heightPixels == 0) ? 1 : heightPixels;
        RecomputeProjection();
    }

    void OrthographicCamera2D::SetPosition(const glm::vec2& position)
    {
        m_Position.x = position.x;
        m_Position.y = position.y;
        RecomputeView();
    }

    void OrthographicCamera2D::SetPosition(const glm::vec3& position)
    {
        m_Position = position;
        RecomputeView();
    }

    void OrthographicCamera2D::SetRotationRadians(float rotationRadians)
    {
        m_RotationRadians = rotationRadians;
        RecomputeView();
    }

    void OrthographicCamera2D::SetZoom(float zoom)
    {
        if (zoom <= 0.0f)
        {
            LT_CORE_WARN("OrthographicCamera2D: zoom must be > 0 (requested {})", zoom);
            return;
        }
        m_Settings.Zoom = zoom;
        RecomputeProjection();
    }

    void OrthographicCamera2D::SetProjection(float zoom, float nearPlane, float farPlane)
    {
        if (zoom <= 0.0f)
        {
            LT_CORE_WARN("OrthographicCamera2D: zoom must be > 0 (requested {})", zoom);
            return;
        }
        if (nearPlane >= farPlane)
        {
            LT_CORE_WARN("OrthographicCamera2D: near plane must be < far plane (near={}, far={})", nearPlane, farPlane);
            return;
        }

        m_Settings.Zoom = zoom;
        m_Settings.NearPlane = nearPlane;
        m_Settings.FarPlane = farPlane;
        RecomputeProjection();
    }

    void OrthographicCamera2D::RecomputeProjection()
    {
        const float aspect = static_cast<float>(m_ViewportWidth) / static_cast<float>(m_ViewportHeight);

        // Define a symmetric orthographic volume centered at origin. Zoom scales the visible size.
        // With zoom=1 and aspect=16/9, visible extents become [-aspect, aspect] x [-1, 1].
        const float halfHeight = 1.0f / m_Settings.Zoom;
        const float halfWidth = aspect * halfHeight;

        m_Projection = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, m_Settings.NearPlane, m_Settings.FarPlane);
        m_ViewProjection = m_Projection * m_View;
    }

    void OrthographicCamera2D::RecomputeView()
    {
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, m_Position);
        transform = glm::rotate(transform, m_RotationRadians, glm::vec3(0.0f, 0.0f, 1.0f));

        // View matrix is inverse of camera transform.
        m_View = glm::inverse(transform);
        m_ViewProjection = m_Projection * m_View;
    }
}

