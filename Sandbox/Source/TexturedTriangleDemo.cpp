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
        m_ShaderAsset.reset();
        m_Shader.reset();
        m_VAO.reset();
        m_VBO.reset();
        m_IBO.reset();
        m_CheckerboardTextureAsset.reset();
        m_CheckerboardTexture.reset();
        m_CheckerboardTextureAssetTask = Async::Task<Assets::TextureAsset::Ptr>();
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

        PollAsyncTextureAsset();

        // Hot reload support:
        // If the asset updated its internal GPU object, refresh our local pointers.
        if (m_ShaderAsset)
        {
            auto current = m_ShaderAsset->GetShader();
            if (current && current != m_Shader)
            {
                m_Shader = current;
                // Re-apply required uniforms after shader recompiles.
                m_Shader->SetInt("u_Texture", 0);
                LT_INFO("Shader hot reloaded: '{}'", m_ShaderAsset->GetKey());
            }
        }

        if (m_CheckerboardTextureAsset)
        {
            auto current = m_CheckerboardTextureAsset->GetTexture();
            if (current && current != m_CheckerboardTexture)
            {
                m_CheckerboardTexture = current;
                LT_INFO("Texture hot reloaded: '{}'", m_CheckerboardTextureAsset->GetKey());
            }
        }
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

        m_ShaderAsset = Assets::AssetManager::LoadBlocking<Assets::ShaderAsset>("Assets/Shaders/TexturedTriangle.glsl");
        if (!m_ShaderAsset || !m_ShaderAsset->GetShader())
        {
            LT_CORE_ERROR("Failed to load shader asset for textured triangle");
            return;
        }
        m_Shader = m_ShaderAsset->GetShader();
        m_Shader->SetInt("u_Texture", 0);

        TextureSpecification textureSpec{};
        textureSpec.GenerateMipmaps = false;
        textureSpec.MinFilter = TextureFilter::Nearest;
        textureSpec.MagFilter = TextureFilter::Nearest;
        textureSpec.WrapU = TextureWrap::Repeat;
        textureSpec.WrapV = TextureWrap::Repeat;

        // Unity-style asset load: file in `Assets/` folder. stb_image supports ASCII PPM, so this is text-only.
        m_CheckerboardTextureAssetTask = Assets::AssetManager::LoadAsync<Assets::TextureAsset>("Assets/Textures/Checker.ppm", textureSpec);
    }

    void TexturedTriangleDemo::PollAsyncTextureAsset()
    {
        if (!m_CheckerboardTexture && m_CheckerboardTextureAssetTask.IsValid() && m_CheckerboardTextureAssetTask.IsDone())
        {
            Assets::TextureAsset::Ptr asset;
            try
            {
                asset = m_CheckerboardTextureAssetTask.Get();
            }
            catch (const std::exception& e)
            {
                LT_CORE_ERROR("TextureAsset task threw while resolving checkerboard texture: {}", e.what());
                m_CheckerboardTextureAssetTask = Async::Task<Assets::TextureAsset::Ptr>();
                return;
            }
            catch (...)
            {
                LT_CORE_ERROR("TextureAsset task threw (unknown) while resolving checkerboard texture");
                m_CheckerboardTextureAssetTask = Async::Task<Assets::TextureAsset::Ptr>();
                return;
            }

            if (asset && asset->GetTexture())
            {
                m_CheckerboardTextureAsset = asset;
                m_CheckerboardTexture = asset->GetTexture();
                LT_INFO("TextureAsset ready: key='{}' guid='{}' ({}x{}, id={})",
                        asset->GetKey(),
                        asset->GetGuid(),
                        m_CheckerboardTexture->GetWidth(),
                        m_CheckerboardTexture->GetHeight(),
                        m_CheckerboardTexture->GetRendererID());
            }
        }
    }
}

