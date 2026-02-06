#include "TestLayer.h"
#include <algorithm>
#include <cmath>

namespace Limitless
{
    TestLayer::TestLayer()
        : Layer("TestLayer")
        , m_ColorChangeSpeed(0.5f) // Change color every 0.5 seconds
    {
        // Initialize clear color to a nice blue
        m_ClearColor[0] = 0.2f; // Red
        m_ClearColor[1] = 0.3f; // Green
        m_ClearColor[2] = 0.8f; // Blue
        m_ClearColor[3] = 1.0f; // Alpha

        LT_INFO("TestLayer created");
    }

    void TestLayer::OnAttach()
    {
        LT_INFO("TestLayer attached");

        // -----------------------------------------------------------------------------
        // Textured triangle demo setup (VAO/VBO/IBO + shader + texture)
        // -----------------------------------------------------------------------------
        struct Vertex
        {
            float position[3];
            float uv[2];
        };

        const Vertex vertices[3] =
        {
            { { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f } },
            { {  0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f } },
            { {  0.0f,  0.5f, 0.0f }, { 0.5f, 1.0f } },
        };

        const uint32_t indices[3] = { 0, 1, 2 };

        m_TriangleVAO = VertexArray::Create();
        m_TriangleVBO = VertexBuffer::Create(vertices, static_cast<uint32_t>(sizeof(vertices)));
        m_TriangleVBO->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_UV" }
        });
        m_TriangleVAO->AddVertexBuffer(m_TriangleVBO);

        m_TriangleIBO = IndexBuffer::Create(indices, 3);
        m_TriangleVAO->SetIndexBuffer(m_TriangleIBO);

        const std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec2 a_UV;
            out vec2 v_UV;
            void main()
            {
                v_UV = a_UV;
                gl_Position = vec4(a_Position, 1.0);
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

        m_TriangleShader = Shader::CreateFromSource("TriangleShader", vertexSrc, fragmentSrc);
        m_TriangleShader->SetInt("u_Texture", 0);

        // Tiny 2x2 checkerboard (RGBA8) so we don't depend on external asset paths yet.
        // Stored explicitly as RGBA bytes to avoid endianness assumptions.
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
        m_CheckerboardTexture = Texture2D::CreateFromRGBA8(2, 2, checkerRGBA, textureSpec);
    }

    void TestLayer::OnDetach()
    {
        LT_INFO("TestLayer detached");
    }

    void TestLayer::OnUpdate(float deltaTime)
    {
        // Update the clear color over time to create a nice visual effect
        m_TimeSeconds += deltaTime;

        // Create a smooth color transition using sine waves
        m_ClearColor[0] = 0.5f + 0.3f * std::sin(m_TimeSeconds * m_ColorChangeSpeed);
        m_ClearColor[1] = 0.5f + 0.3f * std::sin(m_TimeSeconds * m_ColorChangeSpeed + 2.0f);
        m_ClearColor[2] = 0.5f + 0.3f * std::sin(m_TimeSeconds * m_ColorChangeSpeed + 4.0f);
        m_ClearColor[3] = 1.0f; // Keep alpha at 1.0

        // Clamp values to valid range
        for (int i = 0; i < 3; ++i)
        {
            m_ClearColor[i] = std::clamp(m_ClearColor[i], 0.0f, 1.0f);
        }
    }

    void TestLayer::OnRender()
    {
        auto& renderer = Renderer::GetInstance();
        
        if (!renderer.IsInitialized())
        {
            LT_WARN("Renderer not initialized in TestLayer");
            return;
        }

        // Create a clear command with our current color
        ClearCommand::ClearFlags flags;
        flags.color = true;
        flags.depth = true;
        flags.stencil = false;

        auto clearCommand = std::make_unique<ClearCommand>(
            flags,
            m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]
        );

        // Submit the clear command to the global renderer
        if (!renderer.SubmitCommand(std::move(clearCommand)))
        {
            LT_WARN("Failed to submit clear command to renderer");
        }

        // Draw the triangle via render commands.
        if (m_TriangleShader && m_TriangleVAO && m_TriangleIBO && m_CheckerboardTexture)
        {
            renderer.SubmitCommand(std::make_unique<BindShaderCommand>(m_TriangleShader));
            renderer.SubmitCommand(std::make_unique<BindTextureCommand>(m_CheckerboardTexture, 0));
            renderer.SubmitCommand(std::make_unique<BindVertexArrayCommand>(m_TriangleVAO));
            renderer.SubmitCommand(std::make_unique<DrawIndexedCommand>(
                DrawMode::Triangles,
                m_TriangleIBO->GetCount(),
                IndexType::UnsignedInt,
                nullptr,
                0));
        }
    }
} 