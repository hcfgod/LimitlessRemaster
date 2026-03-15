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
    namespace
    {
        constexpr uint32_t kSceneImporterVersion = 1u;
        constexpr uint32_t kPrefabImporterVersion = 1u;
        constexpr uint32_t kTilemapImporterVersion = 1u;
        constexpr uint32_t kTilesetImporterVersion = 1u;
        constexpr uint32_t kAudioMixerImporterVersion = 1u;
        constexpr uint32_t kTileImporterVersion = 1u;
        constexpr uint32_t kTilePaletteImporterVersion = 1u;
        constexpr uint32_t kMeshImporterVersion = 1u;
    }

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
            case AssetType::Mesh:
                return kMeshImporterVersion;
            case AssetType::Scene:
                return kSceneImporterVersion;
            case AssetType::InputActions:
                return AssetImporter<InputActionsAssetResource>::Version;
            case AssetType::AudioClip:
                return AssetImporter<AudioClipAsset>::Version;
            case AssetType::Prefab:
                return kPrefabImporterVersion;
            case AssetType::Tilemap:
                return kTilemapImporterVersion;
            case AssetType::AnimationClip:
                return AssetImporter<AnimationClipAsset>::Version;
            case AssetType::Tileset:
                return kTilesetImporterVersion;
            case AssetType::AudioMixer:
                return kAudioMixerImporterVersion;
            case AssetType::AnimatorController:
                return AssetImporter<AnimatorControllerAsset>::Version;
            case AssetType::Tile:
                return kTileImporterVersion;
            case AssetType::TilePalette:
                return kTilePaletteImporterVersion;
            default:
                return 1u;
        }
    }
}
