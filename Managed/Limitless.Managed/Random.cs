namespace Limitless.Managed;

public static class Random
{
    public static float Value
    {
        get
        {
            unsafe
            {
                return ScriptBridge.RandomValueIcall();
            }
        }
    }

    public static float value => Value;

    public static void InitState(int seed)
    {
        unsafe
        {
            ScriptBridge.SetRandomSeedIcall(unchecked((uint)seed));
        }
    }

    public static int Range(int minInclusive, int maxExclusive)
    {
        unsafe
        {
            return ScriptBridge.RandomRangeIntIcall(minInclusive, maxExclusive);
        }
    }

    public static float Range(float minInclusive, float maxInclusive)
    {
        unsafe
        {
            return ScriptBridge.RandomRangeFloatIcall(minInclusive, maxInclusive);
        }
    }
}
