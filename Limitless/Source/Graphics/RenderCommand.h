#pragma once

#include "Core/Error.h"
#include "Core/Concurrency/LockFreeQueue.h"
#include "Core/Debug/Log.h"
#include "Graphics/GraphicsEnums.h"
#include <memory>
#include <functional>
#include <variant>
#include <vector>
#include <string>

namespace Limitless
{
    // Forward declarations
    class GraphicsContext;
    class Shader;
    class VertexArray;
    class IndexBuffer;
    class VertexBuffer;
    class Texture;
    class Framebuffer;

    // Render command types
    enum class RenderCommandType
    {
        Clear,
        SetViewport,
        SetScissor,
        BindShader,
        BindVertexArray,
        BindIndexBuffer,
        BindVertexBuffer,
        BindTexture,
        BindFramebuffer,
        DrawArrays,
        DrawIndexed,
        DrawInstanced,
        DrawIndexedInstanced,
        SetBlendMode,
        SetDepthTest,
        SetCullFace,
        SetPolygonMode,
        SetLineWidth,
        SetPointSize,
        PushDebugGroup,
        PopDebugGroup,
        InsertDebugMarker,
        Custom
    };

    // Render command priority levels
    enum class RenderCommandPriority
    {
        Low = 0,
        Normal = 1,
        High = 2,
        Critical = 3
    };

    // Base render command interface
    class RenderCommand
    {
    public:
        virtual ~RenderCommand() = default;

        // Execute the render command
        virtual void Execute(GraphicsContext* context) = 0;
        
        // Get command type
        virtual RenderCommandType GetType() const = 0;
        
        // Get command priority
        virtual RenderCommandPriority GetPriority() const { return RenderCommandPriority::Normal; }
        
        // Get command name for debugging
        virtual std::string GetName() const = 0;
        
        // Check if command can be batched with others
        virtual bool CanBatch() const { return true; }
        
        // Get estimated execution cost (for scheduling)
        virtual uint32_t GetEstimatedCost() const { return 1; }
    };

    // Clear command
    class ClearCommand : public RenderCommand
    {
    public:
        struct ClearFlags
        {
            bool color : 1 = true;
            bool depth : 1 = true;
            bool stencil : 1 = false;
        };

        ClearCommand(ClearFlags flags, float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::Clear; }
        std::string GetName() const override { return "Clear"; }

    private:
        ClearFlags m_Flags;
        float m_ClearColor[4];
    };

    // Set viewport command
    class SetViewportCommand : public RenderCommand
    {
    public:
        SetViewportCommand(int x, int y, int width, int height);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetViewport; }
        std::string GetName() const override { return "SetViewport"; }

