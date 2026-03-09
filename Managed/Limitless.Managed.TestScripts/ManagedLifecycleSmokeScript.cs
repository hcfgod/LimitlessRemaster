using Limitless.Managed;

namespace Limitless.Managed.TestScripts;

public sealed class ManagedLifecycleSmokeScript : ScriptableEntity
{
    private int m_UpdateCount;
    private bool m_LoggedFirstUpdate;

    public override void OnCreate()
    {
        m_UpdateCount = 0;
        m_LoggedFirstUpdate = false;
        Debug.Log($"OnCreate entity={EntityHandle} alive={IsEntityAlive}");
    }

    public override void OnUpdate(float deltaTime)
    {
        m_UpdateCount++;
        if (!m_LoggedFirstUpdate)
        {
            m_LoggedFirstUpdate = true;
            Debug.Log($"OnUpdate entity={EntityHandle} dt={deltaTime:0.000} count={m_UpdateCount}");
        }
    }

    public override void OnDestroy()
    {
        Debug.Log($"OnDestroy entity={EntityHandle} updates={m_UpdateCount}");
    }
}
