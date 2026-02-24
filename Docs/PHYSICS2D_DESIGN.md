# Physics2D Design

This document describes the **physics architecture** and **design choices** for the 2D physics system (Box2D, `LT_ENABLE_PHYSICS2D`).

## Multiple worlds per scene

- A **Scene** now owns a collection of `Physics2DWorld` instances.
- The number of worlds is configured by `Physics2DSettings.WorldCount` (serialized in scene JSON, clamped to a safe range).
- Each `Rigidbody2DComponent` has an authored `PhysicsWorldSlot` value. Bodies only simulate and collide inside their assigned world slot.
- Scripts can still raycast through `Physics2D::Raycast`; engine-side world queries now search across scene worlds and return the closest hit.
- In the editor, world count can be adjusted in the **Physics 2D Diagnostics** panel and body world assignment is exposed in the Rigidbody 2D inspector.

This keeps the default simple (`WorldCount = 1`, `PhysicsWorldSlot = 0`) while enabling isolated parallel simulations when needed.

## Physics handle types and layout stability

- **Rigidbody2DComponent**, **BoxCollider2DComponent**, **CircleCollider2DComponent**, and joint components store **runtime handles** (`Physics2DBodyHandle`, `Physics2DShapeHandle`, `Physics2DJointHandle`) that refer to Box2D objects when `LT_ENABLE_PHYSICS2D` is defined.
- These handle types are **layout-stable** across build configurations:
  - When **physics is enabled**, the handles are typedefs to Box2D’s `b2BodyId`, `b2ShapeId`, `b2JointId` (size/alignment match is asserted in `Physics2DWorld.cpp`).
  - When **physics is disabled** (`LT_ENABLE_PHYSICS2D` not defined), the same component structs use **opaque, layout-compatible** stand-in types (same size and alignment) so that component layout is identical.
- **ScriptCore** and the engine share the same EnTT registry and the same `Components.h`; ScriptCore may be built with physics disabled. Layout stability ensures that component structs are ABI-compatible: no garbage data or crashes when ScriptCore touches physics components in a build where physics is off. The design is **solid** for this use case.

See `Limitless/Source/Scene/Components.h` (physics handle block) and `Limitless/Source/Physics/Physics2DWorld.cpp` (static_asserts).

## Files

- `Limitless/Source/Scene/Scene.h` — Scene owns `std::vector<std::unique_ptr<Physics2DWorld>>`
- `Limitless/Source/Scene/Components.h` — Physics handle typedefs / stand-in structs
- `Limitless/Source/Physics/Physics2DWorld.{h,cpp}` — World lifecycle, step, rebuild
- `Limitless/Source/Scene/SceneRuntimeLifecycle.cpp` — Physics init/step tied to scene
