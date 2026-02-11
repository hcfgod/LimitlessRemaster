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

    // Full Renderer2D tests (BeginScene, DrawQuad, EndScene) require graphics context
    // initialization. See Sandbox/Renderer2DDemo for integration usage.
}
