namespace Limitless.Managed;

public static class Input
{
    public static Vector2 MousePosition
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetMousePositionIcall();
            }
        }
    }

    public static Vector2 mousePosition => MousePosition;

    public static Vector2 MouseDelta
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetMouseDeltaIcall();
            }
        }
    }

    public static Vector2 mouseDelta => MouseDelta;

    public static Vector2 MouseWheelDelta
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetMouseWheelDeltaIcall();
            }
        }
    }

    public static Vector2 mouseWheelDelta => MouseWheelDelta;

    public static float GetAxis(string mapName, string actionName)
    {
        return InputActions.ReadAxis1D(mapName, actionName);
    }

    public static bool GetButtonDown(string mapName, string actionName)
    {
        return InputActions.WasStartedThisFrame(mapName, actionName);
    }

    public static bool GetButton(string mapName, string actionName)
    {
        return InputActions.ReadButton(mapName, actionName);
    }

    public static Vector2 GetMousePosition()
    {
        return MousePosition;
    }

    public static Vector2 GetMouseDelta()
    {
        return MouseDelta;
    }

    public static Vector2 GetMouseWheelDelta()
    {
        return MouseWheelDelta;
    }

    public static bool GetMouseButton(MouseButton button)
    {
        unsafe
        {
            return ScriptBridge.GetMouseButtonIcall((int)button);
        }
    }

    public static bool GetMouseButtonDown(MouseButton button)
    {
        unsafe
        {
            return ScriptBridge.GetMouseButtonDownIcall((int)button);
        }
    }

    public static bool GetMouseButtonUp(MouseButton button)
    {
        unsafe
        {
            return ScriptBridge.GetMouseButtonUpIcall((int)button);
        }
    }
}
