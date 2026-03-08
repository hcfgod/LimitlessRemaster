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
    internal static unsafe delegate*<uint, bool> HasRigidbody2DComponentIcall;
    internal static unsafe delegate*<uint, int> GetRigidbody2DBodyTypeIcall;
    internal static unsafe delegate*<uint, int, void> SetRigidbody2DBodyTypeIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DFreezePositionXIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DFreezePositionXIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DFreezePositionYIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DFreezePositionYIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DFixedRotationIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DFixedRotationIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DUseCCDIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DUseCCDIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DEnableSleepIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DEnableSleepIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DStartAwakeIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DStartAwakeIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DInterpolateIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DInterpolateIcall;
    internal static unsafe delegate*<uint, bool> GetRigidbody2DHighContactQualityIcall;
    internal static unsafe delegate*<uint, bool, void> SetRigidbody2DHighContactQualityIcall;
    internal static unsafe delegate*<uint, int> GetRigidbody2DExtraSolverSubStepsIcall;
    internal static unsafe delegate*<uint, int, void> SetRigidbody2DExtraSolverSubStepsIcall;
    internal static unsafe delegate*<uint, float> GetRigidbody2DGravityScaleIcall;
    internal static unsafe delegate*<uint, float, void> SetRigidbody2DGravityScaleIcall;
    internal static unsafe delegate*<uint, float> GetRigidbody2DLinearDampingIcall;
    internal static unsafe delegate*<uint, float, void> SetRigidbody2DLinearDampingIcall;
    internal static unsafe delegate*<uint, float> GetRigidbody2DAngularDampingIcall;
    internal static unsafe delegate*<uint, float, void> SetRigidbody2DAngularDampingIcall;
    internal static unsafe delegate*<uint, Vector2> GetRigidbody2DLinearVelocityIcall;
    internal static unsafe delegate*<uint, Vector2, void> SetRigidbody2DLinearVelocityIcall;
    internal static unsafe delegate*<uint, float, void> SetRigidbody2DLinearVelocityXIcall;
    internal static unsafe delegate*<uint, float, void> SetRigidbody2DLinearVelocityYIcall;
    internal static unsafe delegate*<uint, Vector2, void> AddRigidbody2DLinearVelocityIcall;
    internal static unsafe delegate*<uint, bool, int> GetRigidbody2DContactCountIcall;
    internal static unsafe delegate*<uint, uint, bool, bool> HasContactWithEntityIcall;
    internal static unsafe delegate*<uint, bool, uint> GetContactEntityCountIcall;
    internal static unsafe delegate*<uint, bool, uint, uint> GetContactEntityAtIcall;
    internal static unsafe delegate*<Vector2, Vector2, float, ulong, RaycastHit2D> Raycast2DIcall;
#pragma warning restore 0649
}
