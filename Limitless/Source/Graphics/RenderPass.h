#pragma once

#include "Graphics/Framebuffer.h"
#include "Graphics/RenderTypes.h"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Limitless
{
    class Renderer;

    struct RenderPassColorAttachmentDescriptor
    {
        RenderLoadAction LoadAction = RenderLoadAction::Load;
        RenderStoreAction StoreAction = RenderStoreAction::Store;
        glm::vec4 ClearColor = glm::vec4(0.0f);
    };

    struct RenderPassDepthStencilAttachmentDescriptor
    {
        RenderLoadAction DepthLoadAction = RenderLoadAction::Load;
        RenderStoreAction DepthStoreAction = RenderStoreAction::Store;
        RenderLoadAction StencilLoadAction = RenderLoadAction::DontCare;
        RenderStoreAction StencilStoreAction = RenderStoreAction::DontCare;
        float ClearDepth = 1.0f;
        uint32_t ClearStencil = 0;
    };

    struct RenderPassDescriptor
    {
        std::string DebugName;
        std::shared_ptr<Framebuffer> TargetFramebuffer;
        RenderViewport Viewport{};
        std::optional<RenderScissorRect> ScissorRect{};
        std::vector<RenderPassColorAttachmentDescriptor> ColorAttachments{};
        std::optional<RenderPassDepthStencilAttachmentDescriptor> DepthStencilAttachment{};
    };

    class RenderPass
    {
    public:
        static void Begin(Renderer& renderer, const RenderPassDescriptor& descriptor);
        static void End(Renderer& renderer, const RenderPassDescriptor& descriptor);
    };
}
