using Coral.Managed.Interop;

namespace Limitless.Managed;

internal static class ScriptBridge
{
#pragma warning disable 0649
    internal static unsafe delegate*<NativeString, void> LogInfoIcall;
    internal static unsafe delegate*<NativeString, void> LogWarningIcall;
    internal static unsafe delegate*<NativeString, void> LogErrorIcall;
    internal static unsafe delegate*<uint, bool> EntityExistsIcall;
    internal static unsafe delegate*<NativeString, uint> FindEntityByTagIcall;
    internal static unsafe delegate*<uint, bool> HasTagComponentIcall;
    internal static unsafe delegate*<uint, NativeString> GetTagIcall;
    internal static unsafe delegate*<uint, NativeString, bool> SetTagIcall;
    internal static unsafe delegate*<uint, bool> HasTransformComponentIcall;
    internal static unsafe delegate*<uint, Vector3> GetTransformPositionIcall;
    internal static unsafe delegate*<uint, Vector3, void> SetTransformPositionIcall;
    internal static unsafe delegate*<uint, Vector3> GetTransformRotationIcall;
    internal static unsafe delegate*<uint, Vector3, void> SetTransformRotationIcall;
    internal static unsafe delegate*<uint, Vector3> GetTransformScaleIcall;
    internal static unsafe delegate*<uint, Vector3, void> SetTransformScaleIcall;
#pragma warning restore 0649
}
