#include "Graphics/RenderCommand.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/Shader.h"
#include "Graphics/VertexArray.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/NativeRenderHandles.h"
#include "Graphics/RenderPipeline.h"
#include "Core/Error.h"
#include "Core/Debug/Log.h"

// OpenGL includes
#ifdef LT_USE_GLAD
#include <glad/glad.h>
#endif

#include <SDL3/SDL.h>
#include <cstring>
#include <type_traits>
#include <unordered_map>

namespace {
    // Check for OpenGL errors
    void CheckOpenGLError(const char* operation) {
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            LT_CORE_ERROR("OpenGL error in {}: 0x{:x}", operation, error);
        }
    }

    // GL_KHR_debug / GL 4.3 debug group functions. Loaded at runtime when available.
    using PFNGLPUSHDEBUGGROUPPROC = void (*)(GLenum source, GLuint id, GLsizei length, const GLchar* message);
    using PFNGLPOPDEBUGGROUPPROC = void (*)();
    using PFNGLINSERTDEBUGMARKERPROC = void (*)(GLenum source, GLuint id, GLsizei length, const GLchar* message);

    struct OpenGLDebugFunctionState
    {
        PFNGLPUSHDEBUGGROUPPROC PushDebugGroup = nullptr;
        PFNGLPOPDEBUGGROUPPROC PopDebugGroup = nullptr;
        PFNGLINSERTDEBUGMARKERPROC InsertDebugMarker = nullptr;
        bool DebugFunctionsLoaded = false;
    };

    OpenGLDebugFunctionState& GetOpenGLDebugFunctionState()
    {
        static OpenGLDebugFunctionState state{};
        return state;
    }

    // GL_KHR_debug constants (may not be in GLAD 3.3 headers)
    static constexpr GLenum kGLDebugSourceApplication = 0x824A;

    static void LoadDebugFunctions()
    {
        auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.DebugFunctionsLoaded)
            return;
        debugState.DebugFunctionsLoaded = true;

        debugState.PushDebugGroup = reinterpret_cast<PFNGLPUSHDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPushDebugGroup"));
        if (!debugState.PushDebugGroup)
            debugState.PushDebugGroup = reinterpret_cast<PFNGLPUSHDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPushDebugGroupKHR"));
        debugState.PopDebugGroup = reinterpret_cast<PFNGLPOPDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPopDebugGroup"));
        if (!debugState.PopDebugGroup)
            debugState.PopDebugGroup = reinterpret_cast<PFNGLPOPDEBUGGROUPPROC>(SDL_GL_GetProcAddress("glPopDebugGroupKHR"));
        debugState.InsertDebugMarker = reinterpret_cast<PFNGLINSERTDEBUGMARKERPROC>(SDL_GL_GetProcAddress("glInsertDebugMarker"));
        if (!debugState.InsertDebugMarker)
            debugState.InsertDebugMarker = reinterpret_cast<PFNGLINSERTDEBUGMARKERPROC>(SDL_GL_GetProcAddress("glInsertDebugMarkerKHR"));
    }
}

namespace Limitless
{
    namespace
    {
        GLenum ToOpenGLBlendFactor(BlendFactor factor)
        {
            switch (factor)
            {
                case BlendFactor::Zero: return GL_ZERO;
                case BlendFactor::One: return GL_ONE;
                case BlendFactor::SrcColor: return GL_SRC_COLOR;
                case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
                case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
                case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
                case BlendFactor::DstAlpha: return GL_DST_ALPHA;
                case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
                case BlendFactor::DstColor: return GL_DST_COLOR;
                case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
                case BlendFactor::SrcAlphaSaturate: return GL_SRC_ALPHA_SATURATE;
                default: return GL_ONE;
            }
        }

        GLenum ToOpenGLDepthFunc(DepthTestFunc func)
        {
            switch (func)
            {
                case DepthTestFunc::Never: return GL_NEVER;
                case DepthTestFunc::Less: return GL_LESS;
                case DepthTestFunc::Equal: return GL_EQUAL;
                case DepthTestFunc::LessEqual: return GL_LEQUAL;
                case DepthTestFunc::Greater: return GL_GREATER;
                case DepthTestFunc::NotEqual: return GL_NOTEQUAL;
                case DepthTestFunc::GreaterEqual: return GL_GEQUAL;
                case DepthTestFunc::Always: return GL_ALWAYS;
                default: return GL_LEQUAL;
            }
        }

