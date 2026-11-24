# M5 Design

## 1. M4 fixes

None. My M4 and CR-1 tests all still passed. `CorpusBuilder`, which started a thread per build, is replaced by `BuildWorker`.

## 2. Threads and shared data

There are three threads:

- **GUI thread:** `MainWindow`, the widgets, every `CorpusState` object, and the `BuildWorker` calls `submit()`, `cancel()`, `take()` and the destructor.
- **Build worker:** created in the `BuildWorker` constructor and joined in its destructor. It only runs `run()` and `build()`.
- **IPC thread:** `RemoteAddServer::serve()`, the same as in M4.

The GUI and worker threads share `pending_`, `done_`, `wanted_` and `stop_` in `BuildWorker`. All four are only read or written while holding `mutex_`. The worker waits on the condition variable `wake_` with a predicate that checks them, so a notify can't get lost. A request carries its own `Workspace` copy and the finished `ProcessingCore` is moved out through `done_`, so nothing else is shared. `RemoteAddServer::stop_` is a `std::atomic<bool>`, and its fd and path never change after its thread starts, as in M4.

## 3. States

| State | Meaning | Add / remote add | Build | Reset | Build done | Build failed |
|---|---|---|---|---|---|---|
| `EmptyState` | no documents | → Unbuilt | (disabled) | → Empty | – | – |
| `UnbuiltState` | documents, no corpus | → Unbuilt | start → Building | → Empty | – | – |
| `BuildingState` | worker is building | cancel → Unbuilt | (disabled) | cancel → Empty | → Current | → Failed |
| `CurrentState` | corpus matches workspace | → Unbuilt ("Out of date") | start → Building | → Empty | – | – |
| `FailedState` | last build failed | → Unbuilt | start → Building | → Empty | – | – |

Each handler returns the next state, or `nullptr` to stay. `MainWindow::change_state()` swaps it in and calls `enter()`. Exit isn't a state change: `~MainWindow` destroys `worker_` (which joins), then the state.

## 4. Trace: Reset during a long build

Run with `--build-delay-ms 500`:

1. **GUI:** Build Corpus → `build_corpus()` → `UnbuiltState::build_requested()` → `start_build()` → `BuildWorker::submit()`. This sets `wanted_` to the new id, stores `pending_` and notifies. The state becomes `BuildingState`.
2. **Worker:** `run()` wakes up and takes the request. `build()` waits in `wake_.wait_for(500 ms, should_stop)` for each document.
3. **GUI:** Reset → `reset_application()` → `BuildingState::reset()` → `cancel_build()` → `BuildWorker::cancel()`. This sets `wanted_ = 0` and notifies. The state becomes `EmptyState`.
4. **Worker:** `wait_for` wakes right away because `should_stop()` is now true. `run()` sees the id is no longer wanted, throws the result away, and goes back to waiting.
5. **GUI:** the next Build starts immediately; the worker isn't stuck on the old one.

## 5. Decisions

1. **Build during a build:** not allowed. `BuildingState::can_build()` returns false, so the button is disabled, and `build_requested()` does nothing in that state anyway.
2. **Remote add mid-build:** treated exactly like Add Document. `add_document_file()` calls `BuildingState::workspace_changed()`, which cancels the build. The user sees the new document, the corpus shows "Not built" (or "Out of date"), and Build Corpus is enabled again.
3. **Shared data:** four values under one mutex: the waiting request, the wanted id, the stop flag and the finished result. Each one carries a different message between the threads, so none can be dropped. They're under one lock so a cancel can't happen between the worker checking `should_stop()` and starting to wait. 

## 6. Fault switches

- `AIWS_FAULT_NO_CANCEL` is in `BuildWorker::should_stop()`: it only checks `stop_`, so an unwanted build keeps running. `reset_stops_long_build`, `add_stops_long_build` and `remote_add_mid_build_cancels_it` catch it.
- `AIWS_FAULT_SILENT_SHUTDOWN` is in `~BuildWorker()`: it skips `notify_all()`, so an idle worker never wakes up and Exit hangs. `exit_when_idle_is_quick` catches it, then the test run hits the 30-second CTest timeout.

## 7. AI-use disclosure

I utilized Claude as an assistant for testing, debugging, reviewing documentation, and getting guidance on better approaches and code structure when needed.