    private:
        int m_X, m_Y, m_Width, m_Height;
    };

    // Set scissor command
    class SetScissorCommand : public RenderCommand
    {
    public:
        SetScissorCommand(int x, int y, int width, int height, bool enable = true);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetScissor; }
        std::string GetName() const override { return "SetScissor"; }

    private:
        int m_X, m_Y, m_Width, m_Height;
        bool m_Enable;
    };

    // Bind shader command
    class BindShaderCommand : public RenderCommand
    {
    public:
        explicit BindShaderCommand(std::shared_ptr<Shader> shader);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::BindShader; }
        std::string GetName() const override { return "BindShader"; }

    private:
        std::shared_ptr<Shader> m_Shader;
    };

    // Bind vertex array command
    class BindVertexArrayCommand : public RenderCommand
    {
    public:
        explicit BindVertexArrayCommand(std::shared_ptr<VertexArray> vertexArray);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::BindVertexArray; }
        std::string GetName() const override { return "BindVertexArray"; }

    private:
        std::shared_ptr<VertexArray> m_VertexArray;
    };

    // Bind index buffer command
    class BindIndexBufferCommand : public RenderCommand
    {
    public:
        explicit BindIndexBufferCommand(std::shared_ptr<IndexBuffer> indexBuffer);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::BindIndexBuffer; }
        std::string GetName() const override { return "BindIndexBuffer"; }

    private:
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };

    // Bind vertex buffer command
    class BindVertexBufferCommand : public RenderCommand
    {
    public:
        explicit BindVertexBufferCommand(std::shared_ptr<VertexBuffer> vertexBuffer);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::BindVertexBuffer; }
        std::string GetName() const override { return "BindVertexBuffer"; }

    private:
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
    };

    // Bind texture command
    class BindTextureCommand : public RenderCommand
    {
    public:
        BindTextureCommand(std::shared_ptr<Texture> texture, uint32_t slot = 0);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::BindTexture; }
        std::string GetName() const override { return "BindTexture"; }

    private:
        std::shared_ptr<Texture> m_Texture;
        uint32_t m_Slot;
    };

    // Bind framebuffer command
    class BindFramebufferCommand : public RenderCommand
    {
    public:
        explicit BindFramebufferCommand(std::shared_ptr<Framebuffer> framebuffer);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::BindFramebuffer; }
        std::string GetName() const override { return "BindFramebuffer"; }

    private:
        std::shared_ptr<Framebuffer> m_Framebuffer;
    };

    // Draw arrays command
    class DrawArraysCommand : public RenderCommand
    {
    public:
        DrawArraysCommand(DrawMode mode, int first, uint32_t count);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::DrawArrays; }
        std::string GetName() const override { return "DrawArrays"; }
        uint32_t GetEstimatedCost() const override { return m_Count; }

    private:
        DrawMode m_Mode;
        int m_First;
        uint32_t m_Count;
    };

    // Draw indexed command
    class DrawIndexedCommand : public RenderCommand
    {
    public:
        DrawIndexedCommand(DrawMode mode, uint32_t count, IndexType indexType, void* indices = nullptr, int baseVertex = 0);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::DrawIndexed; }
        std::string GetName() const override { return "DrawIndexed"; }
        uint32_t GetEstimatedCost() const override { return m_Count; }

    private:
        DrawMode m_Mode;
        uint32_t m_Count;
        IndexType m_IndexType;
        void* m_Indices;
        int m_BaseVertex;
    };

    // Draw instanced command
    class DrawInstancedCommand : public RenderCommand
    {
    public:
        DrawInstancedCommand(DrawMode mode, int first, uint32_t count, uint32_t instanceCount);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::DrawInstanced; }
        std::string GetName() const override { return "DrawInstanced"; }
        uint32_t GetEstimatedCost() const override { return m_Count * m_InstanceCount; }

    private:
        DrawMode m_Mode;
        int m_First;
        uint32_t m_Count;
        uint32_t m_InstanceCount;
    };

    // Draw indexed instanced command
    class DrawIndexedInstancedCommand : public RenderCommand
    {
    public:
        DrawIndexedInstancedCommand(DrawMode mode, uint32_t count, IndexType indexType, 
                                   void* indices, uint32_t instanceCount, int baseVertex = 0);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::DrawIndexedInstanced; }
        std::string GetName() const override { return "DrawIndexedInstanced"; }
        uint32_t GetEstimatedCost() const override { return m_Count * m_InstanceCount; }

    private:
        DrawMode m_Mode;
        uint32_t m_Count;
        IndexType m_IndexType;
        void* m_Indices;
        uint32_t m_InstanceCount;
        int m_BaseVertex;
    };

    // Set blend mode command
    class SetBlendModeCommand : public RenderCommand
    {
    public:
        SetBlendModeCommand(BlendFactor srcFactor, BlendFactor dstFactor, bool enable = true);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetBlendMode; }
        std::string GetName() const override { return "SetBlendMode"; }

    private:
        BlendFactor m_SrcFactor;
        BlendFactor m_DstFactor;
        bool m_Enable;
    };

    // Set depth test command
    class SetDepthTestCommand : public RenderCommand
    {
    public:
        SetDepthTestCommand(bool enable, DepthTestFunc func = DepthTestFunc::LessEqual);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetDepthTest; }
        std::string GetName() const override { return "SetDepthTest"; }

    private:
        bool m_Enable;
        DepthTestFunc m_Func;
    };

    // Set cull face command
    class SetCullFaceCommand : public RenderCommand
    {
    public:
        SetCullFaceCommand(bool enable, CullFace face = CullFace::Back);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetCullFace; }
        std::string GetName() const override { return "SetCullFace"; }

    private:
        bool m_Enable;
        CullFace m_Face;
    };

    // Set polygon mode command
    class SetPolygonModeCommand : public RenderCommand
    {
    public:
        SetPolygonModeCommand(PolygonFace face, PolygonMode mode);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetPolygonMode; }
        std::string GetName() const override { return "SetPolygonMode"; }

    private:
        PolygonFace m_Face;
        PolygonMode m_Mode;
    };

    // Set line width command
    class SetLineWidthCommand : public RenderCommand
    {
    public:
        explicit SetLineWidthCommand(float width);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetLineWidth; }
        std::string GetName() const override { return "SetLineWidth"; }

    private:
        float m_Width;
    };

    // Set point size command
    class SetPointSizeCommand : public RenderCommand
    {
    public:
        explicit SetPointSizeCommand(float size);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::SetPointSize; }
        std::string GetName() const override { return "SetPointSize"; }

    private:
        float m_Size;
    };

    // Debug group command
    class PushDebugGroupCommand : public RenderCommand
    {
    public:
        explicit PushDebugGroupCommand(const std::string& name);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::PushDebugGroup; }
        std::string GetName() const override { return "PushDebugGroup"; }

    private:
        std::string m_GroupName;
    };

    // Pop debug group command
    class PopDebugGroupCommand : public RenderCommand
    {
    public:
        PopDebugGroupCommand() = default;
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::PopDebugGroup; }
        std::string GetName() const override { return "PopDebugGroup"; }
    };

    // Insert debug marker command
    class InsertDebugMarkerCommand : public RenderCommand
    {
    public:
        explicit InsertDebugMarkerCommand(const std::string& name);
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::InsertDebugMarker; }
        std::string GetName() const override { return "InsertDebugMarker"; }

    private:
        std::string m_MarkerName;
    };

    // Custom command for user-defined operations
    class CustomCommand : public RenderCommand
    {
    public:
        using CustomFunction = std::function<void(GraphicsContext*)>;
        
        CustomCommand(CustomFunction func, const std::string& name = "Custom");
        
        void Execute(GraphicsContext* context) override;
        RenderCommandType GetType() const override { return RenderCommandType::Custom; }
        std::string GetName() const override { return m_Name; }
        bool CanBatch() const override { return false; }

    private:
        CustomFunction m_Function;
        std::string m_Name;
    };

} // namespace Limitless 