        GLenum ToOpenGLCullFace(CullFace face)
        {
            switch (face)
            {
                case CullFace::Front: return GL_FRONT;
                case CullFace::Back: return GL_BACK;
                case CullFace::FrontAndBack: return GL_FRONT_AND_BACK;
                default: return GL_BACK;
            }
        }

        GLenum ToOpenGLPolygonMode(PolygonMode mode)
        {
            switch (mode)
            {
                case PolygonMode::Point: return GL_POINT;
                case PolygonMode::Line: return GL_LINE;
                case PolygonMode::Fill: return GL_FILL;
                default: return GL_FILL;
            }
        }

        GLenum ToOpenGLPolygonFace(PolygonFace face)
        {
            switch (face)
            {
                case PolygonFace::Front: return GL_FRONT;
                case PolygonFace::Back: return GL_BACK;
                case PolygonFace::FrontAndBack: return GL_FRONT_AND_BACK;
                default: return GL_FRONT_AND_BACK;
            }
        }

        GLenum ToOpenGLDrawMode(DrawMode mode)
        {
            switch (mode)
            {
                case DrawMode::Points: return GL_POINTS;
                case DrawMode::Lines: return GL_LINES;
                case DrawMode::LineLoop: return GL_LINE_LOOP;
                case DrawMode::LineStrip: return GL_LINE_STRIP;
                case DrawMode::Triangles: return GL_TRIANGLES;
                case DrawMode::TriangleStrip: return GL_TRIANGLE_STRIP;
                case DrawMode::TriangleFan: return GL_TRIANGLE_FAN;
                default: return GL_TRIANGLES;
            }
        }

        GLenum ToOpenGLIndexType(IndexType type)
        {
            switch (type)
            {
                case IndexType::UnsignedByte: return GL_UNSIGNED_BYTE;
                case IndexType::UnsignedShort: return GL_UNSIGNED_SHORT;
                case IndexType::UnsignedInt: return GL_UNSIGNED_INT;
                default: return GL_UNSIGNED_INT;
            }
        }

        // Render-thread OpenGL state tracker.
        // Commands that mutate tracked state MUST update this cache.
        // CustomCommand MUST invalidate it (unknown GL calls).
        struct OpenGLStateCache
        {
            GLuint Program = 0;
            GLuint VertexArray = 0;

            uint32_t ActiveTextureUnit = 0;
            std::array<GLuint, 32> BoundTexture2D{};

            void InvalidateAll()
            {
                Program = 0;
                VertexArray = 0;
                ActiveTextureUnit = 0;
                BoundTexture2D.fill(0);
            }

            /// Invalidates texture binding cache for slots [0, count).
            /// Use before binding when external code (e.g. ImGui) may have changed GL state.
            /// Prevents stale cache from skipping glBindTexture, which causes wrong-texture sampling
            /// (e.g. glyph artifacts when color-only quads should use white texture).
            void InvalidateTextureSlots(uint32_t count)
            {
                constexpr GLuint kInvalidId = static_cast<GLuint>(-1);
                for (uint32_t i = 0; i < count && i < BoundTexture2D.size(); ++i)
                    BoundTexture2D[i] = kInvalidId;
            }

            void UseProgram(GLuint program)
            {
                if (Program != program)
                {
                    glUseProgram(program);
                    Program = program;
                }
            }

            void BindVertexArray(GLuint vao)
            {
                if (VertexArray != vao)
                {
                    glBindVertexArray(vao);
                    VertexArray = vao;
                }
            }

            void SetActiveTextureUnit(uint32_t unit)
            {
                if (ActiveTextureUnit != unit)
                {
                    glActiveTexture(GL_TEXTURE0 + unit);
                    ActiveTextureUnit = unit;
                }
            }

            void BindTexture2D(uint32_t unit, GLuint textureId)
            {
                SetActiveTextureUnit(unit);
                if (unit < BoundTexture2D.size())
                {
                    if (BoundTexture2D[unit] != textureId)
                    {
                        glBindTexture(GL_TEXTURE_2D, textureId);
                        BoundTexture2D[unit] = textureId;
                    }
                    return;
                }

                // Fallback: no caching for high slots.
                glBindTexture(GL_TEXTURE_2D, textureId);
            }
        };

