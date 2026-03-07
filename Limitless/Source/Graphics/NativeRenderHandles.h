#pragma once

#include <cstdint>
#include <memory>

namespace Limitless
{
    class Framebuffer;
    class IndexBuffer;
    class Shader;
    class Texture;
    class VertexArray;
    class VertexBuffer;

    uintptr_t GetFramebufferNativeHandle(const std::shared_ptr<Framebuffer>& framebuffer);
    uintptr_t GetFramebufferNativeHandle(const Framebuffer* framebuffer);
    uintptr_t GetIndexBufferNativeHandle(const std::shared_ptr<IndexBuffer>& indexBuffer);
    uintptr_t GetIndexBufferNativeHandle(const IndexBuffer* indexBuffer);
    uintptr_t GetShaderNativeHandle(const std::shared_ptr<Shader>& shader);
    uintptr_t GetShaderNativeHandle(const Shader* shader);
    uintptr_t GetTextureNativeHandle(const std::shared_ptr<Texture>& texture);
    uintptr_t GetTextureNativeHandle(const Texture* texture);
    uintptr_t GetVertexArrayNativeHandle(const std::shared_ptr<VertexArray>& vertexArray);
    uintptr_t GetVertexArrayNativeHandle(const VertexArray* vertexArray);
    uintptr_t GetVertexBufferNativeHandle(const std::shared_ptr<VertexBuffer>& vertexBuffer);
    uintptr_t GetVertexBufferNativeHandle(const VertexBuffer* vertexBuffer);
}
