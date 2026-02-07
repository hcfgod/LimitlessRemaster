#pragma once

#include "Graphics/Camera/CameraTypes.h"

#include <glm/glm.hpp>

#include <string>

namespace Limitless
{
    class Camera
    {
    public:
        Camera(CameraId id, std::string name, CameraType type, CameraUsage usage)
            : m_Id(id)
            , m_Name(std::move(name))
            , m_Type(type)
            , m_Usage(usage)
        {
        }

        virtual ~Camera() = default;

        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;
        Camera(Camera&&) = delete;
        Camera& operator=(Camera&&) = delete;

        CameraId GetId() const { return m_Id; }
        const std::string& GetName() const { return m_Name; }
        CameraType GetType() const { return m_Type; }
        CameraUsage GetUsage() const { return m_Usage; }

        void SetName(std::string name) { m_Name = std::move(name); }
        void SetUsage(CameraUsage usage) { m_Usage = usage; }

        virtual void SetViewportSize(uint32_t widthPixels, uint32_t heightPixels) = 0;

        virtual const glm::mat4& GetViewMatrix() const = 0;
        virtual const glm::mat4& GetProjectionMatrix() const = 0;
        virtual const glm::mat4& GetViewProjectionMatrix() const = 0;

    private:
        CameraId m_Id{};
        std::string m_Name;
        CameraType m_Type = CameraType::Perspective3D;
        CameraUsage m_Usage = CameraUsage::Gameplay;
    };
}

