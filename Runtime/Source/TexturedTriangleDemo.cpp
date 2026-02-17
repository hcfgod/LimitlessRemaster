#include "TexturedTriangleDemo.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Limitless
{
    void TexturedTriangleDemo::Initialize()
    {
        CreateResources();
    }

    void TexturedTriangleDemo::Shutdown()
    {
        m_VAO.reset();
        m_VBO.reset();
        m_IBO.reset();
        m_Material.reset();
        m_LoggedMaterialReady = false;
    }

    void TexturedTriangleDemo::Update(float deltaTime)
    {
        m_TimeSeconds += deltaTime;

        // Smooth clear color transition.
        m_ClearColor[0] = 0.5f + 0.3f * std::sin(m_TimeSeconds * m_ColorChangeSpeed);
        m_ClearColor[1] = 0.5f + 0.3f * std::sin(m_TimeSeconds * m_ColorChangeSpeed + 2.0f);
        m_ClearColor[2] = 0.5f + 0.3f * std::sin(m_TimeSeconds * m_ColorChangeSpeed + 4.0f);
        m_ClearColor[3] = 1.0f;

        for (int i = 0; i < 3; ++i)
        {
            m_ClearColor[i] = std::clamp(m_ClearColor[i], 0.0f, 1.0f);
        }

        PollMaterialReadiness();
    }

    void TexturedTriangleDemo::Render(const CameraManager& cameraManager) const
    {
        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
        {
            return;
        }

        ClearCommand::ClearFlags flags;
        flags.color = true;
        flags.depth = true;
        flags.stencil = false;

        auto clear = std::make_unique<ClearCommand>(flags, m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
        renderer.SubmitCommand(std::move(clear));

        if (!m_VAO || !m_IBO || !m_Material)
        {
            return;
        }

        glm::mat4 viewProjection(1.0f);
        if (const auto* camera = cameraManager.GetCamera(cameraManager.GetActiveCameraId()))
        {
            viewProjection = camera->GetViewProjectionMatrix();
        }

        const glm::mat4 model(1.0f);

        // Only draw when material deps are ready; otherwise we could draw with a stale previously-bound shader.
        if (!m_Material->GetShader() || !m_Material->GetMainTexture())
        {
            return;
        }

        m_Material->SubmitBind(renderer, viewProjection, model);
        renderer.SubmitCommand(std::make_unique<BindVertexArrayCommand>(m_VAO));
        renderer.SubmitCommand(std::make_unique<DrawIndexedCommand>(DrawMode::Triangles, m_IBO->GetCount(), IndexType::UnsignedInt, nullptr, 0));
    }

    void TexturedTriangleDemo::CreateResources()
    {
        struct Vertex
        {
            float Position[3];
            float UV[2];
        };

        const Vertex vertices[3] =
        {
            { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } },
            { {  0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f } },
            { {  0.0f,  0.5f, 0.0f }, { 0.5f, 1.0f } },
        };

        const uint32_t indices[3] = { 0, 1, 2 };

        m_VAO = VertexArray::Create();
        m_VBO = VertexBuffer::Create(vertices, static_cast<uint32_t>(sizeof(vertices)));
        m_VBO->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_UV" }
        });
        m_VAO->AddVertexBuffer(m_VBO);

        m_IBO = IndexBuffer::Create(indices, 3);
        m_VAO->SetIndexBuffer(m_IBO);

        // Material (Unity-style): shader+texture refs live in a single .material.json asset.
        m_Material = Assets::AssetManager::LoadBlocking<Assets::MaterialAsset>("Assets/Materials/TexturedTriangle.material.json");
        if (!m_Material)
        {
            LT_CORE_ERROR("Failed to load material asset for textured triangle");
        }
    }

    void TexturedTriangleDemo::PollMaterialReadiness()
    {
        if (!m_Material)
        {
            return;
        }

        if (!m_LoggedMaterialReady)
        {
            auto shader = m_Material->GetShader();
            auto tex = m_Material->GetMainTexture();
            if (shader && tex)
            {
                m_LoggedMaterialReady = true;
                LT_INFO("Material ready: key='{}' (shader='{}', textureId={})",
                        m_Material->GetKey(),
                        shader->GetName(),
                        tex->GetRendererID());
            }
        }
    }
}

