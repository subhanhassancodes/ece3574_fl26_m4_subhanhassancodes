# M4 Decision Record

## 1. Threads and ownership

| Thread | Runs |
|---|---|
| GUI | `MainWindow`, all widgets, `CorpusBuilder::start/take/job_done`, `RemoteAddServer::start/stop/deliver` |
| build worker (one per job) | `CorpusBuilder::run` |
| IPC server | `RemoteAddServer::serve/handle_client` |

`MainWindow` owns `builder_` and `remote_server_` as `unique_ptr`s. `CorpusBuilder` owns each `BuildJob` (thread, future, cancel flag) in `jobs_`. `take()` joins a job after its `job_done`, which is the worker's last action. `~CorpusBuilder` sets every `cancel` and joins all jobs. `~RemoteAddServer` calls `stop()`, which joins and removes the socket file. `remote_server_` is declared last, so it stops first.

**Constraint 5.** Shared mutable data: `BuildJob::cancel` (GUI writes, worker reads) and `RemoteAddServer::stop_` (GUI writes, server reads) are `std::atomic<bool>`. The promise/future state (worker sets, GUI gets) is synchronized by the standard library. Workers get a copy of the `Workspace`. `version`, `listen_fd_` and `socket_path_` are written before the thread starts and never change. Results reach the GUI only through queued `QMetaObject::invokeMethod`.

## 2. How a finished build is judged valid

`workspace_version_` goes up in `workspace_changed()` on every Add and Reset. `build_corpus()` passes the current version to `CorpusBuilder::start()`. `MainWindow::build_finished()` accepts a result only if `result.version == workspace_version_`. A stale result, success or failure, is dropped. A valid one is move-assigned into `processing_core_`, or its error is shown as "Build failed". Build is disabled while a build for the current version runs.

## 3. Changes since the checkpoint

- Add and Reset no longer cancel the running job; only `~CorpusBuilder` cancels. The version check is the single decision point and the extra work is bounded.
- Build failure no longer opens a modal `QMessageBox`; it shows in the corpus and status labels, so nothing blocks the GUI or remote adds.
- Notification uses `invokeMethod` by name with `Q_ARG` because Qt 6.2 has no functor overload with arguments.

## 4. CR-1 impact

New: `RemoteAddServer`, `remote_protocol` (shared with `tools/aiws_send.cpp`), `MainWindow::start_remote_add()`, and the `--ipc-socket` option in `main.cpp`. `add_document()` now calls `add_document_file()`, which is also connected to `RemoteAddServer::add_requested`. Nothing else changed, since a remote add is the same workspace change as a local one.

**Message format.** One tag byte, a 4-byte big-endian length N, then N payload bytes. Request: tag `0x41` (`'A'`), payload = absolute UTF-8 path, which `aiws_send` builds from its working directory. Reply: tag `0x00` (ok, N = 0) or `0x01` (rejected, payload = UTF-8 reason). N ≤ 65536. The length prefix handles spaces, UTF-8 and newlines; `read_message()` loops until all bytes arrive (5 s limit). The server checks readability and replies before posting to the GUI. It polls every 100 ms, so it stops within 500 ms. SIGINT/SIGTERM quit normally.

## 5. Fault switches

| Macro | Location | Caught by |
|---|---|---|
| `AIWS_FAULT_ACCEPT_STALE` | `MainWindow::build_finished()` | `build_is_stale_after_add`, `build_is_stale_after_reset`, `newer_build_wins_over_older` |
| `AIWS_FAULT_LOSE_FAILURE` | `CorpusBuilder::run()` | `failed_build_is_reported` |

## 6. AI-use disclosure

I utilized Claude as an assistant for testing, debugging, reviewing documentation, and getting guidance on better approaches and code structure when needed.

## Pair programming log

## Pair programming log

- Partner: Shray Gupta 
- Session 1: Nov 8, 9:30–10:15 pm, my code. Walked through CorpusBuilder::run() and MainWindow::build_finished() (the workspace version check), then RemoteAddServer::handle_client() and the message format.
- Session 2: Nov 8, 10:15–11:00 pm, Shray's code. Reviewed BuildJob, MainWindow::finishBuild() and its fault switches, and why RemoteAddServer is declared after MainWindow in main.cpp.
- Role swaps: Session 1: I drove 9:30–9:52 pm, Shray drove 9:52–10:15 pm. Session 2: Shray drove 10:15–10:37 pm, I drove 10:37–11:00 pm.
- My contribution as navigator: I asked whether AIWS_FAULT_ACCEPT_STALE is really caught, since a cancelled stale job returns nullptr and is dropped either way. Shray showed his stale tests use no delay so the worker always finishes a real core, and we built with the macro to confirm the tests fail.
- My partner's contribution as navigator: Shray asked what happens if an older build finishes after a newer one. We ran newer_build_wins_over_older to confirm the older result is dropped by the version check.
- What worked best: The driver explaining each part out loud while the navigator asked "what if" questions about timing, then checking the answer by running a test.