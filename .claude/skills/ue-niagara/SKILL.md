---
name: ue-niagara
description: "Build and edit Unreal Niagara particle systems with the ue_* tools: emitters, modules, inputs, dynamic inputs, user parameters, renderers."
---

# ue-niagara

Tools not in your tool list are reachable with `ue_find_tools` + `ue_call`.

### Niagara

Read the system with `ue_get_niagara_system` first and use the
emitter / module / input names exactly as listed. Find emitter
templates, modules and dynamic inputs with `ue_search_assets` (class
NiagaraEmitter / NiagaraScript, path /Niagara/DefaultAssets/Templates,
/Niagara/Modules, /Niagara/DynamicInputs). Typical loop:
`ue_create_niagara_system` (optionally with templates), add an
emitter from a template, add or remove modules, set inputs whose
`settable` is true, `ue_compile_niagara_system`, then `ue_save_asset`.
Inputs marked `linked` are driven by the dynamic input named in
`dynamicInput`: set its own inputs with `ue_set_niagara_input` (module =
that name), replace it after `ue_reset_niagara_input`, or add one with
`ue_set_niagara_dynamic_input` (e.g. a random range). Renderer settings
such as the material: `ue_set_property` on a path from `rendererPaths`. Values the game
should change at runtime belong in user parameters
(`ue_set_niagara_user_parameter`), wired to inputs with
`ue_link_niagara_input`.
