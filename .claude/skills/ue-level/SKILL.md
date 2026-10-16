---
name: ue-level
description: "Edit the open Unreal level with the ue_* tools: actors, transforms, component properties, and landscapes (sculpt, ramps, paint layers, heightmaps)."
---

# ue-level

Tools not in your tool list are reachable with `ue_find_tools` + `ue_call`.

### Level

`ue_list_actors` before touching anything — labels are not unique the
way you expect. `ue_spawn_actor`, `ue_delete_actor`,
`ue_set_actor_property` and `ue_set_actor_transform` act on the open
level; save it with `ue_save_asset`. For a component, a nested struct
member or an array element use `ue_set_property` with
`Label.Component`, `A.B` or `Tags[0]`.

Landscape: `ue_create_landscape` (flat or from a 16-bit heightmap),
then brush strokes with `ue_sculpt_landscape` (`noise` for natural
relief), straight roads with `ue_ramp_landscape`, and
`ue_paint_landscape` (add the layer first with
`ue_add_landscape_layer`, using a name from `materialLayers` in
`ue_get_landscape_layers`).
Check results with `ue_get_landscape_height` /
`ue_get_landscape_weight` rather than assuming. Remove one with
`ue_delete_landscape` — `ue_delete_actor` leaves its streaming proxies.
