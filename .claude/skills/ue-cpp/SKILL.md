---
name: ue-cpp
description: "Edit and compile Unreal C++ with the ue_* tools: Live Coding vs full rebuild, scaffolding classes and modules, fixing compile errors."
---

# ue-cpp

Tools not in your tool list are reachable with `ue_find_tools` + `ue_call`.

### C++

Use `ue_list_cpp_modules` / `ue_list_cpp_files` / `ue_search_cpp` to
navigate, and `ue_read_cpp_file` / `ue_edit_cpp_file` /
`ue_write_cpp_file` to change source — they stay inside the project and
keep paths project-relative. `ue_scaffold_cpp_class` writes a correct
`.h` + `.cpp` pair; prefer it over hand-written boilerplate.

Compiling: call `ue_apply_code` after editing — it looks at what changed
since the last build and picks Live Coding or a rebuild itself; both
report compiler errors with a source excerpt. Underneath:
a `.cpp` body change goes through `ue_live_compile` with
the editor open (seconds). A new UCLASS/USTRUCT/UPROPERTY, any header
change or a new module needs `ue_rebuild` — it closes the editor,
builds, relaunches and reconnects in one call. Save open assets first
(it refuses with `editor.unsaved_changes`; pass `force` only if the user
agrees to lose them). If the build fails, the editor stays closed and
the result lists `{ file, line, code, message }`: fix those and call
`ue_rebuild` again. Do not push header changes through Live Coding.

Cost order (pick the cheapest that works): data and logic through
Blueprints, `ue_set_property` or `ue_run_python` need no compile; a
`.cpp` body change is a Live Coding patch (~5 s); a header change is a
`ue_rebuild` (~15-20 s, mostly the editor restart). Plan header edits
together and rebuild once rather than after each one.
