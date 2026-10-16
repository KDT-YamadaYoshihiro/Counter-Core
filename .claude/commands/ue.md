---
description: Start (or connect to) the Unreal Editor for this project, then do the task
argument-hint: [task]
---

1. Call `ue_open_editor` (a no-op when the editor already answers; a first
   build can take minutes). If it fails, show its `message` / `errors` and stop.
2. Reply with one line: `connected: <uproject>`.
3. Task: $ARGUMENTS
   If that is empty, ask "What should I do in Unreal?" in one line and stop.