        struct Renderer2DUniformCache
        {
            GLuint Program = 0;
            GLint ViewProjectionLocation = -2; // -2 = unknown, -1 = not found
            GLint ModelLocation = -2;          // -2 = unknown, -1 = not found

            glm::mat4 LastViewProjection{1.0f};
            bool HasViewProjection = false;

            void OnProgramBound(GLuint program)
            {
                if (Program != program)
                {
                    Program = program;
                    ViewProjectionLocation = -2;
                    ModelLocation = -2;
                    HasViewProjection = false;
                }
            }
        };

        struct OpenGLRenderCommandRuntimeState
        {
            OpenGLStateCache GLState;
            Renderer2DUniformCache Renderer2DUniforms;
            std::unordered_map<GLuint, std::unordered_map<std::string, GLint>> UniformLocations;
        };

        OpenGLRenderCommandRuntimeState& GetOpenGLRenderCommandRuntimeState()
        {
            static OpenGLRenderCommandRuntimeState state{};
            return state;
        }

        GLint GetCachedUniformLocationOpenGL(GLuint program, const std::string& name)
        {
            if (program == 0 || name.empty())
                return -1;

            auto& uniformLocations = GetOpenGLRenderCommandRuntimeState().UniformLocations;
            auto& programLocations = uniformLocations[program];
            const auto found = programLocations.find(name);
            if (found != programLocations.end())
                return found->second;

            const GLint location = glGetUniformLocation(program, name.c_str());
            programLocations.emplace(name, location);
            return location;
        }

        GLuint BindShaderProgramOpenGL(const std::shared_ptr<Shader>& shader)
        {
            if (!shader)
                return 0;

            auto& runtimeState = GetOpenGLRenderCommandRuntimeState();
            GLuint program = static_cast<GLuint>(GetShaderNativeHandle(shader));
            if (program != 0)
            {
                runtimeState.GLState.UseProgram(program);
                return program;
            }

            shader->Bind();

            GLint boundProgram = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &boundProgram);
            program = static_cast<GLuint>(boundProgram);
            runtimeState.GLState.Program = program;
            return program;
        }

        void BindVertexArrayOpenGL(const std::shared_ptr<VertexArray>& vertexArray)
        {
            if (!vertexArray)
                return;

            auto& runtimeState = GetOpenGLRenderCommandRuntimeState();
            const GLuint vao = static_cast<GLuint>(GetVertexArrayNativeHandle(vertexArray));
            if (vao != 0)
            {
                runtimeState.GLState.BindVertexArray(vao);
            }
            else
            {
                vertexArray->Bind();
                runtimeState.GLState.VertexArray = 0;
            }

            const auto& indexBuffer = vertexArray->GetIndexBuffer();
            if (indexBuffer)
                indexBuffer->Bind();
        }

        ClearCommand::ClearFlags BuildRenderPassClearFlags(const RenderPassDescriptor& descriptor)
        {
            ClearCommand::ClearFlags flags{};
            flags.color = false;
            flags.depth = false;
            flags.stencil = false;

            for (const auto& colorAttachment : descriptor.ColorAttachments)
            {
                if (colorAttachment.LoadAction == RenderLoadAction::Clear)
                {
                    flags.color = true;
                    break;
                }
            }

            if (descriptor.DepthStencilAttachment.has_value())
            {
                flags.depth = descriptor.DepthStencilAttachment->DepthLoadAction == RenderLoadAction::Clear;
                flags.stencil = descriptor.DepthStencilAttachment->StencilLoadAction == RenderLoadAction::Clear;
            }

            return flags;
        }

