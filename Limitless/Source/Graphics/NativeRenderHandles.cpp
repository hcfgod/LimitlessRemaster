#include "Graphics/NativeRenderHandles.h"

#include "Graphics/Buffer.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/Shader.h"
#include "Graphics/Texture.h"
#include "Graphics/VertexArray.h"

namespace Limitless
{
    uintptr_t GetFramebufferNativeHandle(const std::shared_ptr<Framebuffer>& framebuffer)
    {
        return GetFramebufferNativeHandle(framebuffer.get());
    }

    uintptr_t GetFramebufferNativeHandle(const Framebuffer* framebuffer)
    {
        if (!framebuffer)
            return 0;

        return framebuffer->GetNativeHandle();
    }

    uintptr_t GetIndexBufferNativeHandle(const std::shared_ptr<IndexBuffer>& indexBuffer)
    {
        return GetIndexBufferNativeHandle(indexBuffer.get());
    }

    uintptr_t GetIndexBufferNativeHandle(const IndexBuffer* indexBuffer)
    {
        if (!indexBuffer)
            return 0;

        return indexBuffer->GetNativeHandle();
    }

    uintptr_t GetShaderNativeHandle(const std::shared_ptr<Shader>& shader)
    {
        return GetShaderNativeHandle(shader.get());
    }

    uintptr_t GetShaderNativeHandle(const Shader* shader)
    {
        if (!shader)
            return 0;

        return shader->GetNativeHandle();
    }

    uintptr_t GetTextureNativeHandle(const std::shared_ptr<Texture>& texture)
    {
        return GetTextureNativeHandle(texture.get());
    }

    uintptr_t GetTextureNativeHandle(const Texture* texture)
    {
        if (!texture)
            return 0;

        return texture->GetNativeHandle();
    }

    uintptr_t GetVertexArrayNativeHandle(const std::shared_ptr<VertexArray>& vertexArray)
    {
        return GetVertexArrayNativeHandle(vertexArray.get());
    }

    uintptr_t GetVertexArrayNativeHandle(const VertexArray* vertexArray)
    {
        if (!vertexArray)
            return 0;

        return vertexArray->GetNativeHandle();
    }

    uintptr_t GetVertexBufferNativeHandle(const std::shared_ptr<VertexBuffer>& vertexBuffer)
    {
        return GetVertexBufferNativeHandle(vertexBuffer.get());
    }

    uintptr_t GetVertexBufferNativeHandle(const VertexBuffer* vertexBuffer)
    {
        if (!vertexBuffer)
            return 0;

        return vertexBuffer->GetNativeHandle();
    }
}
