using Coral.Managed.Interop;

namespace Limitless.Managed;

public static class SceneManager
{
    public static bool LoadScene(string sceneIdentifier, LoadSceneMode loadMode = LoadSceneMode.Single)
    {
        unsafe
        {
            NativeString nativeSceneIdentifier = sceneIdentifier ?? string.Empty;
            try
            {
                return ScriptBridge.LoadSceneIcall(nativeSceneIdentifier, (int)loadMode);
            }
            finally
            {
                nativeSceneIdentifier.Dispose();
            }
        }
    }

    public static bool ReloadCurrentScene()
    {
        unsafe
        {
            return ScriptBridge.ReloadCurrentSceneIcall();
        }
    }

    public static bool SetActiveScene(string sceneIdentifier)
    {
        unsafe
        {
            NativeString nativeSceneIdentifier = sceneIdentifier ?? string.Empty;
            try
            {
                return ScriptBridge.SetActiveSceneIcall(nativeSceneIdentifier);
            }
            finally
            {
                nativeSceneIdentifier.Dispose();
            }
        }
    }

    public static bool UnloadScene(string sceneIdentifier)
    {
        unsafe
        {
            NativeString nativeSceneIdentifier = sceneIdentifier ?? string.Empty;
            try
            {
                return ScriptBridge.UnloadSceneIcall(nativeSceneIdentifier);
            }
            finally
            {
                nativeSceneIdentifier.Dispose();
            }
        }
    }
}