        glm::vec4 GetRenderPassClearColor(const RenderPassDescriptor& descriptor)
        {
            for (const auto& colorAttachment : descriptor.ColorAttachments)
            {
                if (colorAttachment.LoadAction == RenderLoadAction::Clear)
                    return colorAttachment.ClearColor;
            }

            return glm::vec4(0.0f);
        }
    }

    void ApplyNamedRenderParameterOpenGL(const std::shared_ptr<Shader>& shader, GLuint program, const RenderParameterBinding& parameter)
    {
        if (!shader || parameter.Name.empty())
            return;

        if (program == 0)
            return;

        const GLint location = GetCachedUniformLocationOpenGL(program, parameter.Name);
        if (location == -1)
            return;

        std::visit([location](const auto& value)
        {
            using ValueT = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<ValueT, int32_t>)
            {
                glUniform1i(location, static_cast<GLint>(value));
            }
            else if constexpr (std::is_same_v<ValueT, float>)
            {
                glUniform1f(location, value);
            }
            else if constexpr (std::is_same_v<ValueT, glm::vec2>)
            {
                glUniform2fv(location, 1, &value[0]);
            }
            else if constexpr (std::is_same_v<ValueT, glm::vec3>)
            {
                glUniform3fv(location, 1, &value[0]);
            }
            else if constexpr (std::is_same_v<ValueT, glm::vec4>)
            {
                glUniform4fv(location, 1, &value[0]);
            }
            else if constexpr (std::is_same_v<ValueT, glm::mat4>)
            {
                glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
            }
            else if constexpr (std::is_same_v<ValueT, std::vector<int32_t>>)
            {
                if (!value.empty())
                {
                    std::vector<GLint> converted(value.begin(), value.end());
                    glUniform1iv(location, static_cast<GLsizei>(converted.size()), converted.data());
                }
            }
            else if constexpr (std::is_same_v<ValueT, std::vector<glm::vec2>>)
            {
                if (!value.empty())
                    glUniform2fv(location, static_cast<GLsizei>(value.size()), &value[0][0]);
            }
            else if constexpr (std::is_same_v<ValueT, std::vector<glm::vec4>>)
            {
                if (!value.empty())
                    glUniform4fv(location, static_cast<GLsizei>(value.size()), &value[0][0]);
            }
        }, parameter.Value);
    }

    // ClearCommand Execute implementation
    void ClearCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        GLbitfield clearMask = 0;
        
        if (m_Flags.color)
        {
            glClearColor(m_ClearColor[0], m_ClearColor[1], m_ClearColor[2], m_ClearColor[3]);
            clearMask |= GL_COLOR_BUFFER_BIT;
            CheckOpenGLError("glClearColor");
        }
        
        if (m_Flags.depth)
        {
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }
        
        if (m_Flags.stencil)
        {
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }
        
        if (clearMask != 0)
        {
            glClear(clearMask);
            CheckOpenGLError("glClear");
        }
    }

    void SetDrawColorAttachmentsCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        while (glGetError() != GL_NO_ERROR)
            ;

        GLint drawFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        if (drawFramebuffer == 0)
        {
            glDrawBuffer(GL_BACK);
        }
        else if (!m_Attachments.empty())
        {
            std::vector<GLenum> drawBuffers;
            drawBuffers.reserve(m_Attachments.size());
            for (uint32_t attachment : m_Attachments)
            {
                drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + attachment);
            }
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        }
        else
        {
            const GLenum fallbackBuffers[] = { GL_COLOR_ATTACHMENT0 };
            glDrawBuffers(1, fallbackBuffers);
        }

        GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            LT_CORE_ERROR("SetDrawColorAttachmentsCommand failed (fbo={}, error=0x{:x}); applying fallback draw buffer",
                          drawFramebuffer,
                          static_cast<uint32_t>(error));
            while (glGetError() != GL_NO_ERROR)
                ;

            if (drawFramebuffer == 0)
            {
                glDrawBuffer(GL_BACK);
            }
            else
            {
                const GLenum fallbackBuffers[] = { GL_COLOR_ATTACHMENT0 };
                glDrawBuffers(1, fallbackBuffers);
            }
        }
    }

    void ClearColorAttachmentCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        while (glGetError() != GL_NO_ERROR)
            ;

        const GLfloat clearValue[4] = { m_ClearValue.r, m_ClearValue.g, m_ClearValue.b, m_ClearValue.a };
        glClearBufferfv(GL_COLOR, static_cast<GLint>(m_AttachmentIndex), clearValue);
        if (const GLenum error = glGetError(); error != GL_NO_ERROR)
        {
            LT_CORE_ERROR("ClearColorAttachmentCommand failed (attachment={}, error=0x{:x})",
                          m_AttachmentIndex,
                          static_cast<uint32_t>(error));
        }
    }

    void BindRenderPipelineCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_Pipeline)
        {
            LT_CORE_WARN("BindRenderPipelineCommand: pipeline was null (no-op)");
            return;
        }

        const auto& descriptor = m_Pipeline->GetDescriptor();
        if (descriptor.ShaderProgram)
        {
            BindShaderProgramOpenGL(descriptor.ShaderProgram);
        }

        if (descriptor.BlendState.Enabled)
        {
            glEnable(GL_BLEND);
            glBlendFunc(ToOpenGLBlendFactor(descriptor.BlendState.SourceColorFactor),
                        ToOpenGLBlendFactor(descriptor.BlendState.DestinationColorFactor));
        }
        else
        {
            glDisable(GL_BLEND);
        }
        CheckOpenGLError("BindRenderPipelineCommand::BlendState");

        if (descriptor.DepthStencilState.DepthTestEnabled)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(ToOpenGLDepthFunc(descriptor.DepthStencilState.DepthCompare));
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(descriptor.DepthStencilState.DepthWriteEnabled ? GL_TRUE : GL_FALSE);
        CheckOpenGLError("BindRenderPipelineCommand::DepthState");

        if (descriptor.RasterState.CullEnabled)
        {
            glEnable(GL_CULL_FACE);
            glCullFace(ToOpenGLCullFace(descriptor.RasterState.CullMode));
        }
        else
        {
            glDisable(GL_CULL_FACE);
        }
        CheckOpenGLError("BindRenderPipelineCommand::CullState");

        glPolygonMode(GL_FRONT_AND_BACK, ToOpenGLPolygonMode(descriptor.RasterState.FillMode));
        CheckOpenGLError("BindRenderPipelineCommand::PolygonMode");
    }

    void SetVertexBufferDataCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_VertexBuffer)
        {
            LT_CORE_WARN("SetVertexBufferDataCommand: vertex buffer was null (no-op)");
            return;
        }

        if (m_DataPtr == nullptr || m_SizeBytes == 0)
        {
            // Allow no-op uploads (useful for defensive code paths).
            return;
        }

        m_VertexBuffer->SetData(m_DataPtr, m_SizeBytes);
    }

    void Renderer2DFlushCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_KeepAlive.VertexArrayHandle || !m_KeepAlive.VertexBufferHandle || !m_KeepAlive.ShaderProgramHandle)
        {
            LT_CORE_WARN("Renderer2DFlushCommand: missing resources (VAO/VBO/Shader), skipping");
            return;
        }

        if (m_VertexBytes == nullptr || m_VertexByteCount == 0 || m_IndexCount == 0)
        {
            // Allow no-op flushes (defensive).
            return;
        }

        // Upload vertices (streaming).
        m_KeepAlive.VertexBufferHandle->SetData(m_VertexBytes, m_VertexByteCount);

        // Bind shader + uniforms (OpenGL fast path when possible).
        GLuint program = BindShaderProgramOpenGL(m_KeepAlive.ShaderProgramHandle);
        auto& renderer2DUniforms = GetOpenGLRenderCommandRuntimeState().Renderer2DUniforms;
        renderer2DUniforms.OnProgramBound(program);

        if (program != 0)
        {
            if (renderer2DUniforms.ViewProjectionLocation == -2)
            {
                renderer2DUniforms.ViewProjectionLocation = GetCachedUniformLocationOpenGL(program, "u_ViewProjection");
            }

            if (renderer2DUniforms.ViewProjectionLocation != -1)
            {
                if (!renderer2DUniforms.HasViewProjection ||
                    std::memcmp(&renderer2DUniforms.LastViewProjection[0][0], &m_ViewProjection[0][0], sizeof(glm::mat4)) != 0)
                {
                    glUniformMatrix4fv(renderer2DUniforms.ViewProjectionLocation, 1, GL_FALSE, &m_ViewProjection[0][0]);
                    renderer2DUniforms.LastViewProjection = m_ViewProjection;
                    renderer2DUniforms.HasViewProjection = true;
                }
            }

            if (renderer2DUniforms.ModelLocation == -2)
            {
                renderer2DUniforms.ModelLocation = GetCachedUniformLocationOpenGL(program, "u_Model");
            }

            if (renderer2DUniforms.ModelLocation != -1)
            {
                static constexpr glm::mat4 kIdentity(1.0f);
                glUniformMatrix4fv(renderer2DUniforms.ModelLocation, 1, GL_FALSE, &kIdentity[0][0]);
            }
        }

        // Bind textures (multi-texture batching) using cached renderer IDs.
        // Invalidate texture cache first: ImGui (or other external code) may have bound its font
        // to texture unit 0, leaving our cache stale. Skipping the bind would cause color-only
        // quads to sample the wrong texture (glyph artifacts).
        const uint32_t count = (m_TextureCount > kMaxTextureSlots) ? kMaxTextureSlots : m_TextureCount;
        GetOpenGLRenderCommandRuntimeState().GLState.InvalidateTextureSlots(count);
        for (uint32_t slot = 0; slot < count; ++slot)
        {
            GetOpenGLRenderCommandRuntimeState().GLState.BindTexture2D(slot, static_cast<GLuint>(m_TextureRendererIds[slot]));
        }

        // Bind geometry and issue draw.
        BindVertexArrayOpenGL(m_KeepAlive.VertexArrayHandle);

        // Safety: in core profile, indexed drawing requires VAO + EBO.
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
        GLint boundEBO = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEBO);

        if (boundVAO == 0 || boundEBO == 0)
        {
            LT_CORE_ERROR("Renderer2DFlushCommand: invalid VAO/EBO binding (VAO={}, EBO={}), skipping draw", boundVAO, boundEBO);
            return;
        }

        glDrawElements(ToOpenGLDrawMode(DrawMode::Triangles), static_cast<GLsizei>(m_IndexCount),
                       ToOpenGLIndexType(m_IndexType), nullptr);
        CheckOpenGLError("Renderer2DFlushCommand glDrawElements");
    }

    void SetTextureSpecificationCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Texture)
        {
            m_Texture->ApplySpecification(m_Specification);
        }
        else
        {
            LT_CORE_WARN("SetTextureSpecificationCommand: texture was null (no-op)");
        }
    }

    void BindFramebufferCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        while (glGetError() != GL_NO_ERROR)
            ;

        if (m_Framebuffer)
        {
            m_Framebuffer->Bind();
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        CheckOpenGLError("BindFramebufferCommand::Execute");
    }

    void BeginRenderPassCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        while (glGetError() != GL_NO_ERROR)
            ;

        if (m_Descriptor.TargetFramebuffer)
        {
            m_Descriptor.TargetFramebuffer->Bind();
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        CheckOpenGLError("BeginRenderPassCommand::BindFramebuffer");

        glViewport(m_Descriptor.Viewport.X,
                   m_Descriptor.Viewport.Y,
                   m_Descriptor.Viewport.Width,
                   m_Descriptor.Viewport.Height);
        CheckOpenGLError("BeginRenderPassCommand::Viewport");

        if (m_Descriptor.ScissorRect.has_value())
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(m_Descriptor.ScissorRect->X,
                      m_Descriptor.ScissorRect->Y,
                      m_Descriptor.ScissorRect->Width,
                      m_Descriptor.ScissorRect->Height);
            CheckOpenGLError("BeginRenderPassCommand::ScissorEnable");
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
        }

        const ClearCommand::ClearFlags clearFlags = BuildRenderPassClearFlags(m_Descriptor);
        if (clearFlags.color || clearFlags.depth || clearFlags.stencil)
        {
            GLbitfield clearMask = 0;

            if (clearFlags.color)
            {
                const glm::vec4 clearColor = GetRenderPassClearColor(m_Descriptor);
                glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
                clearMask |= GL_COLOR_BUFFER_BIT;
                CheckOpenGLError("BeginRenderPassCommand::ClearColor");
            }

            if (m_Descriptor.DepthStencilAttachment.has_value())
            {
                if (clearFlags.depth)
                {
                    glClearDepth(m_Descriptor.DepthStencilAttachment->ClearDepth);
                    clearMask |= GL_DEPTH_BUFFER_BIT;
                    CheckOpenGLError("BeginRenderPassCommand::ClearDepthValue");
                }

                if (clearFlags.stencil)
                {
                    glClearStencil(static_cast<GLint>(m_Descriptor.DepthStencilAttachment->ClearStencil));
                    clearMask |= GL_STENCIL_BUFFER_BIT;
                    CheckOpenGLError("BeginRenderPassCommand::ClearStencilValue");
                }
            }

            if (clearMask != 0)
            {
                glClear(clearMask);
                CheckOpenGLError("BeginRenderPassCommand::Clear");
            }
        }
    }

    void EndRenderPassCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Descriptor.ScissorRect.has_value())
            glDisable(GL_SCISSOR_TEST);
    }

    void ApplyRenderBindingsCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (!m_Shader)
        {
            LT_CORE_WARN("ApplyRenderBindingsCommand: missing shader (no-op)");
            return;
        }

        GLuint program = BindShaderProgramOpenGL(m_Shader);
        GetOpenGLRenderCommandRuntimeState().Renderer2DUniforms.OnProgramBound(program);

        if (m_VertexArray)
        {
            BindVertexArrayOpenGL(m_VertexArray);
        }

        for (const auto& textureBinding : m_Bindings.Textures)
        {
            const GLuint textureId = static_cast<GLuint>(GetTextureNativeHandle(textureBinding.TextureHandle));
            GetOpenGLRenderCommandRuntimeState().GLState.BindTexture2D(textureBinding.Slot, textureId);

            if (!textureBinding.SamplerName.empty())
            {
                RenderParameterBinding samplerBinding{};
                samplerBinding.Name = textureBinding.SamplerName;
                samplerBinding.Value = static_cast<int32_t>(textureBinding.Slot);
                ApplyNamedRenderParameterOpenGL(m_Shader, program, samplerBinding);
            }
        }

        for (const auto& parameter : m_Bindings.Parameters)
            ApplyNamedRenderParameterOpenGL(m_Shader, program, parameter);
    }

    // DrawArraysCommand Execute implementation
    void DrawArraysCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawArraysCommand: no VAO bound (skipping draw)");
            return;
        }

        glDrawArrays(ToOpenGLDrawMode(m_Mode), m_First, static_cast<GLsizei>(m_Count));
        CheckOpenGLError("glDrawArrays");
    }

    // DrawIndexedCommand Execute implementation
    void DrawIndexedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);

        GLint boundEBO = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEBO);

        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawIndexedCommand: no VAO bound (skipping draw)");
            return;
        }

        if (m_Indices == nullptr && boundEBO == 0)
        {
            LT_CORE_ERROR("DrawIndexedCommand: no index buffer bound and indices pointer is null (skipping draw)");
            return;
        }

        if (m_BaseVertex != 0)
        {
            glDrawElementsBaseVertex(ToOpenGLDrawMode(m_Mode), static_cast<GLsizei>(m_Count),
                                     ToOpenGLIndexType(m_IndexType), m_Indices, m_BaseVertex);
            CheckOpenGLError("glDrawElementsBaseVertex");
        }
        else
        {
            glDrawElements(ToOpenGLDrawMode(m_Mode), static_cast<GLsizei>(m_Count),
                           ToOpenGLIndexType(m_IndexType), m_Indices);
            CheckOpenGLError("glDrawElements");
        }
    }
    // DrawInstancedCommand Execute implementation
    void DrawInstancedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // Safety: OpenGL core profile requires a VAO to be bound for vertex specification.
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);
        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawInstancedCommand: no VAO bound (skipping draw)");
            return;
        }

        glDrawArraysInstanced(ToOpenGLDrawMode(m_Mode), m_First, static_cast<GLsizei>(m_Count),
                              static_cast<GLsizei>(m_InstanceCount));
        CheckOpenGLError("glDrawArraysInstanced");
    }

    // DrawIndexedInstancedCommand Execute implementation
    void DrawIndexedInstancedCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        // Safety: In core profile, indexed drawing requires a VAO and an element array buffer
        // unless the caller provides a valid client pointer (which we do not support here).
        GLint boundVAO = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVAO);

        GLint boundEBO = 0;
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &boundEBO);

        if (boundVAO == 0)
        {
            LT_CORE_ERROR("DrawIndexedInstancedCommand: no VAO bound (skipping draw)");
            return;
        }

        // If m_Indices is null, we expect an index buffer to be bound via the VAO state.
        if (m_Indices == nullptr && boundEBO == 0)
        {
            LT_CORE_ERROR("DrawIndexedInstancedCommand: no index buffer bound and indices pointer is null (skipping draw)");
            return;
        }

        if (m_BaseVertex != 0)
        {
            glDrawElementsInstancedBaseVertex(ToOpenGLDrawMode(m_Mode), static_cast<GLsizei>(m_Count),
                                              ToOpenGLIndexType(m_IndexType), m_Indices,
                                              static_cast<GLsizei>(m_InstanceCount), m_BaseVertex);
            CheckOpenGLError("glDrawElementsInstancedBaseVertex");
        }
        else
        {
            glDrawElementsInstanced(ToOpenGLDrawMode(m_Mode), static_cast<GLsizei>(m_Count),
                                    ToOpenGLIndexType(m_IndexType), m_Indices, static_cast<GLsizei>(m_InstanceCount));
            CheckOpenGLError("glDrawElementsInstanced");
        }
    }

    // SetBlendModeCommand Execute implementation
    void SetBlendModeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_BLEND);
            glBlendFunc(ToOpenGLBlendFactor(m_SrcFactor), ToOpenGLBlendFactor(m_DstFactor));
            CheckOpenGLError("glBlendFunc");
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    // SetDepthTestCommand Execute implementation
    void SetDepthTestCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(ToOpenGLDepthFunc(m_Func));
            CheckOpenGLError("glDepthFunc");
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
    }

    // SetCullFaceCommand Execute implementation
    void SetCullFaceCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Enable)
        {
            glEnable(GL_CULL_FACE);
            glCullFace(ToOpenGLCullFace(m_Face));
            CheckOpenGLError("glCullFace");
        }
        else
        {
            glDisable(GL_CULL_FACE);
        }
    }

    // SetPolygonModeCommand Execute implementation
    void SetPolygonModeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glPolygonMode(ToOpenGLPolygonFace(m_Face), ToOpenGLPolygonMode(m_Mode));
        CheckOpenGLError("glPolygonMode");
    }

    // SetLineWidthCommand Execute implementation
    void SetLineWidthCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glLineWidth(m_Width);
        CheckOpenGLError("glLineWidth");
    }

    // SetPointSizeCommand Execute implementation
    void SetPointSizeCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        glPointSize(m_Size);
        CheckOpenGLError("glPointSize");
    }

    // PushDebugGroupCommand Execute implementation
    void PushDebugGroupCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        LoadDebugFunctions();
        const auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.PushDebugGroup)
        {
            debugState.PushDebugGroup(kGLDebugSourceApplication, 0, static_cast<GLsizei>(m_GroupName.size()), m_GroupName.c_str());
        }
    }

    // PopDebugGroupCommand Execute implementation
    void PopDebugGroupCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        LoadDebugFunctions();
        const auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.PopDebugGroup)
        {
            debugState.PopDebugGroup();
        }
    }

    // InsertDebugMarkerCommand Execute implementation
    void InsertDebugMarkerCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        LoadDebugFunctions();
        const auto& debugState = GetOpenGLDebugFunctionState();
        if (debugState.InsertDebugMarker)
        {
            debugState.InsertDebugMarker(kGLDebugSourceApplication, 0, static_cast<GLsizei>(m_MarkerName.size()), m_MarkerName.c_str());
        }
    }

    // CustomCommand Execute implementation
    void CustomCommand::Execute(GraphicsContext* context)
    {
        if (!context)
        {
            LT_THROW_ERROR(ErrorCode::InvalidArgument, "Graphics context cannot be null");
        }

        if (m_Function)
        {
            m_Function(context);
        }

        // Custom code can mutate any OpenGL state. Invalidate cached state for correctness.
        GetOpenGLRenderCommandRuntimeState().GLState.InvalidateAll();
        GetOpenGLRenderCommandRuntimeState().Renderer2DUniforms.OnProgramBound(0);
        GetOpenGLRenderCommandRuntimeState().UniformLocations.clear();
        
        LT_CORE_DEBUG("CustomCommand: {}", m_Name);
    }

} // namespace Limitless 
