#include "Assets/AssetImporterVersion.h"

#include "Assets/AnimationClipAssetImporter.h"
#include "Assets/AnimatorControllerAssetImporter.h"
#include "Assets/AudioClipAssetImporter.h"
#include "Assets/InputActionsAssetImporter.h"
#include "Assets/MaterialAssetImporter.h"
#include "Assets/ShaderAssetImporter.h"
#include "Assets/TextureAssetImporter.h"

namespace Limitless::Assets
{
    uint32_t GetCurrentAssetImporterVersion(AssetType type)
    {
        switch (type)
        {
            case AssetType::Texture2D:
                return AssetImporter<TextureAsset>::Version;
            case AssetType::Shader:
                return AssetImporter<ShaderAsset>::Version;
            case AssetType::Material:
                return AssetImporter<MaterialAsset>::Version;
            case AssetType::InputActions:
                return AssetImporter<InputActionsAssetResource>::Version;
            case AssetType::AudioClip:
                return AssetImporter<AudioClipAsset>::Version;
            case AssetType::AnimationClip:
                return AssetImporter<AnimationClipAsset>::Version;
            case AssetType::AnimatorController:
                return AssetImporter<AnimatorControllerAsset>::Version;
            default:
                return 1u;
        }
    }
}
