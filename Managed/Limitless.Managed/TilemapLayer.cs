using Coral.Managed.Interop;

namespace Limitless.Managed;

public sealed class TilemapLayer : EntityComponent
{
    internal TilemapLayer(uint entityHandle)
        : base(entityHandle)
    {
    }

    public int GridWidth
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerGridWidthIcall(EntityHandle); } }
    }

    public int GridHeight
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerGridHeightIcall(EntityHandle); } }
    }

    public int RenderOrder
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerRenderOrderIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetTilemapLayerRenderOrderIcall(EntityHandle, value); } }
    }

    public bool CollisionEnabled
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerCollisionEnabledIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetTilemapLayerCollisionEnabledIcall(EntityHandle, value); } }
    }

    public bool CastShadows
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerCastShadowsIcall(EntityHandle); } }
        set { unsafe { ScriptBridge.SetTilemapLayerCastShadowsIcall(EntityHandle, value); } }
    }

    public int CellCount
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerCellCountIcall(EntityHandle); } }
    }

    public int TileTableEntryCount
    {
        get { unsafe { return ScriptBridge.GetTilemapLayerTileTableEntryCountIcall(EntityHandle); } }
    }

    public void ResizeGrid(int width, int height)
    {
        unsafe
        {
            ScriptBridge.ResizeTilemapLayerGridIcall(EntityHandle, width, height);
        }
    }

    public bool IsCellInBounds(int cellX, int cellY)
    {
        unsafe
        {
            return ScriptBridge.IsTilemapLayerCellInBoundsIcall(EntityHandle, cellX, cellY);
        }
    }

    public int GetTileId(int cellX, int cellY)
    {
        unsafe
        {
            return ScriptBridge.GetTilemapLayerTileIdIcall(EntityHandle, cellX, cellY);
        }
    }

    public void SetTileId(int cellX, int cellY, int tileId)
    {
        unsafe
        {
            ScriptBridge.SetTilemapLayerTileIdIcall(EntityHandle, cellX, cellY, tileId);
        }
    }

    public string GetTileAssetKey(int cellX, int cellY)
    {
        unsafe
        {
            NativeString nativeValue = ScriptBridge.GetTilemapLayerTileAssetKeyIcall(EntityHandle, cellX, cellY);
            try
            {
                return nativeValue.ToString() ?? string.Empty;
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public void SetTileAssetKey(int cellX, int cellY, string tileAssetKey)
    {
        unsafe
        {
            NativeString nativeValue = tileAssetKey ?? string.Empty;
            try
            {
                ScriptBridge.SetTilemapLayerTileAssetKeyIcall(EntityHandle, cellX, cellY, nativeValue);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public void ClearTile(int cellX, int cellY)
    {
        SetTileAssetKey(cellX, cellY, string.Empty);
    }

    public string GetTileTableEntry(int index)
    {
        unsafe
        {
            NativeString nativeValue = ScriptBridge.GetTilemapLayerTileTableEntryIcall(EntityHandle, index);
            try
            {
                return nativeValue.ToString() ?? string.Empty;
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public void SetTileTableEntry(int index, string tileAssetKey)
    {
        unsafe
        {
            NativeString nativeValue = tileAssetKey ?? string.Empty;
            try
            {
                ScriptBridge.SetTilemapLayerTileTableEntryIcall(EntityHandle, index, nativeValue);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }

    public int GetOrAddTileTableEntry(string tileAssetKey)
    {
        unsafe
        {
            NativeString nativeValue = tileAssetKey ?? string.Empty;
            try
            {
                return ScriptBridge.GetOrAddTilemapLayerTileTableEntryIcall(EntityHandle, nativeValue);
            }
            finally
            {
                nativeValue.Dispose();
            }
        }
    }
}
