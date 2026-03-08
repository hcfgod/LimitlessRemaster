namespace Limitless.Managed;

public sealed class Rigidbody2D
{
    private readonly uint m_EntityHandle;

    internal Rigidbody2D(uint entityHandle)
    {
        m_EntityHandle = entityHandle;
    }

    public Rigidbody2DBodyType BodyType
    {
        get
        {
            unsafe
            {
                return (Rigidbody2DBodyType)ScriptBridge.GetRigidbody2DBodyTypeIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DBodyTypeIcall(m_EntityHandle, (int)value);
            }
        }
    }

    public bool FreezePositionX
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DFreezePositionXIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DFreezePositionXIcall(m_EntityHandle, value);
            }
        }
    }

    public bool FreezePositionY
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DFreezePositionYIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DFreezePositionYIcall(m_EntityHandle, value);
            }
        }
    }

    public bool FixedRotation
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DFixedRotationIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DFixedRotationIcall(m_EntityHandle, value);
            }
        }
    }

    public bool IsRotationLocked => FixedRotation;

    public bool UseCCD
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DUseCCDIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DUseCCDIcall(m_EntityHandle, value);
            }
        }
    }

    public bool EnableSleep
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DEnableSleepIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DEnableSleepIcall(m_EntityHandle, value);
            }
        }
    }

    public bool StartAwake
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DStartAwakeIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DStartAwakeIcall(m_EntityHandle, value);
            }
        }
    }

    public bool Interpolate
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DInterpolateIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DInterpolateIcall(m_EntityHandle, value);
            }
        }
    }

    public bool HighContactQuality
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DHighContactQualityIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DHighContactQualityIcall(m_EntityHandle, value);
            }
        }
    }

    public int ExtraSolverSubSteps
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DExtraSolverSubStepsIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DExtraSolverSubStepsIcall(m_EntityHandle, value);
            }
        }
    }

    public float GravityScale
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DGravityScaleIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DGravityScaleIcall(m_EntityHandle, value);
            }
        }
    }

    public float LinearDamping
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DLinearDampingIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DLinearDampingIcall(m_EntityHandle, value);
            }
        }
    }

    public float AngularDamping
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DAngularDampingIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DAngularDampingIcall(m_EntityHandle, value);
            }
        }
    }

    public Vector2 LinearVelocity
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DLinearVelocityIcall(m_EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DLinearVelocityIcall(m_EntityHandle, value);
            }
        }
    }

    public float LinearVelocityX
    {
        get => LinearVelocity.X;
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DLinearVelocityXIcall(m_EntityHandle, value);
            }
        }
    }

    public float LinearVelocityY
    {
        get => LinearVelocity.Y;
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DLinearVelocityYIcall(m_EntityHandle, value);
            }
        }
    }

    public void AddLinearVelocity(Vector2 deltaVelocity)
    {
        unsafe
        {
            ScriptBridge.AddRigidbody2DLinearVelocityIcall(m_EntityHandle, deltaVelocity);
        }
    }

    public int GetContactCount(bool includeSensorContacts = true)
    {
        unsafe
        {
            return ScriptBridge.GetRigidbody2DContactCountIcall(m_EntityHandle, includeSensorContacts);
        }
    }

    public bool HasContactWith(Entity other, bool includeSensorContacts = true)
    {
        unsafe
        {
            return ScriptBridge.HasContactWithEntityIcall(m_EntityHandle, other.Handle, includeSensorContacts);
        }
    }

    public Entity[] GetContactEntities(bool includeSensorContacts = true)
    {
        unsafe
        {
            int count = checked((int)ScriptBridge.GetContactEntityCountIcall(m_EntityHandle, includeSensorContacts));
            if (count == 0)
                return [];

            Entity[] result = new Entity[count];
            for (int i = 0; i < count; i++)
                result[i] = new Entity(ScriptBridge.GetContactEntityAtIcall(m_EntityHandle, includeSensorContacts, checked((uint)i)));
            return result;
        }
    }
}
