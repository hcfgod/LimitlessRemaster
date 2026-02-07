#include <doctest/doctest.h>

#include "Core/Input/InputSystem.h"
#include "Core/Input/InputAction.h"

#include <SDL3/SDL_events.h>

TEST_SUITE("Input Actions")
{
    static SDL_Event MakeKeyDown(SDL_Scancode scancode)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = scancode;
        e.key.down = true;
        e.key.repeat = false;
        return e;
    }

    static SDL_Event MakeKeyUp(SDL_Scancode scancode)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_UP;
        e.key.type = SDL_EVENT_KEY_UP;
        e.key.scancode = scancode;
        e.key.down = false;
        e.key.repeat = false;
        return e;
    }

    TEST_CASE("Button action phase transitions: Started -> Performed -> Canceled")
    {
        auto& input = Limitless::GetInputSystem();

        auto asset = std::make_shared<Limitless::InputActionAsset>();
        auto& map = asset->AddMap("Gameplay");
        auto& jump = map.AddAction("Jump", Limitless::InputActionValueType::Button);
        jump.AddBinding(Limitless::KeyboardButtonBinding{ .Key = SDL_SCANCODE_SPACE });

        input.SetProjectActionAsset(asset);

        // Frame 1: press
        input.BeginFrame();
        input.OnSdlEvent(MakeKeyDown(SDL_SCANCODE_SPACE));
        input.UpdateActions();
        CHECK(jump.WasStartedThisFrame());
        CHECK(jump.ReadButton() == true);

        // Frame 2: held
        input.BeginFrame();
        input.UpdateActions();
        CHECK(jump.WasPerformedThisFrame());
        CHECK(jump.ReadButton() == true);

        // Frame 3: release
        input.BeginFrame();
        input.OnSdlEvent(MakeKeyUp(SDL_SCANCODE_SPACE));
        input.UpdateActions();
        CHECK(jump.WasCanceledThisFrame());
        CHECK(jump.ReadButton() == false);
    }

    TEST_CASE("Axis2D from WASD evaluates as expected")
    {
        auto& input = Limitless::GetInputSystem();

        auto asset = std::make_shared<Limitless::InputActionAsset>();
        auto& map = asset->AddMap("Editor");
        auto& move = map.AddAction("Move", Limitless::InputActionValueType::Axis2D);
        move.AddBinding(Limitless::KeyboardAxis2DBinding{
            .Up = SDL_SCANCODE_W,
            .Down = SDL_SCANCODE_S,
            .Left = SDL_SCANCODE_A,
            .Right = SDL_SCANCODE_D,
            .Scale = 1.0f
        });

        input.SetProjectActionAsset(asset);

        input.BeginFrame();
        input.OnSdlEvent(MakeKeyDown(SDL_SCANCODE_W));
        input.OnSdlEvent(MakeKeyDown(SDL_SCANCODE_D));
        input.UpdateActions();

        const glm::vec2 v = move.ReadAxis2D();
        CHECK(v.x == doctest::Approx(1.0f));
        CHECK(v.y == doctest::Approx(1.0f));
    }

    TEST_CASE("Override asset stack takes precedence over project asset")
    {
        auto& input = Limitless::GetInputSystem();

        auto project = std::make_shared<Limitless::InputActionAsset>();
        auto& projectMap = project->AddMap("Gameplay");
        auto& actionA = projectMap.AddAction("A", Limitless::InputActionValueType::Button);
        actionA.AddBinding(Limitless::KeyboardButtonBinding{ .Key = SDL_SCANCODE_A });

        auto overrideAsset = std::make_shared<Limitless::InputActionAsset>();
        auto& overrideMap = overrideAsset->AddMap("Gameplay");
        auto& actionB = overrideMap.AddAction("B", Limitless::InputActionValueType::Button);
        actionB.AddBinding(Limitless::KeyboardButtonBinding{ .Key = SDL_SCANCODE_B });

        input.SetProjectActionAsset(project);
        input.PushOverrideActionAsset(overrideAsset);

        input.BeginFrame();
        input.OnSdlEvent(MakeKeyDown(SDL_SCANCODE_A));
        input.OnSdlEvent(MakeKeyDown(SDL_SCANCODE_B));
        input.UpdateActions();

        // Project asset should not be updated while override is active.
        CHECK(actionA.ReadButton() == false);
        CHECK(actionB.ReadButton() == true);

        CHECK(input.PopOverrideActionAsset(overrideAsset));
    }
}

