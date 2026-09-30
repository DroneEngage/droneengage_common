# Changelog

Major changes to `de_common` — the shared C++ library vendored into every
DroneEngage module (module base, message parser, UDP bus, config, helpers).
Newest first.

## [10.9.0] - 2026-09-30

- Phase-3 capability layer (TASK-P3-01): `cap_schema` (de.cap/1 advert validation, param checks with defaults, list-of-types params such as `["string","int"]`, SHA-256 advert hash), capability message IDs (`MODULE_CAPABILITIES` 6542, `CAPABILITY_INVOKE` 6543, `CAPABILITY_RESULT` 6544, `MODULE_STATE` 6545), and the `CModule` / `CFacade_Base` helpers `setCapabilities`, `onInvoke`, `publishState`, `fireEvent`. Invokes run on a worker thread with a deadline and are idempotent by id.
- Review fixes: min/max limits also apply to list-of-types params; an action's `deprecated_alias` resolves to its current name before the handler is called (`capCanonicalAction`).

## [10.8.1] - 2026-09-29

- The capability invoke handler receives the invoke id.

## [10.8.0] - 2026-09-27

- Release of the Phase-2 message IDs listed under 10.7.3 (TASK-P2-01).

## [10.7.3] - 2026-09-27

- Added the Phase-2 mission-planner message IDs (TASK-P2-01): `DE_Package_Upload` (1085), `DE_Supervisor_Action` (1086), `DE_Supervisor_Status` (1087), `DE_Supervisor_Journal` (1088) for GCS↔unit traffic, and `NAV_ADAPTER_ACTION` (6540) / `NAV_ADAPTER_STATUS` (6541) for the de_comm↔de_mavlink nav adapter. Wire formats are normative in `Tasks/mission_planner/Phase-2-Tasks/PROTOCOL.md`. 6539 was already taken by `CAMERA_OVERLAY_ACTION`, so the nav adapter pair moved to 6540/6541.
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
