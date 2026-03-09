namespace Limitless.Managed;

public sealed class Rigidbody2D : EntityComponent
{
    internal Rigidbody2D(uint entityHandle)
        : base(entityHandle)
    {
    }

    public Rigidbody2DBodyType BodyType
    {
        get
        {
            unsafe
            {
                return (Rigidbody2DBodyType)ScriptBridge.GetRigidbody2DBodyTypeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DBodyTypeIcall(EntityHandle, (int)value);
            }
        }
    }

    public bool FreezePositionX
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DFreezePositionXIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DFreezePositionXIcall(EntityHandle, value);
            }
        }
    }

    public bool FreezePositionY
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DFreezePositionYIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DFreezePositionYIcall(EntityHandle, value);
            }
        }
    }

    public bool FixedRotation
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DFixedRotationIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DFixedRotationIcall(EntityHandle, value);
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
                return ScriptBridge.GetRigidbody2DUseCCDIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DUseCCDIcall(EntityHandle, value);
            }
        }
    }

    public bool EnableSleep
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DEnableSleepIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DEnableSleepIcall(EntityHandle, value);
            }
        }
    }

    public bool StartAwake
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DStartAwakeIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DStartAwakeIcall(EntityHandle, value);
            }
        }
    }

    public bool Interpolate
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DInterpolateIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DInterpolateIcall(EntityHandle, value);
            }
        }
    }

    public bool HighContactQuality
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DHighContactQualityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DHighContactQualityIcall(EntityHandle, value);
            }
        }
    }

    public int ExtraSolverSubSteps
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DExtraSolverSubStepsIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DExtraSolverSubStepsIcall(EntityHandle, value);
            }
        }
    }

    public float GravityScale
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DGravityScaleIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DGravityScaleIcall(EntityHandle, value);
            }
        }
    }

    public float LinearDamping
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DLinearDampingIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DLinearDampingIcall(EntityHandle, value);
            }
        }
    }

    public float AngularDamping
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DAngularDampingIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DAngularDampingIcall(EntityHandle, value);
            }
        }
    }

    public Vector2 LinearVelocity
    {
        get
        {
            unsafe
            {
                return ScriptBridge.GetRigidbody2DLinearVelocityIcall(EntityHandle);
            }
        }
        set
        {
            unsafe
            {
                ScriptBridge.SetRigidbody2DLinearVelocityIcall(EntityHandle, value);
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
                ScriptBridge.SetRigidbody2DLinearVelocityXIcall(EntityHandle, value);
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
                ScriptBridge.SetRigidbody2DLinearVelocityYIcall(EntityHandle, value);
            }
        }
    }

    public void AddLinearVelocity(Vector2 deltaVelocity)
    {
        unsafe
        {
            ScriptBridge.AddRigidbody2DLinearVelocityIcall(EntityHandle, deltaVelocity);
        }
    }

    public int GetContactCount(bool includeSensorContacts = true)
    {
        unsafe
        {
            return ScriptBridge.GetRigidbody2DContactCountIcall(EntityHandle, includeSensorContacts);
        }
    }

    public bool HasContactWith(Entity other, bool includeSensorContacts = true)
    {
        unsafe
        {
            return ScriptBridge.HasContactWithEntityIcall(EntityHandle, other.Handle, includeSensorContacts);
        }
    }

    public Entity[] GetContactEntities(bool includeSensorContacts = true)
    {
        unsafe
        {
            int count = checked((int)ScriptBridge.GetContactEntityCountIcall(EntityHandle, includeSensorContacts));
            if (count == 0)
                return [];

            Entity[] result = new Entity[count];
            for (int i = 0; i < count; i++)
                result[i] = new Entity(ScriptBridge.GetContactEntityAtIcall(EntityHandle, includeSensorContacts, checked((uint)i)));
            return result;
        }
    }
}
