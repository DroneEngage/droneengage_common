# Changelog

Major changes to `de_common` — the shared C++ library vendored into every
DroneEngage module (module base, message parser, UDP bus, config, helpers).
Newest first.

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
