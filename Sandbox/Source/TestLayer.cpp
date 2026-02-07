#include "TestLayer.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <glm/gtc/matrix_transform.hpp>

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

        m_TriangleShader = Shader::CreateFromSource("TriangleShader", vertexSrc, fragmentSrc);
        m_TriangleShader->SetInt("u_Texture", 0);

        // Camera + Input Actions (Unity-style)
        m_InputAsset = std::make_shared<InputActionAsset>();
        auto& map = m_InputAsset->AddMap("Editor");

        auto& move = map.AddAction("Move", InputActionValueType::Axis2D);
        move.AddBinding(KeyboardAxis2DBinding{
            .Up = SDL_SCANCODE_W,
            .Down = SDL_SCANCODE_S,
            .Left = SDL_SCANCODE_A,
            .Right = SDL_SCANCODE_D,
            .Scale = 1.0f
        });

        auto& look = map.AddAction("Look", InputActionValueType::Axis2D);
        look.AddBinding(MouseDeltaBinding{
            .Sensitivity = 1.0f,
            .InvertY = false
        });

        auto& boost = map.AddAction("Boost", InputActionValueType::Button);
        boost.AddBinding(KeyboardButtonBinding{ .Key = SDL_SCANCODE_LSHIFT });

        auto& lookEnable = map.AddAction("LookEnable", InputActionValueType::Button);
        lookEnable.AddBinding(MouseButtonBinding{ .Button = SDL_BUTTON_RIGHT });

        m_ActionMove = map.FindAction("Move");
        m_ActionLook = map.FindAction("Look");
        m_ActionBoost = map.FindAction("Boost");
        m_ActionLookEnable = map.FindAction("LookEnable");

        GetInputSystem().SetActionAsset(m_InputAsset);

        // Create an editor camera (3D) and make it active.
        CameraManager::Perspective3DCreateInfo cameraInfo{};
        cameraInfo.Name = "EditorCamera";
        cameraInfo.Usage = CameraUsage::Editor;
        cameraInfo.ViewportWidthPixels = 1280;
        cameraInfo.ViewportHeightPixels = 720;
        cameraInfo.FieldOfViewYDegrees = 60.0f;
        cameraInfo.NearPlane = 0.1f;
        cameraInfo.FarPlane = 1000.0f;

        m_CameraId = m_CameraManager.CreatePerspective3D(cameraInfo);
        m_CameraManager.SetActiveCamera(m_CameraId);

        if (auto* camera = m_CameraManager.GetPerspective3D(m_CameraId))
        {
            camera->SetPosition(glm::vec3(0.0f, 0.0f, 2.0f));
            camera->SetYawPitchDegrees(-90.0f, 0.0f);
        }

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

        // Prove the non-blocking path works: schedule GPU upload on the render thread and continue.
        m_CheckerboardTextureFuture = Texture2D::CreateFromRGBA8Async(2, 2, checkerRGBA, textureSpec);
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

        // Async texture completion check.
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

        // Camera movement (WASD) + mouse look (delta)
        auto* camera = m_CameraManager.GetPerspective3D(m_CameraId);
        const bool wantLook = (m_ActionLookEnable != nullptr) ? m_ActionLookEnable->ReadButton() : true;

        // Unity/editor style: lock+hide cursor while RMB is held.
        if (Application::HasInstance())
        {
            auto& window = Application::GetInstance().GetWindow();
            window.SetCursorLocked(wantLook);
            window.SetCursorVisible(!wantLook);
        }

        if (camera && m_ActionMove && m_ActionLook)
        {
            const glm::vec2 move = m_ActionMove->ReadAxis2D();
            const glm::vec2 look = wantLook ? m_ActionLook->ReadAxis2D() : glm::vec2(0.0f);

            // Mouse delta -> yaw/pitch (radians-ish scaled).
            const float yaw = camera->GetYawDegrees() + (look.x * (m_CameraLookSensitivity * 180.0f / 3.14159265f));
            const float pitch = camera->GetPitchDegrees() + (-look.y * (m_CameraLookSensitivity * 180.0f / 3.14159265f));
            camera->SetYawPitchDegrees(yaw, pitch);

            const float boost = (m_ActionBoost && m_ActionBoost->ReadButton()) ? m_CameraBoostMultiplier : 1.0f;
            const float speed = m_CameraMoveSpeed * boost;

            const glm::vec3 forward = camera->GetForwardDirection();
            const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

            glm::vec3 position = camera->GetPosition();
            position += forward * (move.y * speed * deltaTime);
            position += right * (move.x * speed * deltaTime);
            camera->SetPosition(position);
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
            // Camera matrices (set on render thread).
            glm::mat4 viewProjection(1.0f);
            if (const auto* camera = m_CameraManager.GetCamera(m_CameraManager.GetActiveCameraId()))
            {
                viewProjection = camera->GetViewProjectionMatrix();
            }

            const glm::mat4 model = glm::mat4(1.0f);

            renderer.SubmitCommand(std::make_unique<BindShaderCommand>(m_TriangleShader));
            renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(m_TriangleShader, "u_ViewProjection", viewProjection));
            renderer.SubmitCommand(std::make_unique<SetShaderMat4Command>(m_TriangleShader, "u_Model", model));
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

    void TestLayer::OnWindowResize(Events::WindowResizeEvent& event)
    {
        if (auto* camera = m_CameraManager.GetCamera(m_CameraId))
        {
            camera->SetViewportSize(event.GetWidth(), event.GetHeight());
        }
    }
} 