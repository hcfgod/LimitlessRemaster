using Limitless.Managed;

namespace Limitless.Managed.TestScripts;

public sealed class ManagedEntityApiSmokeScript : ScriptableEntity
{
    private Vector3 m_OriginalPosition;
    private bool m_HasTransform;
    private string m_OriginalTag = string.Empty;

    public override void OnCreate()
    {
        m_HasTransform = Entity.HasTransform;
        m_OriginalTag = Entity.HasTag ? Entity.Tag : string.Empty;
        m_OriginalPosition = m_HasTransform ? Transform.Position : Vector3.Zero;

        LogInfo($"EntityApi OnCreate handle={Entity.Handle} alive={Entity.IsAlive} hasTag={Entity.HasTag} tag='{m_OriginalTag}' hasTransform={m_HasTransform} position={m_OriginalPosition}");

        if (Entity.HasTag && !string.IsNullOrWhiteSpace(m_OriginalTag))
        {
            Entity found = FindEntityByTag(m_OriginalTag);
            LogInfo($"EntityApi FindEntityByTag('{m_OriginalTag}') => {found.Handle} alive={found.IsAlive}");
        }

        if (m_HasTransform)
        {
            Transform.Position = new Vector3(m_OriginalPosition.X + 1.0f, m_OriginalPosition.Y, m_OriginalPosition.Z);
            Transform.Rotation = new Vector3(Transform.Rotation.X, Transform.Rotation.Y, Transform.Rotation.Z + 5.0f);
            LogInfo($"EntityApi mutated transform position={Transform.Position} rotation={Transform.Rotation} scale={Transform.Scale}");
        }
    }

    public override void OnDestroy()
    {
        if (m_HasTransform)
            Transform.Position = m_OriginalPosition;

        LogInfo($"EntityApi OnDestroy handle={Entity.Handle} finalTag='{(Entity.HasTag ? Entity.Tag : string.Empty)}' finalPosition={(m_HasTransform ? Transform.Position : Vector3.Zero)}");
    }
}
