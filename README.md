![UE 5.8](https://img.shields.io/badge/UE-5.8-darkgreen)

# PCGEx | Inline Settings

**Inline, instanced PCG settings.**  
A small struct to create inline, instanced PCG settings that can be exposed as graph parameters.

`FPCGExInlineSettings` owns an inline instance of any non-abstract PCG settings class, or references a settings asset
instead. Wherever PCG exposes struct members -- node override pins, *Get Graph Parameter*, subgraph pins -- it shows up
as a single soft object path, `Settings`, pointing at the effective settings object. Feed that path to the stock PCG
*Proxy* node's `Settings` pin to run it.

## Usage

As a graph parameter: add a parameter of the `FPCGExInlineSettings` struct type, pick a class (optionally restrict it
with *Allowed Class*), edit the settings inline, then drop the parameter into the graph.

In C++:

```cpp
UPROPERTY(EditAnywhere, Category = "Settings", meta = (PCG_Overridable))
FPCGExInlineSettings Sampler{UPCGSurfaceSamplerSettings::StaticClass()};
```

- The allowed class passed to the constructor restricts the picker; it is only user-editable on graph parameter definitions.
- **External** takes precedence over the inline instance. Setting it greys the inline properties out.
- On graph instances and components, inline settings still shared with the parent graph are read-only until you click
  **Make Local Copy**.
- Code that edits the struct should use `SetInstance` / `SetExternal` (or call `SyncSettings`) so `Settings` stays in sync.
