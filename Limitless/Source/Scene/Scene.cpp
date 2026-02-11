#include "Scene/Scene.h"
#include "Graphics/Camera/Camera.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/RenderCommand.h"
#include "Graphics/Renderer.h"
#include "Graphics/Renderer2D.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Limitless
{
    Scene::Scene() = default;

    entt::entity Scene::CreateEntity(const std::string& name)
    {
        entt::entity entity = m_Registry.create();
        m_Registry.emplace<TagComponent>(entity, TagComponent{ name });
        return entity;
    }

    void Scene::DestroyEntity(entt::entity entity)
    {
        m_Registry.destroy(entity);
    }

    bool Scene::IsValid(entt::entity entity) const
    {
        return m_Registry.valid(entity);
    }

    void SceneRenderer::Render(Scene& scene, const Camera& camera)
    {
        Renderer2D::BeginScene(camera);

        auto view = scene.GetRegistry().view<TransformComponent, QuadRendererComponent>();
        for (entt::entity entity : view)
        {
            const auto& transform = view.get<TransformComponent>(entity);
            const auto& quad = view.get<QuadRendererComponent>(entity);

            glm::mat4 model = transform.GetLocalMatrix();
            // Apply quad size as scale (Renderer2D unit quad is -0.5..0.5).
            model = glm::scale(model, glm::vec3(quad.Size.x, quad.Size.y, 1.0f));
            Renderer2D::DrawQuad(model, quad.Color);
        }

        Renderer2D::EndScene();
    }

    void SceneRenderer::RenderToViewport(Scene& scene, const Camera& camera,
        const std::shared_ptr<Framebuffer>& framebuffer, uint32_t width, uint32_t height)
    {
        if (!framebuffer || width == 0 || height == 0)
            return;

        auto& renderer = Renderer::GetInstance();
        if (!renderer.IsInitialized())
            return;

        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(framebuffer));
        renderer.SubmitCommand(std::make_unique<SetViewportCommand>(0, 0, static_cast<int>(width), static_cast<int>(height)));

        ClearCommand::ClearFlags clearFlags;
        clearFlags.color = true;
        clearFlags.depth = true;
        clearFlags.stencil = false;
        renderer.SubmitCommand(std::make_unique<ClearCommand>(clearFlags, 0.12f, 0.12f, 0.14f, 1.0f));

        Render(scene, camera);

        renderer.SubmitCommand(std::make_unique<BindFramebufferCommand>(nullptr));
    }
}
