![UE 5.7](https://img.shields.io/badge/UE-5.7-darkgreen)

# PCGEx | Inline Settings

**Inline, instanced PCG settings.**  
A small struct to create inline, instanced PCG settings that can be exposed as graph parameters.

`FPCGExInlineSettings` owns an inline instance of a PCG settings class (any class the PCG palette offers, plus
Blueprint elements), or references a settings asset instead. Wherever PCG exposes struct members -- node override pins,
*Get Graph Parameter*, subgraph pins -- it shows up as a single soft object path, `Settings`, pointing at the effective
settings object. Feed that path to the **PCGEx | Proxy** node (or the stock PCG *Proxy*) `Settings` pin to run it.

## PCGEx | Proxy

A better copy of the stock PCG *Proxy* node, made for inline settings:

- **Interface** picks where the pins come from: a *Settings Class* (concrete, or an abstract class tagged
  `UCLASS(Abstract, meta=(PCGExProxyInterface))` -- PCGEx tags its factory provider bases this way), or a *Blueprint
  Element* class. Abstract interfaces are templates: their Required inputs become optional, the concrete `Settings`
  decides.
- **Settings** is the object to run; override it from an inline settings parameter's `Settings` path.
- **Extra Input Pins** are user-declared pins forwarded to the inner settings' pin of the same label when the data type
  fits (subtype rule). Labels the interface or the proxy already use are ignored. Inner per-parameter override pins are
  valid targets.
- The proxy's `Overrides` attribute set reaches the inner only when one of its attributes names an inner parameter; the
  proxy's own override pins (`Settings`, ...) never do.
- Inner output is routed to the interface's output pins (a single pin takes everything, tagged with the inner label);
  undeclared labels are dropped with a warning.
- Inner pausing (async work, dynamic dependencies), abort, main-thread needs and GC references are bridged.
  *Pause Wake* chooses between a next-tick poll (default) and re-dispatch.
- Not supported through the proxy: subgraph/loop settings, GPU settings, and inner elements that assume a graph node
  (`Context->Node`). Inner errors are logged but do not show on the proxy node. Blueprint element hosting is wired but
  not yet runtime-tested.

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
- Which inline properties show is set in **Project Settings > Plugins > PCGEx | Inline Settings** (base PCG node
  properties hidden except `Seed` by default; extra hidden properties/categories). The eye button on a graph parameter
  definition overrides that per parameter (always show / always hide, by property or category).
- Code that edits the struct should use `SetInstance` / `SetExternal` (or call `SyncSettings`) so `Settings` stays in sync.
- Code that *reads* the struct at execution should call `Resolve()` (the object behind `Settings`, no load) rather than
  `Instance`: PCG's transient override copies carry a nested duplicate in `Instance` while `Settings` keeps the
  persistent path. An unloaded External asset resolves to null; load it on the game thread first.  

---

## Thanks

Special thanks to [TynannicGoat](https://github.com/mharris382) who keeps having interesting problems to solve
