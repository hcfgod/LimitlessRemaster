#include <doctest/doctest.h>

#include "Core/Input/InputSystem.h"

#include <SDL3/SDL_events.h>

TEST_SUITE("Input System")
{
    static SDL_Event MakeKeyEvent(SDL_EventType type, SDL_Scancode scancode, bool down, bool repeat = false)
    {
        SDL_Event e{};
        e.type = type;
        e.key.type = type;
        e.key.scancode = scancode;
        e.key.down = down;
        e.key.repeat = repeat;
        return e;
    }

    static SDL_Event MakeMouseMotionEvent(float x, float y, float dx, float dy)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.x = x;
        e.motion.y = y;
        e.motion.xrel = dx;
        e.motion.yrel = dy;
        return e;
    }

    static SDL_Event MakeMouseButtonEvent(SDL_EventType type, uint8_t button, bool down)
    {
        SDL_Event e{};
        e.type = type;
        e.button.type = type;
        e.button.button = button;
        e.button.down = down;
        return e;
    }

    static SDL_Event MakeMouseWheelEvent(float x, float y)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.x = x;
        e.wheel.y = y;
        return e;
    }

    TEST_CASE("BeginFrame resets per-frame state")
    {
        auto& input = Limitless::GetInputSystem();

        input.BeginFrame();
        CHECK(input.GetMouseDelta().x == doctest::Approx(0.0f));
        CHECK(input.GetMouseWheelDelta().y == doctest::Approx(0.0f));

        input.OnSdlEvent(MakeMouseMotionEvent(10.0f, 20.0f, 3.0f, 4.0f));
        input.OnSdlEvent(MakeMouseWheelEvent(0.0f, 1.0f));

        CHECK(input.GetMouseDelta().x == doctest::Approx(3.0f));
        CHECK(input.GetMouseWheelDelta().y == doctest::Approx(1.0f));

        input.BeginFrame();
        CHECK(input.GetMouseDelta().x == doctest::Approx(0.0f));
        CHECK(input.GetMouseDelta().y == doctest::Approx(0.0f));
        CHECK(input.GetMouseWheelDelta().x == doctest::Approx(0.0f));
        CHECK(input.GetMouseWheelDelta().y == doctest::Approx(0.0f));
    }

    TEST_CASE("Keyboard pressed/released flags are per-frame and repeat is ignored")
    {
        auto& input = Limitless::GetInputSystem();
        input.BeginFrame();

        // Press W
        input.OnSdlEvent(MakeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, true, false));
        CHECK(input.IsKeyDown(SDL_SCANCODE_W));
        CHECK(input.WasKeyPressedThisFrame(SDL_SCANCODE_W));
        CHECK(!input.WasKeyReleasedThisFrame(SDL_SCANCODE_W));

        // Repeat press shouldn't set pressed-this-frame again.
        input.OnSdlEvent(MakeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, true, true));
        CHECK(input.IsKeyDown(SDL_SCANCODE_W));
        CHECK(input.WasKeyPressedThisFrame(SDL_SCANCODE_W));

        // New frame clears pressed/released flags.
        input.BeginFrame();
        CHECK(input.IsKeyDown(SDL_SCANCODE_W));
        CHECK(!input.WasKeyPressedThisFrame(SDL_SCANCODE_W));
        CHECK(!input.WasKeyReleasedThisFrame(SDL_SCANCODE_W));

        // Release W
        input.OnSdlEvent(MakeKeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_W, false, false));
        CHECK(!input.IsKeyDown(SDL_SCANCODE_W));
        CHECK(input.WasKeyReleasedThisFrame(SDL_SCANCODE_W));
    }

    TEST_CASE("Mouse button state and pressed/released flags")
    {
        auto& input = Limitless::GetInputSystem();
        input.BeginFrame();

        input.OnSdlEvent(MakeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, true));
        CHECK(input.IsMouseButtonDown(SDL_BUTTON_RIGHT));
        CHECK(input.WasMouseButtonPressedThisFrame(SDL_BUTTON_RIGHT));

        input.BeginFrame();
        CHECK(input.IsMouseButtonDown(SDL_BUTTON_RIGHT));
        CHECK(!input.WasMouseButtonPressedThisFrame(SDL_BUTTON_RIGHT));

        input.OnSdlEvent(MakeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, false));
        CHECK(!input.IsMouseButtonDown(SDL_BUTTON_RIGHT));
        CHECK(input.WasMouseButtonReleasedThisFrame(SDL_BUTTON_RIGHT));
    }
}

