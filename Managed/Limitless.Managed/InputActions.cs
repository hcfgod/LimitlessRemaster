using Coral.Managed.Interop;

namespace Limitless.Managed;

public static class InputActions
{
    public static bool HasAction(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionExistsIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static bool IsPressed(string mapName, string actionName, float deadzone = 0.0001f)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionPressedIcall(nativeMapName, nativeActionName, deadzone);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static bool WasStartedThisFrame(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionStartedIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static bool WasPerformedThisFrame(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionPerformedIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static bool WasCanceledThisFrame(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionCanceledIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static bool ReadButton(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionButtonIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static float ReadAxis1D(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionAxis1DIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }

    public static Vector2 ReadAxis2D(string mapName, string actionName)
    {
        unsafe
        {
            NativeString nativeMapName = mapName ?? string.Empty;
            NativeString nativeActionName = actionName ?? string.Empty;
            try
            {
                return ScriptBridge.InputActionAxis2DIcall(nativeMapName, nativeActionName);
            }
            finally
            {
                nativeActionName.Dispose();
                nativeMapName.Dispose();
            }
        }
    }
}
