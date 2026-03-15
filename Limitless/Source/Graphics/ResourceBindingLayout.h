#pragma once

#include "Graphics/ShaderDescriptor.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Limitless
{
    class Texture;

    /// Describes one slot in a resource binding layout.
    ///
    /// A layout is typically derived from ShaderReflection at shader creation
    /// time and describes *what* the shader expects (type, binding index, stage
    /// visibility). A ResourceBindingSet then fills in the *actual* resources.
    struct ResourceBindingEntry
    {
        enum class Type : uint8_t
        {
            UniformBuffer,
            Texture,
            Sampler,
            CombinedImageSampler,
            StorageBuffer
        };

        std::string Name;
        Type        BindingType = Type::UniformBuffer;
        uint32_t    Binding     = 0;
        uint32_t    Set         = 0;
        ShaderStage StageVisibility = ShaderStage::Vertex;
    };

    /// A layout object built from shader reflection that describes
    /// all resource binding points a shader expects.
    struct ResourceBindingLayout
    {
        std::vector<ResourceBindingEntry> Entries;

        /// Build a ResourceBindingLayout from ShaderReflection data.
        static ResourceBindingLayout FromReflection(const ShaderReflection& reflection);
    };

    /// A concrete resource binding: associates a binding index with an actual
    /// resource handle (buffer pointer, texture, etc.).
    struct ResourceBinding
    {
        uint32_t Binding = 0;
        uint32_t Set     = 0;

        // Only one of these is valid per binding (determined by layout entry type).
        std::shared_ptr<Texture> TextureResource;
        const void*              BufferData   = nullptr;
        uint32_t                 BufferSize   = 0;
    };

    /// A filled-in set of resource bindings ready to be submitted to the GPU.
    ///
    /// The layout pointer describes the expected shape; the Bindings vector
    /// provides the actual resources.
    struct ResourceBindingSet
    {
        const ResourceBindingLayout* Layout = nullptr;
        std::vector<ResourceBinding> Bindings;
    };

}  // namespace Limitless
