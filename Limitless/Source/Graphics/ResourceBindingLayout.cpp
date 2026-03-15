#include "ResourceBindingLayout.h"

namespace Limitless
{
    ResourceBindingLayout ResourceBindingLayout::FromReflection(const ShaderReflection& reflection)
    {
        ResourceBindingLayout layout;
        layout.Entries.reserve(reflection.Resources.size());

        for (const auto& res : reflection.Resources)
        {
            ResourceBindingEntry entry;
            entry.Name            = res.Name;
            entry.Binding         = res.Binding;
            entry.Set             = res.Set;
            entry.StageVisibility = res.StageVisibility;

            switch (res.ResourceType)
            {
                case ShaderResourceEntry::Type::UniformBuffer:
                    entry.BindingType = ResourceBindingEntry::Type::UniformBuffer;
                    break;
                case ShaderResourceEntry::Type::StorageBuffer:
                    entry.BindingType = ResourceBindingEntry::Type::StorageBuffer;
                    break;
                case ShaderResourceEntry::Type::SampledImage:
                    entry.BindingType = ResourceBindingEntry::Type::CombinedImageSampler;
                    break;
                case ShaderResourceEntry::Type::SeparateImage:
                    entry.BindingType = ResourceBindingEntry::Type::Texture;
                    break;
                case ShaderResourceEntry::Type::SeparateSampler:
                    entry.BindingType = ResourceBindingEntry::Type::Sampler;
                    break;
                case ShaderResourceEntry::Type::PushConstant:
                    // Push constants don't map to a binding entry in the layout;
                    // skip them (they're handled via a separate push-constant path).
                    continue;
            }

            layout.Entries.push_back(std::move(entry));
        }

        return layout;
    }

}  // namespace Limitless
