#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Assets/AssetManager.h"
#include "Assets/TextureAsset.h"

TEST_SUITE("Asset System")
{
    TEST_CASE("AssetManager cache stats are initially zero")
    {
        Limitless::Assets::AssetManager::ClearCaches();

        size_t keyCount = 999;
        size_t guidCount = 999;
        Limitless::Assets::AssetManager::GetCacheStats(keyCount, guidCount);

        CHECK(keyCount == 0);
        CHECK(guidCount == 0);
    }

    TEST_CASE("AssetManager GetCachedByKey returns nullptr for empty key")
    {
        auto result = Limitless::Assets::AssetManager::GetCachedByKey("");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager GetCachedByKey returns nullptr for non-existent key")
    {
        auto result = Limitless::Assets::AssetManager::GetCachedByKey("Assets/NonExistent/Asset.xyz");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager GetCachedByGuid returns nullptr for empty guid")
    {
        auto result = Limitless::Assets::AssetManager::GetCachedByGuid("");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager GetByGuid returns nullptr for empty guid")
    {
        auto result = Limitless::Assets::AssetManager::GetByGuid<Limitless::Assets::TextureAsset>("");
        CHECK(result == nullptr);
    }

    TEST_CASE("AssetManager ClearCaches does not crash")
    {
        CHECK_NOTHROW(Limitless::Assets::AssetManager::ClearCaches());
    }

    TEST_CASE("AssetManager GetCacheStats after ClearCaches reports zero")
    {
        Limitless::Assets::AssetManager::ClearCaches();

        size_t keyCount = 999;
        size_t guidCount = 999;
        Limitless::Assets::AssetManager::GetCacheStats(keyCount, guidCount);

        CHECK(keyCount == 0);
        CHECK(guidCount == 0);
    }
}
