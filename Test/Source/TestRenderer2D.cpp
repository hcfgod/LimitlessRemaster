#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>
#include "Graphics/Renderer2D.h"
#include <glm/glm.hpp>

TEST_SUITE("Renderer2D")
{
    TEST_CASE("GetStatistics returns zeros before Initialize")
    {
        auto stats = Limitless::Renderer2D::GetStatistics();
        CHECK(stats.DrawCalls == 0);
        CHECK(stats.Batches == 0);
        CHECK(stats.QuadCount == 0);
    }

    TEST_CASE("ResetStatistics does not crash before Initialize")
    {
        CHECK_NOTHROW(Limitless::Renderer2D::ResetStatistics());
    }

    TEST_CASE("ResetStatistics clears reported values")
    {
        Limitless::Renderer2D::ResetStatistics();
        auto stats = Limitless::Renderer2D::GetStatistics();
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

    TEST_CASE("IsShaderReady returns false before Initialize")
    {
        CHECK_FALSE(Limitless::Renderer2D::IsShaderReady());
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
        Limitless::Renderer2D::ResetStatistics();

        auto stats1 = Limitless::Renderer2D::GetStatistics();
        auto stats2 = Limitless::Renderer2D::GetStatistics();

        CHECK(stats1.DrawCalls == stats2.DrawCalls);
        CHECK(stats1.Batches == stats2.Batches);
        CHECK(stats1.QuadCount == stats2.QuadCount);
    }
}
