namespace Limitless.Managed;

public static class Physics2D
{
    public static RaycastHit2D Raycast(Vector2 origin, Vector2 direction, float maxDistance = 1000.0f, ulong collisionMask = ulong.MaxValue)
    {
        unsafe
        {
            return ScriptBridge.Raycast2DIcall(origin, direction, maxDistance, collisionMask);
        }
    }

    public static bool Raycast(Vector2 origin, Vector2 direction, out RaycastHit2D hit, float maxDistance = 1000.0f, ulong collisionMask = ulong.MaxValue)
    {
        hit = Raycast(origin, direction, maxDistance, collisionMask);
        return hit.HasHit;
    }
}
