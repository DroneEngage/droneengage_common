# Changelog

Major changes to `de_common` — the shared C++ library vendored into every
DroneEngage module (module base, message parser, UDP bus, config, helpers).
Newest first.

## [Unreleased]

- `de_databus/de_cpu_monitor.{hpp,cpp}`: board CPU monitor moved here
  from de_comm (GL-0032 logic, unchanged API in `de::comm`): `CCpuMonitor`
  FREE<->SUPER_BUSY hysteresis, `deParseProcStatCpu`, `deCpuUsagePercent`.
  New: `deParseProcSelfStatCpu` / `deProcessCpuUsagePercent` (process CPU
  for the 6547 `pu` field), `deBoardId()` (`DE_BOARD_ID` > machine-id >
  hostname), `deReadProcStatCpu` / `deReadProcSelfStatCpu` sampler
  wrappers (`DE_PROC_STAT_PATH` / `DE_PROC_SELF_STAT_PATH` overrides),
  and `cpu_monitor` config keys `report_s` (10) / `summary_s` (60, 0 =
  off) (GL-0037).

## [10.13.0] - 2026-10-18

- New intermodule message `TYPE_AndruavMessage_MODULE_CPU_STATUS` (6547):
  MODULE_TO_MODULE only (module -> de_comm, `internal_message=true`,
  never relayed). Fields `bd` (board id), `u` (board CPU %), `pu` (process
  CPU %, optional), `s` (CPU_BOARD_STATE_FREE/SUPER_BUSY), `ev`
  (CPU_REPORT_EVENT_PERIODIC/BECAME_SUPER_BUSY/BECAME_FREE) (GL-0036).
- 1008: `MI` 1/2 re-documented as UNIT-level CPU transitions; new
  `MESSAGE_ID_CPU_SUMMARY` (3, `NT=NOTIFICATION_TYPE_INFO`) for the
  periodic unit summary; new optional `CP` object (unit + per-board CPU
  detail) present only when `MI` is a CPU id. Contract only — no
  behaviour change.

## [10.12.0] - 2026-10-18

- `TYPE_AndruavMessage_Error` (1008) gains an optional `MI` int field
  (message_ID): a machine-readable event id so the GCS can act on an
  event without parsing `DS`. Absent/0 = none; all existing senders
  omit it (GL-0031).
- New `MESSAGE_ID_*` constants: `MESSAGE_ID_CPU_SUPER_BUSY` (1,
  `NT=NOTIFICATION_TYPE_WARNING`) and `MESSAGE_ID_CPU_FREE` (2,
  `NT=NOTIFICATION_TYPE_NOTICE`), plus `ERROR_SYSTEM_LOAD` (30) as the
  `EN` for unit system-load events. Contract only — no behaviour change.

## [10.11.0] - 2026-10-14

- Per-camera OSD routing helper (CA-0023, epic CA-0022):
  `helpers/overlay_target.hpp` `de::camera::resolveOverlayTarget` resolves
  the `t` target a module stamps on `CAMERA_OVERLAY_ACTION` from its
  `camera` config section (`overlay_target` > `cam_id` — caller arg or
  `camera.cam_id` — > `output_video_device_name` >
  `output_video_device` > `source_video_device_name` >
  `source_video_device`; empty = legacy all-cameras). The
  `CAMERA_OVERLAY_ACTION` doc block now lists `t` matching against
  unique_name, local_name, V4L2 card name and `/dev/videoN`.

## [10.10.0] - 2026-09-30

- Phase-4A coordination-fabric message IDs (TASK-P4A-01): `DE_EVENT`
  (1090, reliable fleet event envelope), `DE_EVENT_ACK` (1091), `DE_ROSTER`
  (1092, latest_wins on the server), `DE_WORLD` (1093) and
  `DE_OP_CONTROL` (1094). Wire formats are normative in
  `Tasks/mission_planner/PROTOCOL.md`.

## [10.9.0] - 2026-09-30

- Phase-3 capability layer (TASK-P3-01): `cap_schema` (de.cap/1 advert validation, param checks with defaults, list-of-types params such as `["string","int"]`, SHA-256 advert hash), capability message IDs (`MODULE_CAPABILITIES` 6542, `CAPABILITY_INVOKE` 6543, `CAPABILITY_RESULT` 6544, `MODULE_STATE` 6545), and the `CModule` / `CFacade_Base` helpers `setCapabilities`, `onInvoke`, `publishState`, `fireEvent`. Invokes run on a worker thread with a deadline and are idempotent by id.
- Review fixes: min/max limits also apply to list-of-types params; an action's `deprecated_alias` resolves to its current name before the handler is called (`capCanonicalAction`).

## [10.8.1] - 2026-09-29

- The capability invoke handler receives the invoke id.

## [10.8.0] - 2026-09-27

- Release of the Phase-2 message IDs listed under 10.7.3 (TASK-P2-01).

## [10.7.3] - 2026-09-27

- Added the Phase-2 mission-planner message IDs (TASK-P2-01): `DE_Package_Upload` (1085), `DE_Supervisor_Action` (1086), `DE_Supervisor_Status` (1087), `DE_Supervisor_Journal` (1088) for GCS↔unit traffic, and `NAV_ADAPTER_ACTION` (6540) / `NAV_ADAPTER_STATUS` (6541) for the de_comm↔de_mavlink nav adapter. Wire formats are normative in `Tasks/mission_planner/PROTOCOL.md`. 6539 was already taken by `CAMERA_OVERLAY_ACTION`, so the nav adapter pair moved to 6540/6541.
- Earlier 10.7.x: `onConfigUpdated` hook, camera overlay action messages, heartbeat thread-join fix, `CAMERA_OVERLAY_ACTION_MOVE_SHAPE`.

## [10.7.0] - 2026-09

- Added monotonic-clock time helpers (`get_time_usec_monotonic`) so elapsed-time checks are not affected by system clock changes.
- Added MAC-address and elapsed-time helper utilities.
- Ported helpers that were previously inlined inside `de_comm`.
- Added the `RemoteExecute_Result` message type (1084) and its result codes.

## [10.6.0] - 2026-09

- Added precision-landing calibration/cancel messages and `PRECLAND_REASON_*`/`PRECLAND_CALIB_STATUS_*` codes.
- Fixed permission bit definitions.

## [10.5.x] - 2026-09-06

- The unix socket falls back from `/run/de_comm` to `/tmp/de_comm` at runtime when not running as root, and the socket directory is auto-created.
- Memory health reports a rolling-window peak RSS.

## [10.4.0] - 2026-09-01

- Config files now report JSON parse errors with line/column highlighting.

## [10.3.0] - 2026-08-31

- Moved the unix domain socket from `/tmp` to `/run/de_comm/`.

## [10.2.0] - 2026-08-29

- Added module health monitoring with an auto-scheduled memory-status heartbeat.
- Merged telnet/remote-command protocol definitions.

## [10.1.0] - 2026-08-25

- Added `applyLocalOverrides` + deep merge: `*.local` files override the module config.

## [10.0.x] - 2026-07/08

- Added the IR MI48 camera module class and message types.
- Added the `SOUND_LIST` (6530) and `DRONE_FENCE_ACTION` message types.
- Added smart `s2s_udp_packet_size` handling and a databus chunk-size guard.

## [9.x] - 2026-03 and earlier

- Added the base message-parser class and shared default-command/config-action handling used by all modules.
- Added the module configuration template schema.
