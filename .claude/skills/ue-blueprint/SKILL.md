---
name: ue-blueprint
description: "Edit Unreal Blueprints, Widget Blueprints (UMG) and Animation Blueprints with the ue_* tools: read graphs, add nodes, wire pins, variables, compile, save, diff."
---

# ue-blueprint

Tools not in your tool list are reachable with `ue_find_tools` + `ue_call`.

### Blueprints

Read before writing: `ue_get_blueprint` for variables and the graph
list, then `ue_get_graph` for one graph's nodes and connections.
`ue_search_nodes` resolves a function/event name to something
`ue_add_node` accepts — do not invent node names.

Node references are the `guid` (stable across edits) or the per-read
`N1`, `N2`, … ids. Pin endpoints are `"<node>.<pin>"`.

The loop for any change:

1. `ue_snapshot_blueprint` if the change is large enough to review.
2. Edit (`ue_add_node`, `ue_connect_pins`, `ue_add_variable`, …).
3. `ue_compile_blueprint` — no edit tool compiles. If `succeeded` is
   false, read `messages`, fix, recompile.
4. `ue_save_blueprint` only after a clean compile (it refuses with
   `blueprint.compile_errors` otherwise). `ue_delete_blueprint` refuses
   with `blueprint.referenced` while other assets use it — show the
   user the referencers instead of forcing it.
5. `ue_diff_blueprint` to show the user what actually changed.

Widget Blueprints (UMG): `ue_get_widget_tree`, then `ue_add_widget` /
`ue_move_widget` / `ue_remove_widget` / `ue_bind_widget`. Widget and
slot properties (text, anchors, padding) go through `ue_set_property`
on the node's `objectPath` / `slotPath`. Compile and save as above.

Animation Blueprints: add graph nodes with `ue_add_node` type `Node`
(`AnimGraphNode_StateMachine`, `AnimStateNode`, ...); wiring one state's
Out to another's In creates a transition, whose rule graph is named
`From->To` (listed as `transition` by `ue_get_blueprint`).
