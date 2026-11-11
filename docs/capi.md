# C ABI

Header: `include/football/capi.h`. Shared library: `simcore` (`libsimcore.so`
on Linux, `simcore.dll` on Windows). C linkage, `cdecl` on Windows.

## Models and ownership

`include/football/models.h` is the single source for the model layouts. It is a
C header with no simulation/library dependencies. Under `#ifdef __cplusplus` it
also declares aliases (`football::pitch::PitchModel` and
`football::ball::BallModel`). The aliases are the exact same types as
`football_pitch_model` and `football_ball_model`, not wrappers.

Callers construct model values themselves; the C API ships no default-value
helpers. C and C++ can build them without linking `simcore`:

```c
#include "football/models.h"

/* Inside a function: */
football_pitch_model pitch = {105.0f, 68.0f, 0.04f, 1.6f, 0.015f};
football_ball_model ball = {0.43f, 0.11f, 0.02f};
pitch.length = 100.0f;
```

`Ball()` / `Pitch()` in C++ use the same explicit values as their default
argument; there are no default-value functions to import.

Neither C nor C++ model fields have implicit physical defaults. In C++, `{}`
produces zeros and an uninitialized local has indeterminate fields. Model values
are not validated; callers supply physically meaningful values. `football_init`
passes the shared model types directly to `Env`, which copies them. There is no
per-field C-to-C++ conversion.

## Environment lifecycle

The library owns exactly one environment. There is no handle: callers invoke
`football_init` / `football_shutdown` and the library keeps the C++ `Env`
internally.

- `football_init(pitch, ball)` constructs the environment from copied models.
  Initializing while already initialized returns `FOOTBALL_INVALID_ARGUMENT`
  with the "already initialized" diagnostic; call `football_shutdown` first.
- `football_shutdown()` destroys the environment and is safe to call at any
  time, including repeatedly and when nothing is initialized.
- The single environment is global shared state. Calls that touch it (init,
  shutdown, and future stepping/state operations) must be externally serialized
  by the caller; the adapter does not lock.

Only configuration and lifecycle are exposed for now. This API does not yet
provide stepping, state access, or contact reporting.

## ABI and errors

- `football_status` is `int32_t`, not a compiler-sized enum. Zero is success.
- The pitch struct is five sequential 32-bit floats (20 bytes); the ball struct
  is three sequential 32-bit floats (12 bytes). Use natural alignment, not custom
  packing. Future fields require a new ABI rather than extending these in place.
- C structs have no implicit defaults: `{0}` does not supply physical defaults.
- Model values are passed through without range or finiteness checks. Nonsensical
  values may produce divisions by zero, NaNs, or meaningless simulation results.
- No C++ exceptions cross this boundary. NULL arguments, allocation failures,
  and unexpected exceptions map to distinct status constants.
- `football_last_error()` returns a library-owned, thread-local UTF-8 diagnostic
  (up to 511 bytes). Copy it before another status-returning call on that thread.
  Successful status-returning calls clear it; shutdown and last-error do not.

## C# P/Invoke example

No C++ classes cross the boundary. Match the structs using sequential layout and
statuses using `int`. Ensure the native library matches the process architecture
and is on the native library search path.

```csharp
using System;
using System.Runtime.InteropServices;

[StructLayout(LayoutKind.Sequential)]
public struct PitchModel
{
    public float Length, Width, QuadraticResistance, GroundDeceleration, AirDrag;
}

[StructLayout(LayoutKind.Sequential)]
public struct BallModel
{
    public float Mass, Radius, MagnusCoefficient;
}

public static class Native
{
    private const string Library = "simcore";

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    public static extern int football_init(in PitchModel pitch, in BallModel ball);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    public static extern void football_shutdown();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
    public static extern IntPtr football_last_error();

    public static void Check(int status)
    {
        if (status != 0)
            throw new InvalidOperationException(
                Marshal.PtrToStringUTF8(football_last_error()));
    }

    public static void Example()
    {
        var pitch = new PitchModel {
            Length = 100.0f, Width = 68.0f, QuadraticResistance = 0.04f,
            GroundDeceleration = 1.6f, AirDrag = 0.015f
        };
        var ball = new BallModel { Mass = 0.43f, Radius = 0.11f, MagnusCoefficient = 0.02f };
        Check(football_init(in pitch, in ball));
        try
        {
            // Future simulation operations use the single environment here.
        }
        finally
        {
            football_shutdown();
        }
    }
}
```

The native ABI is exercised by `unittests/capi/capi_test.c`, compiled as C11,
including layout checks, default/custom configurations, lifecycle, NULL inputs,
re-initialization behavior, and error reset. The C# snippet is usage
documentation, not an automated test.
