<!-- BEGIN ue-cli -->
## Unreal Engine (ue-cli)

The `ue_*` MCP tools drive a **running Unreal Editor**; `uecli <command>` does
the same from a shell. `/ue <task>` starts the editor and begins a task.

- Cannot connect → `ue_open_editor` (opens this project, waits until it
  answers). Do not retry other tools first.
- Only everyday tools are listed. For anything else (landscape, Niagara,
  widgets, components, interfaces, packaging, jobs, …) run
  `ue_find_tools("<words>")`, then `ue_call("<name>", { ...args })` — prefer
  that over Python when a tool exists.
- `.uasset` / `.umap` are binary: never read, grep or edit them as text.
- Edits live in the editor's memory until a save tool runs.
- Resolve names with `ue_search_assets`; do not guess `/Game/...` paths.
- Keep results small: read one graph at a time; pass `nextSince` back to
  `ue_get_logs` instead of re-reading the log.
- Workflows per area are skills — load the one for the task: `ue-assets`, `ue-blueprint`, `ue-cpp`, `ue-level`, `ue-niagara`, `ue-run`, `ue-python`.
<!-- END ue-cli -->
