#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Graphics/Renderer2D.h"
#include <glm/glm.hpp>

TEST_SUITE("Renderer2D")
{
    TEST_CASE("GetStatistics returns zeros on a fresh instance")
    {
        Limitless::Renderer2D renderer;
        auto stats = renderer.GetStatistics();
        CHECK(stats.DrawCalls == 0);
        CHECK(stats.Batches == 0);
        CHECK(stats.QuadCount == 0);
    }

    TEST_CASE("ResetStatistics does not crash on a fresh instance")
    {
        Limitless::Renderer2D renderer;
        CHECK_NOTHROW(renderer.ResetStatistics());
    }

    TEST_CASE("ResetStatistics clears reported values")
    {
        Limitless::Renderer2D renderer;
        renderer.ResetStatistics();
        auto stats = renderer.GetStatistics();
        CHECK(stats.DrawCalls == 0);
        CHECK(stats.Batches == 0);
        CHECK(stats.QuadCount == 0);
    }

    TEST_CASE("Statistics struct has expected fields")
    {
        Limitless::Renderer2D::Statistics stats;
        stats.DrawCalls = 1;
        stats.Batches = 2;
        stats.QuadCount = 10;

        CHECK(stats.DrawCalls == 1);
        CHECK(stats.Batches == 2);
        CHECK(stats.QuadCount == 10);

        stats.Reset();
        CHECK(stats.DrawCalls == 0);
        CHECK(stats.Batches == 0);
        CHECK(stats.QuadCount == 0);
    }

    TEST_CASE("IsShaderReady returns false on a fresh instance")
    {
        Limitless::Renderer2D renderer;
        CHECK_FALSE(renderer.IsShaderReady());
    }

    TEST_CASE("GetDefaultShaderKey returns a non-null string")
    {
        const char* key = Limitless::Renderer2D::GetDefaultShaderKey();
        REQUIRE(key != nullptr);
        CHECK(std::string(key).empty() == false);
    }

    TEST_CASE("Statistics::Reset clears all fields to zero")
    {
        Limitless::Renderer2D::Statistics stats;
        stats.DrawCalls = 100;
        stats.Batches = 50;
        stats.QuadCount = 10000;

        stats.Reset();
        CHECK(stats.DrawCalls == 0);
        CHECK(stats.Batches == 0);
        CHECK(stats.QuadCount == 0);
    }

    TEST_CASE("GetStatistics is consistent across consecutive calls")
    {
        Limitless::Renderer2D renderer;
        renderer.ResetStatistics();

        auto stats1 = renderer.GetStatistics();
        auto stats2 = renderer.GetStatistics();

        CHECK(stats1.DrawCalls == stats2.DrawCalls);
        CHECK(stats1.Batches == stats2.Batches);
        CHECK(stats1.QuadCount == stats2.QuadCount);
    }

    TEST_CASE("Multiple instances have independent state")
    {
        Limitless::Renderer2D a;
        Limitless::Renderer2D b;

        auto statsA = a.GetStatistics();
        auto statsB = b.GetStatistics();

        CHECK(statsA.DrawCalls == 0);
        CHECK(statsB.DrawCalls == 0);
    }

    TEST_CASE("Default returns a valid reference")
    {
        auto& def = Limitless::Renderer2D::Default();
        CHECK(def.GetDefaultShaderKey() != nullptr);
    }
}
