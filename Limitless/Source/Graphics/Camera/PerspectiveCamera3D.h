#pragma once

#include "Graphics/Camera/Camera.h"

#include <glm/glm.hpp>

namespace Limitless
{
    class PerspectiveCamera3D final : public Camera
    {
    public:
        struct Settings
        {
            float FieldOfViewYDegrees;
            float NearPlane;
            float FarPlane;

            constexpr Settings()
                : FieldOfViewYDegrees(60.0f)
                , NearPlane(0.1f)
                , FarPlane(1000.0f)
            {
            }
        };

        // NOTE: Use `Settings{}` explicitly for default args to satisfy Clang/GCC on macOS/Linux.
        PerspectiveCamera3D(CameraId id, std::string name, CameraUsage usage, uint32_t widthPixels, uint32_t heightPixels, Settings settings = Settings{});

        void SetViewportSize(uint32_t widthPixels, uint32_t heightPixels) override;

        void SetPosition(const glm::vec3& position);
        void SetYawPitchDegrees(float yawDegrees, float pitchDegrees);
        void SetPerspective(float fieldOfViewYDegrees, float nearPlane, float farPlane);

        glm::vec3 GetPosition() const { return m_Position; }
        float GetYawDegrees() const { return m_YawDegrees; }
        float GetPitchDegrees() const { return m_PitchDegrees; }
        float GetFieldOfViewYDegrees() const { return m_Settings.FieldOfViewYDegrees; }
        float GetNearPlane() const { return m_Settings.NearPlane; }
        float GetFarPlane() const { return m_Settings.FarPlane; }

        glm::vec3 GetForwardDirection() const;

        const glm::mat4& GetViewMatrix() const override { return m_View; }
        const glm::mat4& GetProjectionMatrix() const override { return m_Projection; }
        const glm::mat4& GetViewProjectionMatrix() const override { return m_ViewProjection; }

    private:
        void RecomputeProjection();
        void RecomputeView();

        uint32_t m_ViewportWidth = 1;
        uint32_t m_ViewportHeight = 1;

        glm::vec3 m_Position{0.0f, 0.0f, 5.0f};
        float m_YawDegrees = -90.0f;   // Looking down -Z by default.
        float m_PitchDegrees = 0.0f;

        Settings m_Settings{};

        glm::mat4 m_View{1.0f};
        glm::mat4 m_Projection{1.0f};
        glm::mat4 m_ViewProjection{1.0f};
    };
}

