#pragma once

#include "Graphics/Camera/Camera.h"

#include <glm/glm.hpp>

namespace Limitless
{
    class OrthographicCamera2D final : public Camera
    {
    public:
        struct Settings
        {
            float Zoom;      // 1.0 = default size
            float NearPlane;
            float FarPlane;

            constexpr Settings()
                : Zoom(1.0f)
                , NearPlane(-1.0f)
                , FarPlane(1.0f)
            {
            }
        };

        // NOTE: Use `Settings{}` explicitly for default args to satisfy Clang/GCC on macOS/Linux.
        OrthographicCamera2D(CameraId id, std::string name, CameraUsage usage, uint32_t widthPixels, uint32_t heightPixels, Settings settings = Settings{});

        void SetViewportSize(uint32_t widthPixels, uint32_t heightPixels) override;

        void SetPosition(const glm::vec2& position);
        void SetPosition(const glm::vec3& position);
        void SetRotationRadians(float rotationRadians);
        void SetProjection(float zoom, float nearPlane, float farPlane);
        void SetZoom(float zoom);

        const glm::vec3& GetPosition() const { return m_Position; }
        float GetRotationRadians() const { return m_RotationRadians; }
        float GetZoom() const { return m_Settings.Zoom; }

        const glm::mat4& GetViewMatrix() const override { return m_View; }
        const glm::mat4& GetProjectionMatrix() const override { return m_Projection; }
        const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProjection; }

    private:
        void RecomputeProjection();
        void RecomputeView();

        uint32_t m_ViewportWidth = 1;
        uint32_t m_ViewportHeight = 1;

        glm::vec3 m_Position{0.0f, 0.0f, 0.0f};
        float m_RotationRadians = 0.0f;
        Settings m_Settings{};

        glm::mat4 m_View{1.0f};
        glm::mat4 m_Projection{1.0f};
        glm::mat4 m_ViewProjection{1.0f};
    };
}

