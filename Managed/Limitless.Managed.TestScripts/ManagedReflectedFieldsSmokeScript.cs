using Limitless.Managed;

namespace Limitless.Managed.TestScripts;

public sealed class ManagedReflectedFieldsSmokeScript : ScriptableEntity
{
    public float Speed = 2.5f;
    public int Counter = 7;
    public bool EnabledFlag = true;
    public string DisplayName = "ManagedReflected";
    public Vector3 SpawnOffset = new(1.0f, 2.0f, 3.0f);
    public Entity? Target;

    public override void OnCreate()
    {
        LogInfo($"ReflectedFields OnCreate speed={Speed} counter={Counter} enabled={EnabledFlag} name='{DisplayName}' offset={SpawnOffset} targetAlive={(Target != null && Target.IsAlive)} targetHandle={(Target != null ? Target.Handle : Entity.NullHandle)}");

        if (EnabledFlag && Entity.HasTransform)
            Transform.Position = SpawnOffset;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!EnabledFlag || !Entity.HasTransform)
            return;

        Vector3 position = Transform.Position;
        position.X += Speed * deltaTime;
        Transform.Position = position;
    }

    public override void OnDestroy()
    {
        LogInfo($"ReflectedFields OnDestroy counter={Counter} name='{DisplayName}' finalPosition={(Entity.HasTransform ? Transform.Position : Vector3.Zero)}");
    }
}
