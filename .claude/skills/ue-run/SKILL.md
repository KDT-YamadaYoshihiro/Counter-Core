---
name: ue-run
description: "Verify Unreal changes with the ue_* tools: Play-In-Editor, logs, screenshots, automation tests, compile-all and other long-running jobs."
---

# ue-run

Tools not in your tool list are reachable with `ue_find_tools` + `ue_call`.

### Run and observe

`ue_play_in_editor` / `ue_stop_play` / `ue_play_status` drive PIE.
Verify behaviour instead of asserting it works. `ue_get_logs` takes a
severity/category filter and an incremental `since` — pass back the
returned `nextSince` rather than re-reading the whole tail.
`ue_take_screenshot` needs a rendering editor. `ue_wait_for_event`
blocks on a topic instead of polling.

### Long-running jobs

`ue_compile_all_blueprints`, `ue_run_automation_tests`, `ue_run_pie` and
`ue_build_level` block until done and return the collected
errors/warnings — use them for verification after a broad change.
A job status of `succeeded` only means the run finished: read
`result.failed` (tests, compile-all) or `result.succeeded` (level build).
`ue_start_job` / `ue_job_status` / `ue_list_jobs` are the generic
escape hatch.
