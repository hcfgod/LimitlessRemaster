namespace Limitless.Managed;

public sealed class Grid2D : EntityComponent
{
    internal Grid2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Vector2 CellSize
    {
        get { unsafe { return ScriptBridge.GetGrid2DCellSizeIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetGrid2DCellSizeIcall(EntityHandle, value); } }
    }

    public Vector2 CellGap
    {
        get { unsafe { return ScriptBridge.GetGrid2DCellGapIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetGrid2DCellGapIcall(EntityHandle, value); } }
    }
}
