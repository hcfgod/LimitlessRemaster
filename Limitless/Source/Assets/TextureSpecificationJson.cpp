#include "Assets/TextureSpecificationJson.h"

namespace Limitless::Assets
{
    TextureSpecification TextureSpecificationFromImporterSettingsJson(const nlohmann::json& j)
    {
        TextureSpecification spec{};
        if (!j.is_object())
        {
            return spec;
        }

        spec.GenerateMipmaps = j.value("generateMipmaps", spec.GenerateMipmaps);
        spec.FlipVerticallyOnLoad = j.value("flipVerticallyOnLoad", spec.FlipVerticallyOnLoad);
        spec.MinFilter = static_cast<TextureFilter>(j.value("minFilter", static_cast<int>(spec.MinFilter)));
        spec.MagFilter = static_cast<TextureFilter>(j.value("magFilter", static_cast<int>(spec.MagFilter)));
        spec.WrapU = static_cast<TextureWrap>(j.value("wrapU", static_cast<int>(spec.WrapU)));
        spec.WrapV = static_cast<TextureWrap>(j.value("wrapV", static_cast<int>(spec.WrapV)));
        return spec;
    }

    bool IsDefaultTextureSpecification(const TextureSpecification& s)
    {
        const TextureSpecification defaults{};
        return s.MinFilter == defaults.MinFilter &&
               s.MagFilter == defaults.MagFilter &&
               s.WrapU == defaults.WrapU &&
               s.WrapV == defaults.WrapV &&
               s.GenerateMipmaps == defaults.GenerateMipmaps &&
               s.FlipVerticallyOnLoad == defaults.FlipVerticallyOnLoad;
    }
}

