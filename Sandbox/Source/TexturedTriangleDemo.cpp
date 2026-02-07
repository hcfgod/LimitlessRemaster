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
        m_Shader.reset();
        m_VAO.reset();
        m_VBO.reset();
        m_IBO.reset();
        m_CheckerboardTexture.reset();
        m_CheckerboardTextureFuture = {};
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

        PollAsyncTexture();
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

        if (!m_Shader || !m_VAO || !m_IBO || !m_CheckerboardTexture)
        {
            return;
        }

        glm::mat4 viewProjection(1.0f);
        if (const auto* camera = cameraManager.GetCamera(cameraManager.GetActiveCameraId()))
        {
            viewProjection = camera->GetViewProjectionMatrix();
        }

        const glm::mat4 model(1.0f);

        renderer.SubmitCommand(std::make_unique<BindShaderCommand>(m_Shader));
        renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(m_Shader, "u_ViewProjection", viewProjection));
        renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(m_Shader, "u_Model", model));
        renderer.SubmitCommand(std::make_unique<BindTextureCommand>(m_CheckerboardTexture, 0));
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

        const std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec2 a_UV;
            out vec2 v_UV;
            uniform mat4 u_ViewProjection;
            uniform mat4 u_Model;
            void main()
            {
                v_UV = a_UV;
                gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);
            }
        )";

        const std::string fragmentSrc = R"(
            #version 330 core
            in vec2 v_UV;
            out vec4 FragColor;
            uniform sampler2D u_Texture;
            void main()
            {
                FragColor = texture(u_Texture, v_UV);
            }
        )";

        m_Shader = Shader::CreateFromSource("TriangleShader", vertexSrc, fragmentSrc);
        m_Shader->SetInt("u_Texture", 0);

        const uint8_t checkerRGBA[2 * 2 * 4] =
        {
            255, 255, 255, 255,   0,   0,   0, 255,
              0,   0,   0, 255, 255, 255, 255, 255
        };

        TextureSpecification textureSpec{};
        textureSpec.GenerateMipmaps = false;
        textureSpec.MinFilter = TextureFilter::Nearest;
        textureSpec.MagFilter = TextureFilter::Nearest;
        textureSpec.WrapU = TextureWrap::Repeat;
        textureSpec.WrapV = TextureWrap::Repeat;

        m_CheckerboardTextureFuture = Texture2D::CreateFromRGBA8Async(2, 2, checkerRGBA, textureSpec);
    }

    void TexturedTriangleDemo::PollAsyncTexture()
    {
        if (!m_CheckerboardTexture && m_CheckerboardTextureFuture.valid())
        {
            if (m_CheckerboardTextureFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                m_CheckerboardTexture = m_CheckerboardTextureFuture.get();
                if (m_CheckerboardTexture)
                {
                    LT_INFO("Async texture upload complete ({}x{}, id={})",
                            m_CheckerboardTexture->GetWidth(),
                            m_CheckerboardTexture->GetHeight(),
                            m_CheckerboardTexture->GetRendererID());
                }
            }
        }
    }
}

