---
name: ue-assets
description: "Find, read and change Unreal assets and properties (Data Assets, settings, class defaults, materials, imports, packaging) and run editor Python with the ue_* tools."
---

# ue-assets

Tools not in your tool list are reachable with `ue_find_tools` + `ue_call`.

### Session

`ue_ping` / `ue_get_version` confirm the editor, project and plugin are
live — do this once before a long session. `ue_undo` / `ue_redo` revert
the last editor transaction, which is the fastest way out of a bad edit.

`ue_run_python` runs Python in the editor (`import unreal`) with no
compile: use it for anything without a dedicated tool (materials, UMG,
data assets, bulk asset work). `print()` what you need back; check
`succeeded` and read `error` (the traceback) when it is false.

### Assets

`ue_get_project_info` for layout and engine version, `ue_search_assets`
to resolve a name to an asset path before passing it to another tool.
Guessing `/Game/...` paths wastes turns. `ue_save_asset` persists one
package; `ue_import_asset` brings in source files.

`ue_get_properties` / `ue_set_property` read and write any asset,
object or actor by reflection (Data Assets, settings, components,
Blueprint class defaults). Read first and reuse the exact value format
it returns; save the asset after setting.

Materials: edit with `ue_run_python` (MaterialEditingLibrary) and check
the result with `ue_get_material_graph` (also reads material functions).
`ue_package_project` builds a shippable package outside the editor;
save assets first.
