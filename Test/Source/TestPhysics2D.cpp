#define DOCTEST_CONFIG_WITH_VARIADIC_MACROS
#include <doctest/doctest.h>

#include "Physics/Physics2DWorld.h"
#include "Scene/Scene.h"
#include "Scene/Components/PhysicsComponents.h"

#include <glm/glm.hpp>

TEST_SUITE("Physics2D")
{
    TEST_CASE("Physics2DWorldSettings has sensible defaults")
    {
        Limitless::Physics2DWorldSettings settings;

        CHECK(settings.WorldCount == 1);
        CHECK(settings.Gravity.x == doctest::Approx(0.0f));
        CHECK(settings.Gravity.y == doctest::Approx(-9.81f));
        CHECK(settings.VelocitySubSteps == 8);
        CHECK(settings.EnableSleep == true);
        CHECK(settings.EnableContinuousCollision == true);
        CHECK(settings.HighContactQualityMode == false);
    }

    TEST_CASE("Physics2DRaycastHit defaults to no hit")
    {
        Limitless::Physics2DRaycastHit hit;

        CHECK(hit.HasHit == false);
        CHECK((hit.Entity == entt::null));
        CHECK(hit.Point == glm::vec2(0.0f));
        CHECK(hit.Fraction == doctest::Approx(0.0f));
    }

    TEST_CASE("Physics2DDiagnostics defaults to zero counts")
    {
        Limitless::Physics2DDiagnostics diag;

        CHECK(diag.BodyCount == 0);
        CHECK(diag.AwakeBodyCount == 0);
        CHECK(diag.SleepingBodyCount == 0);
        CHECK(diag.ContactPairCount == 0);
        CHECK(diag.PenetratingContactPointCount == 0);
        CHECK(diag.MaxPenetrationDepth == doctest::Approx(0.0f));
    }

    TEST_CASE("Physics2DBodyDiagnostics defaults to invalid")
    {
        Limitless::Physics2DBodyDiagnostics bodyDiag;

        CHECK(bodyDiag.IsValid == false);
        CHECK(bodyDiag.IsAwake == false);
        CHECK(bodyDiag.ContactPairCount == 0);
    }

    TEST_CASE("Physics2DWorld construction and initialization")
    {
        Limitless::Physics2DWorld world(0);

        CHECK(world.GetSceneWorldSlot() == 0);
        CHECK_FALSE(world.IsInitialized());

        Limitless::Physics2DWorldSettings settings;
        settings.Gravity = glm::vec2(0.0f, -20.0f);
        settings.VelocitySubSteps = 4;

        world.Initialize(settings);
        CHECK(world.IsInitialized());
        CHECK(world.GetSettings().Gravity.y == doctest::Approx(-20.0f));
        CHECK(world.GetSettings().VelocitySubSteps == 4);
    }

    TEST_CASE("Physics2DWorld SetSettings updates configuration")
    {
        Limitless::Physics2DWorld world(0);

        Limitless::Physics2DWorldSettings settings;
        settings.Gravity = glm::vec2(0.0f, -9.81f);
        world.Initialize(settings);

        Limitless::Physics2DWorldSettings newSettings = settings;
        newSettings.Gravity = glm::vec2(0.0f, -15.0f);
        newSettings.EnableSleep = false;

        world.SetSettings(newSettings);
        CHECK(world.GetSettings().Gravity.y == doctest::Approx(-15.0f));
        CHECK(world.GetSettings().EnableSleep == false);
    }

    TEST_CASE("Physics2DWorld diagnostics are initially zeroed")
    {
        Limitless::Physics2DWorld world(0);

        Limitless::Physics2DWorldSettings settings;
        world.Initialize(settings);

        auto& diag = world.GetDiagnostics();
        CHECK(diag.BodyCount == 0);
        CHECK(diag.AwakeBodyCount == 0);
    }

    TEST_CASE("Physics2DWorld diagnostics enable/disable")
    {
        Limitless::Physics2DWorld world(0);

        CHECK(world.IsDiagnosticsEnabled());

        world.SetDiagnosticsEnabled(false);
        CHECK_FALSE(world.IsDiagnosticsEnabled());

        world.SetDiagnosticsEnabled(true);
        CHECK(world.IsDiagnosticsEnabled());
    }

    TEST_CASE("Physics2DWorld different world slots")
    {
        Limitless::Physics2DWorld world0(0);
        Limitless::Physics2DWorld world1(1);
        Limitless::Physics2DWorld world5(5);

        CHECK(world0.GetSceneWorldSlot() == 0);
        CHECK(world1.GetSceneWorldSlot() == 1);
        CHECK(world5.GetSceneWorldSlot() == 5);
    }

    TEST_CASE("Scene physics world access before initialization returns null")
    {
        Limitless::Scene scene;

        const auto* world = scene.GetPhysics2DWorld(0);
        CHECK((world == nullptr || !world->IsInitialized()));
    }

    TEST_CASE("Scene GetPhysics2DSettings returns defaults")
    {
        Limitless::Scene scene;

        const auto& settings = scene.GetPhysics2DSettings();
        CHECK(settings.WorldCount == 1);
        CHECK(settings.Gravity.y == doctest::Approx(-9.81f));
    }

    TEST_CASE("Scene SetPhysics2DSettings updates stored settings")
    {
        Limitless::Scene scene;

        Limitless::Physics2DWorldSettings settings;
        settings.Gravity = glm::vec2(0.0f, -20.0f);
        settings.WorldCount = 2;
        settings.EnableSleep = false;

        scene.SetPhysics2DSettings(settings);

        const auto& stored = scene.GetPhysics2DSettings();
        CHECK(stored.Gravity.y == doctest::Approx(-20.0f));
        CHECK(stored.WorldCount == 2);
        CHECK(stored.EnableSleep == false);
    }

    TEST_CASE("Physics2DWorld TryGetBodyDiagnostics returns false for unknown entity")
    {
        Limitless::Physics2DWorld world(0);

        Limitless::Physics2DWorldSettings settings;
        world.Initialize(settings);

        Limitless::Physics2DBodyDiagnostics bodyDiag;
        bool found = world.TryGetBodyDiagnostics(entt::null, bodyDiag);
        CHECK_FALSE(found);
    }

    TEST_CASE("Physics2DWorld RaycastClosest returns no hit in empty world")
    {
        Limitless::Physics2DWorld world(0);

        Limitless::Physics2DWorldSettings settings;
        world.Initialize(settings);

        auto hit = world.RaycastClosest(
            glm::vec2(0.0f, 0.0f),
            glm::vec2(1.0f, 0.0f),
            100.0f,
            0xFFFFFFFFFFFFFFFFull);

        CHECK_FALSE(hit.HasHit);
    }
}
