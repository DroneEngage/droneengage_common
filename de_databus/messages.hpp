

/**
 * =====================================================================
 * MESSAGE DOCUMENTATION LEGEND
 * =====================================================================
 * Every TYPE_AndruavMessage_* (and related) constant below is annotated
 * with a short doc block gathered by tracing actual sender/receiver code
 * across all modules + the React webclient (2026 audit). Tags used:
 *
 * @direction
 *   WEB_TO_MODULE      - GCS webclient -> onboard module (a "command")
 *   MODULE_TO_WEB       - onboard module -> GCS webclient (telemetry/status)
 *   BIDIRECTIONAL       - used both ways (often request/response)
 *   MODULE_TO_MODULE    - internal only, never reaches the webclient
 *   DRONE_TO_DRONE      - peer unit <-> peer unit (via comm server/P2P)
 *   DEAD/UNUSED         - constant defined but no sender+receiver pair found
 *
 * @rate  (how often it is actually sent, for FREQUENT/periodic messages)
 *   ON_DEMAND  - one-shot, purely event/user-triggered, no timer
 *   LOW        - periodic, interval >= 5s
 *   MEDIUM     - periodic, interval ~1-5s
 *   HIGH       - periodic/streamed, interval ~100-999ms
 *   REALTIME   - streamed continuously / interval <=100ms / tied to frame rate
 *
 * @discard  (guidance for a link-quality-aware sender: can this specific
 *            message be safely dropped/coalesced -keep-latest-only- when
 *            the connection is congested or low quality?)
 *   YES - latest-value semantics; a newer message fully supersedes an
 *         older one and the receiver tolerates gaps (state snapshots,
 *         telemetry, joystick/position streams).
 *   NO  - must be delivered reliably; it is a command, a one-time event,
 *         part of a chunked/reassembled payload, or a raw data byte
 *         stream where loss corrupts state (e.g. terminal I/O).
 *
 * Field optionality was verified against actual `.contains()`/null checks
 * in the parsing code, not just the sender's intent.
 * =====================================================================
 */


// InterModules command
/**
 * @brief CMD_TYPE_INTERMODULE is used when you want to send a message from a  module to another.
 * This is mainly used to emulate a message comes from an external gcs to a module but is created by another module.
 * i.e. FCB module can emulate take_image even comes from gcs to camera module.
 * Even if you do not use CMD_TYPE_INTERMODULE and uses a command id that is for inter-module commands such as id > 9500
 * then it will be handled by communicator module such as TYPE_AndruavModule_RemoteExecute.
 */
#define CMD_TYPE_INTERMODULE "uv"
#define CMD_TYPE_SYSTEM_MSG  "s"

// JSON InterModule Fields
#define JSON_INTERMODULE_MODULE_ID              "a"
#define JSON_INTERMODULE_MODULE_CLASS           "b"
#define JSON_INTERMODULE_MODULE_MESSAGES_LIST   "c"
#define JSON_INTERMODULE_MODULE_FEATURES        "d"
#define JSON_INTERMODULE_MODULE_KEY             "e"
#define JSON_INTERMODULE_PARTY_RECORD           "f"
#define JSON_INTERMODULE_SOCKET_STATUS          "g"
#define JSON_INTERMODULE_HARDWARE_ID            "s"
#define JSON_INTERMODULE_HARDWARE_TYPE          "t"
#define JSON_INTERMODULE_VERSION                "v"
#define JSON_INTERMODULE_TIMESTAMP_INSTANCE     "u"
#define JSON_INTERMODULE_RESEND                 "z"





/**
 * @brief assume JSON header is never less than 10
 * @details Assume JSON header is never less than 10
 * This is to speed up fiding binary message.
 * @todo Please Confirm.
 */
#define MIN_JSON_HEADER_LEANGTH 10



// Communication Commands

/**
 * @brief Group boradcast
 * @details group broad cast overrides individual.
 * @see Andruav_Communication_Server for details.
 */
#define CMD_COMM_GROUP                  "g"
/**
 * @brief Individual broadcast.
 * @details single target except for the following
 * *_GD_* all GCS
 * *_AGN_* all agents
 * @see Andruav_Communication_Server for details.
 */
#define CMD_COMM_INDIVIDUAL             "i"

/**
 * @brief System command.
 * @details this should be handled by communication server. e.g. task access messages.
 * @see Andruav_Communication_Server for details.
 */
#define CMD_COMM_SYSTEM                 "s"


// Reserved Target Values
#define ANDRUAV_PROTOCOL_SENDER_ALL_GCS         "_GCS_"
#define ANDRUAV_PROTOCOL_SENDER_ALL_AGENTS      "_AGN_"
#define ANDRUAV_PROTOCOL_SENDER_ALL             "_GD_"
#define ANDRUAV_PROTOCOL_SENDER_COMM_SERVER     "_SYS_"


// Andruav Protocol Fields
#define ANDRUAV_PROTOCOL_GROUP_ID               "gr"
#define ANDRUAV_PROTOCOL_SENDER                 "sd"
#define ANDRUAV_PROTOCOL_TARGET_ID              "tg"
#define ANDRUAV_PROTOCOL_MESSAGE_TYPE           "mt"
#define ANDRUAV_PROTOCOL_MESSAGE_CMD            "ms"
#define ANDRUAV_PROTOCOL_MESSAGE_PERMISSION     "p"
#define INTERMODULE_ROUTING_TYPE                "ty"
#define INTERMODULE_MODULE_KEY                  "GU"
#define WAITING_EVENT                           "ew"
#define FIRE_EVENT                              "ef"
#define LINKED_TO_STEP                          "ls"


// SOCKET STATUS
#define SOCKET_STATUS_FREASH 			1   // socket is new
#define SOCKET_STATUS_CONNECTING    	2	// connecting to WS
#define SOCKET_STATUS_DISCONNECTING 	3   // disconnecting from WS
#define SOCKET_STATUS_DISCONNECTED 		4   // disconnected  from WS
#define SOCKET_STATUS_CONNECTED 		5   // connected to WS
#define SOCKET_STATUS_REGISTERED 		6   // connected and executed AddMe
#define SOCKET_STATUS_UNREGISTERED 		7   // connected but not registred
#define SOCKET_STATUS_ERROR 		    8   // Error




/**
 * @brief Module registers itself with the comm broker.
 * @direction MODULE_TO_MODULE (module -> de_comm broker)
 * @rate ON_DEMAND - sent once on connect, plus on-demand resend request
 * @discard NO - identity/registration, must be delivered
 * fields: a module_id, b module_class, c module_messages filter list,
 *  d module_features [T,R], e module_key, s hardware_serial,
 *  t hardware_serial_type, v module_version, z resend flag - all REQUIRED
 */
#define TYPE_AndruavModule_ID                   9100
/**
 * @brief Generic "call this action on another module" RPC envelope.
 * @direction MODULE_TO_MODULE - internal RPC; sub-command carried in nested "C" field (often reuses a TYPE_AndruavMessage_* or RemoteCommand_* id as the verb)
 * @rate ON_DEMAND
 * @discard NO - it is an RPC call, dropping it silently loses the request
 * fields: C int REQUIRED - sub-command/action id
 */
#define TYPE_AndruavModule_RemoteExecute        9101
/**
 * @brief FCB broadcasts vehicle position to other onboard modules (e.g. camera geotagging).
 * @direction MODULE_TO_MODULE (mavlink -> broker -> interested modules; camera_2025, sdr)
 * @rate HIGH - periodic ~300ms (every 30 ticks of the ~10ms main loop)
 * @discard YES - latest position supersedes older ones, internal consumers tolerate staleness
 * fields: la/ln int REQUIRED (lat/lon degE7), a int REQUIRED (abs alt mm),
 *  r int REQUIRED (rel alt mm), ha int OPTIONAL (horiz accuracy mm),
 *  y int OPTIONAL (yaw cdeg)
 */
#define TYPE_AndruavModule_Location_Info        9102



// Andruav Messages

/**
 * @brief Reports a unit's own GPS fix (sent by de_comm for a control-unit; the
 * drone/FCB instead forwards raw MAVLink GPS under TYPE_AndruavMessage_MAVLINK).
 * @direction MODULE_TO_WEB
 * @rate HIGH - periodic ~1000ms (1Hz, comm broker's 10Hz scheduler / 10 ticks)
 * @discard YES - only the latest fix matters, safe to drop under a bad link
 * la: getLatitude() * 1e-7 (float, REQUIRED)
 * ln: getLongitude() * 1e-7 (float, REQUIRED)
 * a: absolute altitude in meter (float, REQUIRED)
 * r: relative altitude in meter (float, REQUIRED)
 * y: yaw in cdeg (int, always 0 - unused placeholder)
 * 3D: GPS 3D fix indicator (int, REQUIRED)
 * SC: satellite count (int, REQUIRED)
 * p: GPS provider, always 0 from sender (int, REQUIRED)
 * c: accuracy (OPTIONAL, defaults 0)
 * t: timestamp (OPTIONAL, defaults now())
 * s: ground speed (OPTIONAL)
 */
#define TYPE_AndruavMessage_GPS                     1002
/**
 * @brief DEAD/UNUSED - defined but never constructed or parsed anywhere in
 * this codebase. Real battery telemetry is forwarded as raw MAVLink
 * BATTERY_STATUS under TYPE_AndruavMessage_MAVLINK (every ~5s from de_mavlink).
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_POWER                   1003
/**
 * @brief Unit identity/state heartbeat - vehicle type, flight mode, GPS mode,
 * name, permissions, module list, etc. Also answerable on-demand.
 * @direction BIDIRECTIONAL - module->web periodic push; web can also request
 *  it via TYPE_AndruavMessage_RemoteExecute{C: TYPE_AndruavMessage_ID}
 * @rate LOW - periodic ~10s heartbeat, PLUS immediate resend on connect,
 *  module restart, or any state change (arm/mode/swarm)
 * @discard YES for the periodic heartbeat refresh (latest state wins) -
 *  but always request/resend after reconnect or a real state change, do
 *  not rely on a dropped heartbeat resolving itself before then
 * fields: VT,GS,VR,B,FM,GM,C,UD,DS,p,dv,m1,T int/bool/string REQUIRED;
 *  TP OPTIONAL; b,FI,AP,FL,AR,SD,x,y,n,o,q,z,a OPTIONAL (only present when
 *  truthy/non-default on the sender side)
 */
#define TYPE_AndruavMessage_ID 	                 1004
/**
 * @brief This command is used to execute a remote command on another unit.
 * @param C command id.
 *      * 1- This command is can me less than 1000 which means it is a specific command need to
 *        be executed by target drone such as RemoteCommand_REQUEST_PARA_LIST, RemoteCommand_MISSION_COUNT ...etc.
 *      * 2- It also can be equal to a command ID such as TYPE_AndruavMessage_ID
 *        in this case the command need to be sent by the target drone, which in this case wwill
 *        be sendID. TYPE_AndruavMessage_RemoteExecute(TYPE_AndruavMessage_ID) === request ID and
 *        target drone should reply with TYPE_AndruavMessage_ID == sendID.
 *                   TYPE_AndruavMessage_RemoteExecute(TYPE_AndruavMessage_HomeLocation) == send Home Location
 *       * Not all command s are implemented.
 * @param Act :bool which used with @param C to activate & deactivate as extra parameter.
 *
 * @direction BIDIRECTIONAL - mostly WEB_TO_MODULE requests; also used MODULE_TO_MODULE
 * @rate ON_DEMAND - one-off request/command per user action or connection bootstrap
 * @discard NO - it is a command/request envelope, not a state snapshot
 * fields: C int REQUIRED - target command id; Act OPTIONAL - context-dependent
 *  sub-action/activate flag; other fields vary by the value of C
 */
#define TYPE_AndruavMessage_RemoteExecute 	   1005
/**
 * @brief Captured still image (binary message: JSON header + raw image bytes).
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - one message per captured image, can repeat as a bounded
 *  burst (see TYPE_AndruavMessage_Ctrl_Cameras' "b" count), not continuous video
 * @discard NO - each image is unique payload data, not a superseding snapshot
 * fields: prv OPTIONAL ("gps" or absent); lat/lng/alt float REQUIRED (0 if no
 *  fix); tim int REQUIRED; trailing binary image bytes REQUIRED (may be
 *  zero-length on capture failure); des/spd/ber/acc OPTIONAL (reserved,
 *  not currently populated by the C++ sender)
 */
#define TYPE_AndruavMessage_IMG                     1006
/**
 * @brief Error/notification report (module lifecycle failures, permission denials, etc).
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - fired only on error conditions, no timer
 * @discard NO - alerts must be delivered
 * fields: EN int REQUIRED (error number); IT int REQUIRED (reporting
 *  component); NT int REQUIRED (severity, see NOTIFICATION_TYPE_*);
 *  DS string REQUIRED (description)
 */
#define TYPE_AndruavMessage_Error                   1008
/**
 * @brief Change flight mode.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - flight-mode command, safety-relevant
 * fields: F unsigned int REQUIRED (dropped if absent) - Andruav flight-mode code
 */
#define TYPE_AndruavMessage_FlightControl           1010
/**
 * @brief List of cameras available on a unit (capabilities, recording state).
 * @direction MODULE_TO_WEB - pushed on state change (record/stream/switch/
 *  WebRTC join-hangup) and as a reply (R=true) to an explicit request
 * @rate ON_DEMAND - event-triggered, no periodic timer
 * @discard NO - reasonably low frequency, treat as authoritative state
 * fields: T array REQUIRED, each {v,ln,id,active,r,p,s,a}; R bool REQUIRED
 *  (true only when replying to an explicit request)
 */
#define TYPE_AndruavMessage_CameraList 		   1012  //RX: {"tg":"GCS1","sd":"zxcv","ty":"c","gr":"1","cm":"i","mt":1012,"ms":"{\"E\":2,\"P\":0,\"I\":\"zxcv\"}"}
/**
 * @brief Drone-side report event (currently only "mission waypoint reached").
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - fired only when a mission waypoint is reached, no timer
 * @discard NO - a discrete milestone event
 * fields: R int REQUIRED (report type, only Drone_Report_NAV_ItemReached=1
 *  defined); P int REQUIRED (mission sequence number reached)
 */
#define TYPE_AndruavMessage_DroneReport             1020
/**
 * @brief WebRTC signaling (SDP offer/answer, ICE candidates, hangup) for camera streaming.
 * @direction BIDIRECTIONAL
 * @rate ON_DEMAND - one message per signaling event
 * @discard NO - SDP/ICE loss breaks call setup; must be delivered
 * fields: w object REQUIRED - {packet, number (target party id), channel};
 *  webclient falls back to the raw payload if w is absent
 */
#define TYPE_AndruavMessage_Signaling               1021
/**
 * @brief Home position (push on FCB update, and reply to an explicit request).
 * @direction BIDIRECTIONAL (module push MODULE_TO_WEB; web can request via RemoteExecute)
 * @rate ON_DEMAND
 * @discard YES - idempotent/re-derivable; if lost, the webclient can simply
 *  re-request it, so it is safe to drop under a congested link
 * fields: T float REQUIRED (lat, deg); O float REQUIRED (lon, deg);
 *  A float REQUIRED (alt, m)
 */
#define TYPE_AndruavMessage_HomeLocation            1022
/**
 * @brief Request/response of geofence list.
 * @direction BIDIRECTIONAL - web requests via RemoteExecute{C:1023}; module replies directly with mt=1023
 * @rate ON_DEMAND
 * @discard NO - a request/response pair, must be delivered
 * fields: fn string OPTIONAL - fence name filter; if absent, all fences of
 *  the party are sent back one by one
 */
#define TYPE_AndruavMessage_GeoFence                1023
/**
 * @brief Applies a fence definition pushed from storage/system ("_sys_") or saved by the web.
 * @direction BIDIRECTIONAL/system-mediated - "_sys_" pushes to module and web on task load; web also writes it when saving fence tasks
 * @rate ON_DEMAND - triggered when tasks/fences are (re)loaded
 * @discard NO - must be applied to be effective, not a snapshot
 * fields: fence-definition object per CGeoFenceFactory schema; "n" string
 *  REQUIRED (fence name, needed to attach)
 */
#define TYPE_AndruavMessage_ExternalGeoFence        1024
/**
 * @brief Fired whenever geofence proximity/in-zone status changes; can repeat while near a boundary.
 * @direction MODULE_TO_WEB
 * @rate MEDIUM - event-driven, can repeat continuously while near a boundary (not a fixed timer)
 * @discard YES - only the latest in/out-zone state matters, older hits can be dropped
 * fields: n string REQUIRED (fence name); z bool REQUIRED (in_zone);
 *  d number OPTIONAL (distance, NaN if absent); o bool/legacy-int OPTIONAL
 *  (should_keep_outside)
 */
#define TYPE_AndruavMessage_GEOFenceHit             1025
/**
 * @brief Mission waypoint list, sent as a manually-chunked burst (max 20 items/chunk, no per-chunk ack).
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - a one-shot burst of several UDP packets triggered by a mission reload request
 * @discard NO - each chunk is required for reassembly; losing ONE chunk
 *  corrupts the whole waypoint list (no per-chunk retransmission exists)
 * fields: n int REQUIRED (item count in this chunk); i int REQUIRED
 *  (WAYPOINT_CHUNK / WAYPOINT_LAST_CHUNK / WAYPOINT_NO_CHUNK marker);
 *  "0".."n-1" object REQUIRED per waypoint item (t,s,a,g,l,h,y, ...)
 */
#define TYPE_AndruavMessage_WayPoints               1027
/**
 * @brief Whether a fence is currently attached to the vehicle.
 * @direction BIDIRECTIONAL - web requests via RemoteExecute{C:1029}; module replies directly
 * @rate ON_DEMAND
 * @discard NO
 * fields: request: fn string OPTIONAL (fence name filter); response:
 *  n string REQUIRED (fence name), a bool REQUIRED (isAttachedToFence)
 */
#define TYPE_AndruavMessage_GeoFenceAttachStatus    1029
/**
 * @brief Arm/disarm the vehicle.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - critical safety command, permission-gated (PERMISSION_ALLOW_GCS_MODES_CONTROL)
 * fields: A bool REQUIRED (arm/disarm); D bool OPTIONAL (force/emergency disarm)
 */
#define TYPE_AndruavMessage_Arm                     1030
/**
 * @brief Change target altitude (routes to DE-Pilot or direct MAVLink takeoff/change-altitude).
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a float/unsigned REQUIRED - target altitude (meters)
 */
#define TYPE_AndruavMessage_ChangeAltitude          1031
/**
 * @brief Land the vehicle.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - critical safety command
 * fields: none (empty payload)
 */
#define TYPE_AndruavMessage_Land                    1032
/**
 * @brief "Fly here" - fly to a guided point. Module replies with TYPE_AndruavMessage_DistinationLocation.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a float REQUIRED (lat); g float REQUIRED (lon); l float/unsigned
 *  OPTIONAL (alt mm, keeps current relative alt if 0/absent); x/y/z OPTIONAL
 *  (velocity components; sent by some callers but NOT consumed by the handler)
 */
#define TYPE_AndruavMessage_GuidedPoint              1033
/**
 * @brief "Circle here" - intended to fly a loiter circle around a point.
 * @direction WEB_TO_MODULE (intended) - DEAD on the receiving side: webclient
 *  sends it (API_do_CircleHere) but drone_engage_mavlink has NO case for it
 *  in fcb_andruav_message_parser.cpp's parseCommand switch, despite being
 *  subscribed in main.cpp's MESSAGE_FILTER. Currently a no-op end-to-end.
 * @rate ON_DEMAND (intended)
 * @discard NO (intended - a command)
 * fields (as sent): a lat, g lon, l alt, r radius, t turns
 */
#define TYPE_AndruavMessage_CirclePoint              1034
/**
 * @brief Yaw the vehicle to a target angle/rate.
 * @direction WEB_TO_MODULE - routed to DE-Pilot or direct MAVLink depending on DE-Pilot active state
 * @rate ON_DEMAND
 * @discard NO
 * fields: A number REQUIRED (target_angle); R number REQUIRED (turn_rate);
 *  C bool REQUIRED (is_clock_wise); L bool REQUIRED (is_relative)
 */
#define TYPE_AndruavMessage_DoYAW                   1035
/**
 * @brief DEAD as a JSON message today - roll/pitch/yaw/nav-error info.
 * The webclient still has a parser for it (legacy/mobile-client compat),
 * but drone_engage_mavlink sends the equivalent data (ATTITUDE/
 * NAV_CONTROLLER_OUTPUT/VFR_HUD) packed as raw MAVLink under
 * TYPE_AndruavMessage_MAVLINK instead (every ~500ms).
 * @direction DEAD/UNUSED (as this JSON type)
 * fields (per parser, if ever sent): a roll(rad), b pitch(rad), y yaw,
 *  d target bearing, e wp distance, f alt error - all treated as REQUIRED
 */
#define TYPE_AndruavMessage_NAV_INFO                1036
/**
 * @brief Confirms the target of a GuidedPoint/CirclePoint after processing.
 * @direction MODULE_TO_WEB - one-shot echo, fired once per confirmed target;
 *  also listed in mavlink's own MESSAGE_FILTER but has no incoming handler
 *  (subscription is vestigial - this module only ever sends it)
 * @rate ON_DEMAND
 * @discard YES - purely informational echo of the just-sent command; if
 *  lost the webclient's own optimistic UI state is already correct
 * fields: P int REQUIRED (target_type/DESTINATION_* marker); T float
 *  REQUIRED (lat); O float REQUIRED (lon); A float REQUIRED (alt)
 */
#define TYPE_AndruavMessage_DistinationLocation     1037
/**
 * @brief DEAD/UNUSED - defined in messages.hpp (and duplicated in
 * drone_engage_communication_pro's copy) but no construction or parsing
 * code found anywhere in the repo (C++ or JS).
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_ConfigCOM                1038
/**
 * @brief DEAD/UNUSED - same situation as ConfigCOM: only a #define, no
 * builder or parser found in mavlink, communication_pro, or webclient.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_ConfigFCB                1039
/**
 * @brief Change target speed.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a float/unsigned REQUIRED (speed); b bool REQUIRED (is_ground_speed);
 *  c float/int REQUIRED (throttle); d bool REQUIRED (is_relative)
 */
#define TYPE_AndruavMessage_ChangeSpeed              1040
/**
 * @brief Trigger still-image capture (a bounded burst, not continuous video - video streams over WebRTC).
 * @direction WEB_TO_MODULE - module replies with TYPE_AndruavMessage_CameraList after handling
 * @rate ON_DEMAND
 * @discard NO
 * fields: a string REQUIRED (channel/camera local name, "" = first
 *  available); b number REQUIRED (image count); c number REQUIRED (ms
 *  between shots); d number OPTIONAL (sent but not consumed by receiver);
 *  e number OPTIONAL (0/1: 1 = send low-res PNG to GCS, else full-res)
 */
#define TYPE_AndruavMessage_Ctrl_Cameras             1041
/**
 * @brief User selects/clears a tracking point or region on the video.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND - only on user click/drag, no loop
 * @discard NO - a discrete user action
 * fields: a int REQUIRED (sub-action: 0 POINT,1 REGION,2 STOP,3 PAUSE,
 *  4 ENABLE,5 QUERY_CONFIG,6/7 AI_DRIVER on/off); b/c float REQUIRED for
 *  a=0/1 (center or region X/Y); r float REQUIRED for a=0 (radius);
 *  d/e float REQUIRED for a=1 (region width/height)
 */
#define TYPE_AndruavMessage_TrackingTarget_ACTION   1042
/**
 * @brief Streamed tracked-target position deltas, feeding DE-Pilot's autonomous camera-pointing/steering.
 * @direction MODULE_TO_MODULE - sent internal_message=true from de_tracker
 *  to de_mavlink (CDEPilotTracking); NOT normally relayed to the web
 *  client (the broker's default routing only force-broadcasts
 *  TrackingTarget_STATUS, not this one). IR/drone_engage_IR_camera reuses
 *  this same ID for unrelated hot/cold thermal point reporting.
 * @rate REALTIME - de_tracker emits every 15 processed frames (~2Hz at
 *  30fps target); IR camera module emits every ~3 frames (~10Hz)
 * @discard YES - latest offset supersedes older ones, safe to drop under congestion
 * fields (tracker): t array REQUIRED, each {x,y} normalized -0.5..0.5 offset;
 * fields (IR camera, same message id): t array, each {type:"hot"|"cold", x, y, temp}
 */
#define TYPE_AndruavMessage_TrackingTargetLocation  1043
/**
 * @brief Tracking state transition (lost/detected/enabled/stopped/config).
 * @direction MODULE_TO_WEB - the ONE tracking message explicitly
 *  force-broadcast by the comm broker regardless of internal_message flag;
 *  also consumed locally by de_mavlink's DE-Pilot.
 * @rate ON_DEMAND - fired only on state transitions, not periodic
 * @discard NO - state transitions must be delivered reliably (this is why
 *  it's the one message the broker force-broadcasts)
 * fields: a int REQUIRED (0 LOST,1 DETECTED,2 ENABLED,3 STOPPED,4 CONFIG);
 *  b uint8 REQUIRED (tracking_camera_direction); c bool REQUIRED (ai_priority)
 */
#define TYPE_AndruavMessage_TrackingTarget_STATUS   1044
/**
 * @brief Upload a full mission (legacy serialized text format; also carries fences).
 * @direction WEB_TO_MODULE - a single large message, relies on de_common's
 *  own UDP-layer chunking (unlike WayPoints, which is manually chunked)
 * @rate ON_DEMAND
 * @discard NO - loss corrupts the uploaded mission
 * fields: a string REQUIRED - serialized mission-plan text
 */
#define TYPE_AndruavMessage_UploadWayPoints          1046
/**
 * @brief Engage/release RC input mode (gamepad session start/stop), not a per-frame stream itself.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND - sent when a gamepad session starts/stops or RC mode changes
 * @discard NO - a mode-change command
 * fields: b unsigned int REQUIRED - RC_SUB_ACTION (0 RELEASED, 1
 *  CENTER_CHANNELS, 2 FREEZE_CHANNELS, 4 JOYSTICK_CHANNELS,
 *  8 JOYSTICK_CHANNELS_GUIDED)
 */
#define TYPE_AndruavMessage_RemoteControlSettings   1047
/**
 * @brief Set the home location.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: T float REQUIRED (lat); O float REQUIRED (lon); A float REQUIRED
 *  (alt; falls back to current FCB home altitude if 0)
 */
#define TYPE_AndruavMessage_SET_HOME_LOCATION        1048
/**
 * @brief DEAD on the module side - webclient sends it (API_CONST_RemoteCommand_zoomCamera)
 * but drone_engage_camera_2025 does not subscribe to it in MESSAGE_FILTER
 * and has no parser case for it.
 * @direction WEB_TO_MODULE (intended) - DEAD/UNUSED end-to-end today
 * fields (as sent): u string REQUIRED (camera unique name); a bool
 *  REQUIRED (zoom in/out); b number OPTIONAL (absolute zoom); c number
 *  OPTIONAL (zoom step)
 */
#define TYPE_AndruavMessage_CameraZoom               1049
/**
 * @brief DEAD on the module side - webclient sends it (API_SwitchCamera) but
 * drone_engage_camera_2025 has no handler for it. Actual camera switching
 * in the UI goes through RemoteCommand_SWITCHCAM(114) via RemoteExecute
 * instead (also effectively dead - see that constant below).
 * @direction WEB_TO_MODULE (intended) - DEAD/UNUSED end-to-end today
 * fields (as sent): u string REQUIRED - target camera unique name
 */
#define TYPE_AndruavMessage_CameraSwitch             1050
/**
 * @brief DEAD on the module side - webclient sends it (API_TurnMobileFlash,
 * likely for legacy/mobile Andruav app support) but drone_engage_camera_2025
 * has no handler for it.
 * @direction WEB_TO_MODULE (intended) - DEAD/UNUSED on this C++ module today
 * fields (as sent): f bool REQUIRED (flash on/off); u string REQUIRED (camera unique name)
 */
#define TYPE_AndruavMessage_CameraFlash		   1051
/**
 * @brief Continuous joystick/gamepad axis stream while engaged (RC channel values).
 * @direction WEB_TO_MODULE
 * @rate REALTIME - streamed every 250ms (CONST_sendRXChannels_Interval) via
 *  setInterval while a gamepad is engaged - the canonical "safe to drop
 *  under a bad link" message in this protocol
 * @discard YES - pure joystick position stream; only the latest sample is
 *  meaningful, dropping stale samples under congestion is the correct
 *  behavior (the next 250ms tick supersedes it)
 * fields: R unsigned REQUIRED (Rudder [0,1000]); T unsigned REQUIRED
 *  (Throttle); A unsigned REQUIRED (Aileron); E unsigned REQUIRED
 *  (Elevator); w/x/y/z int OPTIONAL (Aux 1-4; -999 sentinel = release channel)
 */
#define TYPE_AndruavMessage_RemoteControl2	   1052
/**
 * @brief DEAD/UNUSED - defined but never constructed or parsed anywhere in the repo.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_SensorsStatus           1053

/**
 * @brief tell a drone that another drone is in its team -a follower-.
 * @details
 *  ------------------------------
 *  | Sender | Receiver | Action |
 *  ------------------------------
 *    (1,3)GCS       Drone(follower)      Ask this drone to be a follower/unfollow a leader
 *    ANY       Drone(other)         This is just an announcement. No action is required.
 *    (2) LEADER    Drone(follower)      Confirm that this drone is a follower. and gives index and formation. requires a confirmation from follower.
*  (1) This message can be sent from GCS or another Drone either a leader or not.
 * (2) This message requests from the receiver "Drone" to send @ref TYPE_AndruavMessage_UpdateSwarm to Leader Drone.
 * The receiver can refuse to send @ref TYPE_AndruavMessage_UpdateSwarm
 * and the third drone can also refuse the request to be followed by the receiver.
 * @note receiver should not assume it is a follower. It only should forward this request to the leader.
 * @direction BIDIRECTIONAL - WEB_TO_MODULE (GCS request) and DRONE_TO_DRONE (leader<->follower)
 * @rate ON_DEMAND
 * @discard NO - state-changing swarm membership request
 * fields: a int REQUIRED for FOLLOW/CHANGE_FORMATION (follower index, -1=any,
 *  omitted for UNFOLLOW); b string OPTIONAL (leader party id); c string
 *  REQUIRED (target/slave party id - must match receiver, else it's just an
 *  announcement); d int OPTIONAL (formation id); f int REQUIRED (SWARM_FOLLOW/
 *  SWARM_UNFOLLOW/SWARM_CHANGE_FORMATION); h/v int OPTIONAL (min h/v distance)
 */
#define TYPE_AndruavMessage_FollowHim_Request           1054
/**
 * @brief This message is sent from Leader drone to a follower. It guides it to the destination point that it wants it to go to.
 * @details
 * There is nothing called a Follower Drone
 * All Drones Obey AndruavResala_FollowMe_Guided EVEN if they are Leaders.<br>
 * If a Drone wants to IGNORE these messages that is OK for whatever reason.<br>
 * If a Drone wants to Stop others from sending such messages it can send ANdruavResala_UpdateSwarm with remove action.
 * @direction DEAD/UNUSED in this codebase - registered in de_mavlink's
 *  MESSAGE_FILTER but no `case` handler exists and it is never constructed.
 *  Actual leader->follower guidance today flows through
 *  TYPE_AndruavMessage_SWARM_MAVLINK instead.
 */
#define TYPE_AndruavMessage_FollowMe_Guided             1055
/**
 * @brief This command is sent to instruct a drone to be a leader with a swarm-formation.
 * A Formation FORMATION_SERB_NO_SWARM means there is no swarm mode anymore.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a int REQUIRED (formation id; FORMATION_SERB_NO_SWARM demotes
 *  leader); b string REQUIRED (must equal own party_id, else treated as an
 *  announcement); h/v int OPTIONAL (min h/v spacing, default KNODE_LENGTH)
 */
#define TYPE_AndruavMessage_Make_Swarm                  1056
/**
 * @brief DEAD/UNUSED - defined but never constructed or parsed anywhere in the repo.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_SwarmReport                 1057
/**
 * @brief This message is sent to Leader Drone to add a slave drone in a swarm and in an index.
 * given index may contradict with other indices. It is upto Leader Drone to handle this conflict.
 * @direction DRONE_TO_DRONE - follower -> leader, via comm server
 * @rate ON_DEMAND - sent when a follower requests to join/leave a leader's swarm
 * @discard NO - swarm membership state change
 * fields: a int REQUIRED (SWARM_ADD/SWARM_DELETE); c string REQUIRED
 *  (leader party id - must match receiver, else announcement); d string
 *  REQUIRED (follower/slave party id); b (follower index, documented but
 *  not used by the current implementation)
 */
#define TYPE_AndruavMessage_UpdateSwarm                 1058
/**
 * @brief Cellular signal status of a unit (sender module not present in this
 * repo - likely an Android/mobile companion app - only consumers found here).
 * @direction MODULE_TO_WEB - relayed drone -> comm-server -> web
 * @rate unknown from this repo (likely periodic cellular report) - treat as MEDIUM/LOW
 * @discard YES - a signal-strength snapshot, latest reading is sufficient
 * fields: r number REQUIRED (signal level dBm); s number REQUIRED (network
 *  type code); op string OPTIONAL (operator name); c string OPTIONAL
 *  (country ISO); ds number OPTIONAL (data state)
 */
#define TYPE_AndruavMessage_CommSignalsStatus           1059
/**
 * d: event-id
 * [c]: sender module class type
 * [s]: sender module class id
 * [m]: JSON sender-module specific data.
 *
 * [bin]: binary conntent maybe attached to the command.
 * @direction BIDIRECTIONAL - WEB_TO_MODULE, MODULE_TO_MODULE, and rebroadcast
 *  DRONE_TO_DRONE via the comm server (to ANDRUAV_PROTOCOL_SENDER_ALL_AGENTS)
 * @rate ON_DEMAND - event-driven (mission item triggers, GCS UI action)
 * @discard NO - an event trigger, dropping it means a mission/sync event never fires
 * fields: d string REQUIRED (droneengage event id); m object OPTIONAL (event payload)
 */
#define TYPE_AndruavMessage_Sync_EventFire              1061
/**
 * @brief DEAD end-to-end - webclient has request/response code for it
 * (API_requestSearchableTargets / a matching parser), but both are
 * explicitly gated behind CONST_EXPERIMENTAL_FEATURES_ENABLED=false, and
 * no module in this repo ever constructs or answers it.
 * @direction DEAD/UNUSED (experimental, disabled)
 * fields (intended response): t array of {n REQUIRED, t OPTIONAL default "na"}
 */
#define TYPE_AndruavMessage_SearchTargetList            1062


//! NOT USED YET
/**
 * @brief Confirmed DEAD/UNUSED - `andruav_facade.cpp::API_sendPrepherals()`
 * is fully implemented but explicitly commented "NOT USED .... ENABLE IT"
 * directly above it, and is never called from anywhere. No web or module
 * parser exists for it either.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_Prepherials                 1070
/**
 * @brief: sends information about UDP Proxy of the unit.
 * a:  string - udp_ip_other
 * p:  int - udp_port_other
 * o:  int - optimization_level
 * en: bool - enabled
 * z: bool - paused
 * @direction MODULE_TO_WEB - implemented in drone_engage_mavlink (NOT
 *  camera_2025/communication_pro, despite the doc's original placement here)
 * @rate ON_DEMAND - sent in reply to an explicit request or after a
 *  pause/resume telemetry-rate change, not periodic
 * @discard YES - a status snapshot, latest value supersedes older ones
 * fields: a string REQUIRED (proxy IP); p number REQUIRED (proxy port);
 *  o number REQUIRED (optimization/streaming level); en bool REQUIRED
 *  (enabled); z bool OPTIONAL (paused, defaults false)
 */
#define TYPE_AndruavMessage_UDPProxy_Info               1071
/**
 * @brief used to set unit name and description.
 * This message is mainly sent from web and received by communication module.
 * It is used to change unit name and description.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - a rename command
 * fields: UN string REQUIRED (unit name); DS string REQUIRED (description);
 *  PR bool OPTIONAL (reset party_id flag)
*/
#define TYPE_AndruavMessage_Unit_Name                   1072
/**
 * @brief used to ping a unit name.
 * This message works in two ways:
 * * 1- send a ping to a unit to tell it that I am alive via p2p.
 * * 2- This is similar to send RemoteExecute (TYPE_AndruavMessage_ID)
 *      But in this case target unit does not need to reply with TYPE_AndruavMessage_ID
 *      It can reply with same TYPE_AndruavMessage_Ping_Unit
 *  Note that 1 & 2 can be done in a single message.
 *
 * params:
 *      [a]: sender_party_id : drone_engage party id. case: #1
 *      [k]: 1 - request ack.                         case: #2
 * @direction DRONE_TO_DRONE - P2P mesh only (ESP32 ring/mesh nodes); not used by web despite the constant existing there
 * @rate ON_DEMAND - not observed on a fixed timer, invoked to verify P2P mesh presence
 * @discard YES - a liveness probe; occasional loss is tolerable, next ping follows
 */
#define TYPE_AndruavMessage_Ping_Unit                   1073

/**
 * @brief used to upload DroneEngage Mission File.
 *
 * params:
 *      [j]: serialized DE mission JSON (NOTE: actual field is "j", the
 *           original doc below listing [a]/[e] is stale/incorrect)
 *      [e]: p_eraseFirst (bool, OPTIONAL - erase existing fences/mission first)
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - a mission upload, loss corrupts the mission
 */
#define TYPE_AndruavMessage_Upload_DE_Mission           1075


/**
 * @brief Select/search/enable/disable AI recognition targets, or request the class list.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a int REQUIRED (0 POINT[unused],1 SEARCH,2 DISABLE,3 ENABLE,
 *  4 CLASS_LIST); i array of int REQUIRED for a=1 (selected class indices)
 */
#define TYPE_AndruavMessage_AI_Recognition_ACTION               1076
/**
 * @brief AI recognition state transition, or the detectable class list.
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - fired only on state transitions, not periodic
 * @discard NO - state transitions must be delivered
 * fields: a int REQUIRED (0 LOST,1 DETECTED,2 ENABLED,3 DISABLED,4
 *  CLASS_LIST); c array of string REQUIRED only when a=4 (class names)
 */
#define TYPE_AndruavMessage_AI_Recognition_STATUS               1077
/**
 * @brief Best-detected-object bounding box, feeding de_tracker's AI-assisted driver.
 * @direction MODULE_TO_MODULE - yolo_ai/yolo_ai_generic -> de_tracker,
 *  sent internal_message=true so it never reaches the comm server/web
 * @rate REALTIME - fires on essentially every processed inference frame
 *  (no explicit throttle; bound only by camera FPS / inference throughput)
 * @discard YES - latest bounding box supersedes older ones, ideal candidate to drop under congestion
 * fields: b object REQUIRED - {x,y,w,h} normalized best-object box, conf
 *  float OPTIONAL (defaults 1.0); t array DEBUG-ONLY - full detection list,
 *  wrapped in #ifdef DEBUG in drone_engage_yolo_ai ("too much traffic"),
 *  never sent in release builds
 */
#define TYPE_AndruavMessage_AI_Recognition_TargetLocation       1078

/**
 * @brief Viewlink module control (laser/tracker/AI/camera/gimbal + get-status).
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a int REQUIRED (1 LASER,2 TRACKER,3 AI,4 CAMERA,5 GIMBAL,6
 *  GET_STATUS); b int REQUIRED for most categories (sub-action); value
 *  number OPTIONAL (laser brightness/frequency, tracker speed); x/y int
 *  REQUIRED for ADJUST_CROSS_POSITION; c number REQUIRED for camera zoom;
 *  p/y float REQUIRED for gimbal position/adjust (normalized -1..1)
 */
#define TYPE_AndruavMessage_Viewlink_ACTION                    1079
/**
 * @brief Viewlink status: connection/tracking/recording/error/telemetry/gimbal attitude.
 * @direction MODULE_TO_WEB
 * @rate MIXED, internally rate-limited by the sender:
 *  - GIMBAL_ATTITUDE/telemetry subtypes: HIGH/REALTIME, min interval ~0.1s (10Hz)
 *  - TRACKING subtype: MEDIUM, min interval ~0.5s (2Hz) unless status changes
 *  - CONNECTION/RECORDING/ERROR subtypes: ON_DEMAND, state transitions
 * @discard depends on subtype (field "a"): YES for GIMBAL_ATTITUDE/telemetry
 *  (latest orientation/values win) - NO for CONNECTION/TRACKING/ERROR
 *  (discrete state transitions must be delivered)
 * fields: a int REQUIRED (0 CONNECTION,1 TRACKING,2 RECORDING,3 ERROR,
 *  4 TELEMETRY,6 GIMBAL_ATTITUDE,7 ALL); a=6: y/p/r float REQUIRED;
 *  a=7: nested g/tr/gm/lrf/ai objects REQUIRED
 */
#define TYPE_AndruavMessage_Viewlink_STATUS                    1080
/**
 * @brief Engage DE-Pilot autonomous control (stabilization/altitude/tracking/yaw).
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - permission-gated (PERMISSION_ALLOW_GCS_MODES_CONTROL) flight-behavior command
 * fields: e bool OPTIONAL (enable/disable); m int OPTIONAL (default
 *  DEPILOT_OP_DISABLED, operation mode); q bool OPTIONAL (enqueue vs
 *  immediate); d int(ms) OPTIONAL (duration for timed modes); l
 *  float(m) OPTIONAL (target altitude); y float(deg) OPTIONAL (target yaw)
 */
#define TYPE_AndruavMessage_DEPilot_Control                    1081
/**
 * @brief Viewlink telemetry stream (gimbal angles, target/vehicle coords, LRF distance, AI targets).
 * @direction MODULE_TO_WEB - drone_engage_viewlink_module (Python)
 * @rate HIGH/REALTIME - internally rate-limited to min ~100ms (10Hz) between sends
 * @discard YES - latest telemetry snapshot supersedes older ones
 * fields: a int REQUIRED (always VIEWLINK_STATUS_TELEMETRY=4); angles
 *  {yaw,pitch,roll} OPTIONAL; vehicle {lat,lon,alt} OPTIONAL; target
 *  {lat,lon,alt,timestamp,source} OPTIONAL; lrf_distance float OPTIONAL;
 *  ai_targets array OPTIONAL
 * @note this id was reassigned from 1081 to 1083 (2026) to resolve a
 *  collision: drone_engage_viewlink_module's vendored messages.py had
 *  independently defined this same telemetry message at 1081, the same
 *  numeric id as TYPE_AndruavMessage_DEPilot_Control above. 1082 is
 *  already taken by the webclient's TYPE_AndruavMessage_Chat, so 1083 was
 *  the next free id verified unused across de_common, all vendored
 *  messages.hpp/.py copies, and the webclient's js_andruavMessages.js.
 */
#define TYPE_AndruavMessage_Viewlink_Telemetry                 1083

//Binary Starts with 2000

//deprecated telemetry technology
/**
 * @brief DEPRECATED/DEAD - no sender found anywhere; the receiver
 * (drone_engage_mavlink) parses the embedded raw mavlink stream but
 * discards the result (sendNative call is commented out). Superseded by
 * TYPE_AndruavMessage_MAVLINK + UDP proxy for telemetry.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_LightTelemetry              2022

/**********************************************************************
                        New Andruav Messages 2019
**********************************************************************/
/**
 * @brief Despite the "OBSOLETE" label, this message is ACTIVELY USED -
 * sets a servo channel value from the webclient's manual servo dialog and gamepad handlers.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND - per manual/gamepad servo adjustment
 * @discard NO - permission-gated (PERMISSION_ALLOW_GCS_MODES_SERVOS) command
 * fields: n int REQUIRED (servo channel); v int REQUIRED (servo value,
 *  0->1000, 9999->2000 special-cased)
 * @note legacy "OBSOLETE" name kept for wire compatibility, but the code path is live - see fcb_andruav_message_parser.cpp
 */
#define TYPE_AndruavMessage_ServoChannel                       6001  // NOTE: legacy name, but actively used (see doc above)

/**
 * @brief Raw MAVLink v2 message(s), packed back-to-back (binary, no JSON "ms" payload).
 * @direction BIDIRECTIONAL - MODULE_TO_WEB carries almost all telemetry
 *  (position/attitude/power/heartbeat/EKF/vibration/wind/terrain/distance/
 *  servo/mission/parameters); WEB_TO_MODULE carries parameter writes etc.
 * @rate HIGH, multi-rate depending on sub-message: position/attitude
 *  ~500ms, wind/terrain ~1000ms, power/EKF/vibration ~5000ms (all driven
 *  from de_mavlink's 10ms-tick loopScheduler); web->module direction is ON_DEMAND
 * @discard YES for the telemetry stream direction (MAVLink is designed for
 *  lossy links, latest state wins) - NO for the web->module command subset
 *  (e.g. parameter writes), which must be delivered
 */
#define TYPE_AndruavMessage_MAVLINK                            6502
/**
 * @brief Leader shares its position/attitude with swarm followers (packed MAVLink GLOBAL_POSITION_INT + ATTITUDE).
 * @direction MODULE_TO_MODULE (DRONE_TO_DRONE) - not sent to the webclient
 * @rate HIGH - periodic ~1000ms, rate-limited via DEF_SWARM_LEADER_LOCATION_UPDATE_RATE
 * @discard YES - latest leader position/attitude supersedes older ones
 */
#define TYPE_AndruavMessage_SWARM_MAVLINK                      6503

/**
 * Used by other modules to exchange mavlink information
 * between each other.
 * This allows custom implementation for sharing mavlink info
 * between mavlink module and other modules.
 * @direction DEAD/UNUSED - defined but no construction or parsing found anywhere in this codebase.
 *  Deliberately not revived by the precision-landing work: de_precland uses the
 *  semantic TYPE_AndruavMessage_PRECLAND_TARGET (6537) instead, which supersedes
 *  this use case. If a generic raw-MAVLink bridge is wanted later it needs its
 *  own msgid whitelist in de_mavlink, not this ID.
*/
#define TYPE_AndruavMessage_INTERNAL_MAVLINK                   6504


/**
 * @brief P2P mesh control (restart/connect/scan/access to a MAC).
 * @direction BIDIRECTIONAL - WEB_TO_MODULE (scan/reset) and
 *  MODULE_TO_MODULE (mavlink -> broker -> p2p module, intermodule-only)
 * @rate ON_DEMAND
 * @discard NO - a mesh state-change command
 * fields: a int REQUIRED (P2P_ACTION_* code); b string REQUIRED for
 *  CONNECT_TO_MAC (target mac); p string OPTIONAL (wifi password);
 *  c int OPTIONAL (wifi channel)
 */
#define TYPE_AndruavMessage_P2P_ACTION                         6505
/**
 * @brief DEAD/UNUSED - both p2p and mavlink modules validate/parse this
 * type defensively but NOTHING in the repo ever constructs/sends it.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_P2P_STATUS                         6506

/**
 * @brief Detected mesh access points (BSSIDs) near this unit.
 * @direction MODULE_TO_WEB
 * @rate MEDIUM - periodic ~10s (P2P loop ticks ~1s, gated by counter%10==0, or immediate on change)
 * @discard YES - a scan snapshot, latest results supersede older ones
 * fields: keyed by party_id string; per entry: b string (bssid), p string
 *  (party id), s string (ssid), c int (channel), r int (rssi), t int (usec since last seen)
 */
#define TYPE_AndruavMessage_P2P_InRange_BSSID                  6507
/**
 * @brief Detected mesh peer nodes/pings near this unit.
 * @direction MODULE_TO_WEB
 * @rate MEDIUM - periodic ~10s (gated by a 10s check-rate timer)
 * @discard YES - a scan snapshot, latest results supersede older ones
 * fields: keyed by party_id string; per entry: m string (mac), p string
 *  (party id), c bool (connected), t int (usec since last action)
 */
#define TYPE_AndruavMessage_P2P_InRange_Node                   6508


/**
 * @brief used to set communication channels on/off
 * current fields are:
 * [p2p]: for turning p2p on/off or leave as is.
 * [ws]: for turning communication server websocket on/off or leave as is.
 * [w2]: for turning communication server LOCAL websocket on/off or leave as is.
 * [p2]: ip of [w2] which means reconnect to this ip Local Communication Socket.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - a link-control command
 * fields: ws bool OPTIONAL (cloud websocket on/off); wsd number OPTIONAL
 *  (auto-revert seconds); w2 bool OPTIONAL (LOCAL websocket on/off);
 *  wd2 number OPTIONAL (LOCAL auto-revert seconds)
 * @note the "p2p" field documented above is not implemented by the
 *  receiving handler for this message (only ws/wsd/w2/wd2 are handled).
 * (Fixed 2026: the webclient sender previously sent the local-line
 *  revert-duration as "w2d" instead of "wd2", so it was silently ignored -
 *  js_commands_api.js now sends "wd2" to match the C++ receiver.)
 */
#define TYPE_AndruavMessage_Communication_Line_Set             6509

/**
 * @brief Reports that the comm websocket line was just turned off.
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - fired only as the line is going offline (timing-sensitive: delivery over the very link that's dying is inherently unreliable)
 * @discard NO in principle (a state transition), but be aware delivery is
 *  best-effort here since it races the link going down
 * fields: ws bool REQUIRED (as implemented); p2p bool documented but never populated by the sender
 */
#define TYPE_AndruavMessage_Communication_Line_Status          6510


/**
 * @brief Text-to-speech request.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: t string REQUIRED (text); l string OPTIONAL (language); p OPTIONAL (pitch); v OPTIONAL (volume)
 */
#define TYPE_AndruavMessage_SOUND_TEXT_TO_SPEECH               6511
/**
 * @brief Play a sound file.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: f string REQUIRED (file path)
 */
#define TYPE_AndruavMessage_SOUND_PLAY_FILE                    6512


/**
 * @brief MODULE_ACTION is a generic module message.
 * In SDR it is used to configure the module.
 * current fields are:
 *
 * CMD#1
 * [a]: SDR_ACTION_SDR_INFO                                 6
 * [fc]: center frequency
 * [g]: gain
 * [r]: sample rate
 * [m]: demodulation mode -NOT IMPLEMENTED-
 * [i]: driver index, based on TYPE_AndruavMessage_SDR_INFO
 * [t]: rate of reading signals. - 0 means once
 * [r]: display bars... i.e. # of merged output readings.
 * [l]: trigger level... signal level after which a trigger event is sent.
 *
 * **********************************************************************
 * CMD#2
 * [a]:  SDR_ACTION_LIST_SDR_DEVICES                        2
 * [dr]: drivers list
 *
 * CMD#3
 * [a]: SDR_ACTION_TRIGGER                                  7
 * @direction BIDIRECTIONAL - WEB_TO_MODULE for CONNECT/DISCONNECT/SET_CONFIG/
 *  READ_DATA/PAUSE_DATA; MODULE_TO_WEB for SDR_INFO reply, LIST_SDR_DEVICES
 *  reply, and TRIGGER alerts
 * @rate ON_DEMAND per command/reply; TRIGGER fires whenever a signal
 *  crosses the configured level (event-driven, not periodic)
 * @discard NO - configuration/commands and trigger alerts must be delivered
 * (Fixed 2026: field "t" (streaming interval, milliseconds - see the
 *  webclient's "Interval (ms)" label) was previously mis-consumed by
 *  sdr_driver.cpp as whole seconds (multiplied by 1e9 instead of 1e6 to
 *  get nanoseconds), making a UI value of e.g. 1000 wait ~16.7 minutes
 *  instead of 1 second. sdr_driver.cpp now converts ms->ns correctly.)
 */
#define TYPE_AndruavMessage_SDR_ACTION                         6514
/**
 * @brief Request SDR info or device list (wraps a sub-action inside TYPE_AndruavMessage_SDR_ACTION's namespace).
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: C int REQUIRED (always TYPE_AndruavMessage_SDR_ACTION); a int
 *  REQUIRED (SDR_ACTION_SDR_INFO or SDR_ACTION_LIST_SDR_DEVICES)
 */
#define TYPE_AndruavMessage_SDR_REMOTE_EXECUTE                 6515
/**
 * @brief Streamed FFT spectrum bars (binary: JSON header + float32 array).
 * @direction MODULE_TO_WEB
 * @rate REALTIME - continuous stream while active; interval = SDR_ACTION's
 *  "t" field in milliseconds; t=0 means send once
 * @discard YES - the definitive "keep-latest, drop-under-congestion"
 *  message in the SDR module; a stale spectrum frame is worthless, resubscribe later
 * fields: fcm double REQUIRED (min frequency); fcst float REQUIRED
 *  (frequency step); tim uint64 REQUIRED (timestamp usec); binary
 *  attachment REQUIRED (raw float array of spectrum bars)
 */
#define TYPE_AndruavMessage_SDR_SPECTRUM                       6516

// GPIO Parameters
#define GPIO_ACTION_PORT_CONFIG                                0
#define GPIO_ACTION_INFO                                       1
#define GPIO_ACTION_PORT_WRITE                                 2

/**
 * @brief Own P2P mesh connection status of this unit.
 * @direction MODULE_TO_WEB - periodic push, plus on-demand reply to a
 *  RemoteExecute{C: P2P_InRange_BSSID} request
 * @rate MEDIUM - periodic ~10s (same loop gate as InRange_BSSID)
 * @discard YES - a status snapshot, latest value wins
 * fields: c int (connection type), a1/a2 string (own/AP address), wc int
 *  (wifi channel), wp string (wifi password), pa string (parent node
 *  address), pc bool (parent connected), f string (firmware version),
 *  lp string (expected parent mac), a/o/d bool (driver connected/p2p
 *  connected/p2p disabled)
 */
#define TYPE_AndruavMessage_P2P_INFO                           6517


/**
 * @brief Fires once per mission-item transition during mission execution.
 * @direction MODULE_TO_MODULE - internal only (sent with internal_only
 *  flag), FCB -> comm broker, never reaches the webclient
 * @rate ON_DEMAND - repeats over a mission's lifetime, once per waypoint reached
 * @discard NO - drives mission-attached-command triggering, must be delivered
 * fields: s string REQUIRED - event/mission-item sequence ID
 */
#define TYPE_AndruavMessage_Mission_Item_Sequence              6518


/**
 * @brief Configure or write a GPIO pin.
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO - a hardware-control command
 * fields: i string OPTIONAL (target module_key, mismatch => dropped);
 *  a int REQUIRED (GPIO_ACTION_PORT_CONFIG/PORT_WRITE, PORT_READ
 *  unimplemented); CONFIG: p int REQUIRED (pin), m int REQUIRED (mode),
 *  v int OPTIONAL, n int OPTIONAL (pin name); WRITE: v int REQUIRED, n
 *  string OPTIONAL (lookup by name, takes priority), p int OPTIONAL
 *  (fallback lookup by number), d uint REQUIRED only in PWM mode (duty width)
 */
#define TYPE_AndruavMessage_GPIO_ACTION                        6519
/**
 * @brief GPIO pin status (full snapshot or a single targeted pin).
 * @direction MODULE_TO_WEB
 * @rate MEDIUM/LOW - internal tick every 1000ms, sent to the GCS every
 *  10000ms (10s); PLUS immediate event-driven send on any config/write
 *  change or in reply to a GPIO_REMOTE_EXECUTE request
 * @discard YES for the periodic 10s snapshot (latest pin state wins) -
 *  the immediate on-change updates are lower-volume and worth keeping,
 *  but losing one is recoverable from the next periodic snapshot
 * fields: a int REQUIRED (always GPIO_ACTION_INFO); s array REQUIRED,
 *  each {i,p/b,m,t,d(if PWM),v,n(optional)}
 */
#define TYPE_AndruavMessage_GPIO_STATUS                        6520
/**
 * @brief Request current GPIO status (full or a specific pin).
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: a int REQUIRED (only GPIO_STATUS request handled); p int
 *  OPTIONAL (specific pin, falls back to full status if not found);
 *  i string OPTIONAL (module_key)
 */
#define TYPE_AndruavMessage_GPIO_REMOTE_EXECUTE                6521

/**
 * @brief Set IP/Port of Local Communication Server.
 * current fields are:
 * [u]: url/ip
 * [p]: port
 * @direction WEB_TO_MODULE
 * @rate ON_DEMAND
 * @discard NO
 * fields: u string REQUIRED (local server URL/IP); p string REQUIRED (local server port)
 * @bug drone_engage_communication_pro's handler validates/parses u and p
 *  from the incoming command but then calls
 *  reconnectToCommServer("192.168.1.144", "9967", "my_key") with hardcoded
 *  literals instead of the parsed values - the received u/p are discarded.
 */
#define TYPE_AndruavMessage_LocalServer_ACTION                 6522
/**
 * @brief DEAD/UNUSED - only the #define exists (C++ and JS); no construction or parsing anywhere.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_LocalServer_STATUS                 6523
//TODO: use Action instead of Remote execute in all similar messages
/**
 * @brief DEAD/UNUSED - the TODO above suggests it was meant to replace
 * LocalServer_ACTION but was never wired up; no sender or receiver anywhere.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_LocalServer_REMOTE_EXECUTE         6524

/**
 * @brief Module configuration action - restart/apply-config/fetch-template/fetch-config/shutdown/restart-hw.
 * @direction WEB_TO_MODULE - handled generically by every C++ module via
 *  de_common's de_message_parser_base.cpp (and Python equivalent)
 * @rate ON_DEMAND
 * @discard NO - a config-management command
 * fields: a int REQUIRED (CONFIG_ACTION_*); b string OPTIONAL (target
 *  module_key, message ignored if it doesn't match); c object REQUIRED
 *  only for APPLY_CONFIG (new config JSON)
 */
#define TYPE_AndruavMessage_CONFIG_ACTION                      6525
/**
 * @brief Reply carrying a module's config template or current config JSON.
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - sent in reply to a CONFIG_ACTION fetch request
 * @discard NO - config content must arrive intact, not a state snapshot
 * fields: a int REQUIRED (CONFIG_STATUS_FETCH_CONFIG_TEMPLATE/FETCH_CONFIG);
 *  b object REQUIRED (template/config JSON); k string REQUIRED (module_key);
 *  R bool REQUIRED (true when replying to a request)
 */
#define TYPE_AndruavMessage_CONFIG_STATUS                      6526

/**
 * @brief DEAD/UNUSED - defined but never constructed or parsed anywhere (not even in the webclient parser).
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_MAVLINK_EVENTS                     6527

/**
 * @brief DEAD/UNIMPLEMENTED - parser case exists but body is an empty
 * switch with "//TODO: LATER"; no sender anywhere.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_IR_CAMERA_MI48_ACTION              6528
/**
 * @brief DEAD/UNIMPLEMENTED - sent MODULE_TO_MODULE (internal_message=true)
 * on IR detection state changes, but the receiving parser case is an empty
 * stub ("//TODO: LATER") and no other module/web parses it - effectively inert.
 * @direction MODULE_TO_MODULE (intended), currently a no-op end-to-end
 * @rate ON_DEMAND (intended) - event-driven on state transitions
 * fields: a int REQUIRED (reuses TrackingTarget_STATUS_* constants);
 *  b uint8 REQUIRED (camera direction)
 */
#define TYPE_AndruavMessage_IR_CAMERA_MI48_STATUS              6529
/**
 * @brief Sound library list (available TTS/sound files).
 * @direction MODULE_TO_WEB - sent on startup, after config apply, and in
 *  reply to a RemoteExecute{C: SOUND_LIST} request
 * @rate ON_DEMAND
 * @discard NO - low frequency, not a candidate for dropping
 * fields: T array REQUIRED, each {n name, f file_path}; R bool REQUIRED
 *  (true when replying to a request)
 */
#define TYPE_AndruavMessage_SOUND_LIST                         6530
/**
 * @brief Remote Telnet/Terminal messages.
 * @details Allows a webclient to open a remote shell session on a unit,
 * send keystrokes, and receive terminal output. The de_telnet module
 * owns the pty lifecycle; de_comm routes these messages like any other
 * module-class message.
 *
 * TELNET_ACTION_OPEN    - open a new session. Reply with TELNET_STATUS.
 * TELNET_ACTION_CLOSE   - close a session by session_id.
 * TELNET_ACTION_LIST    - request list of active sessions.
 * TELNET_ACTION_RESIZE  - resize pty window (cols/rows).
 * TELNET_ACTION_DATA    - keystrokes/input from client (binary payload).
 *
 * JSON fields (in "ms" / ANDRUAV_PROTOCOL_MESSAGE_CMD):
 *   "a": action code (TELNET_ACTION_*)
 *   "i": session_id (string, assigned by module on OPEN)
 *   "d": text data (string) for DATA action when not using binary attach
 *   "c": columns (int) for RESIZE
 *   "r": rows    (int) for RESIZE
 *   "sh": shell  (string, optional) override shell binary for OPEN
 *   "st": status code (int) for TELNET_STATUS
 *   "e": error message (string) for TELNET_STATUS on failure
 *   "l": array of session info objects for LIST reply
 *
 * Binary path: TELNET_DATA may carry raw bytes as the binary attachment
 * after the JSON header (see CModule::sendBMSG). The "i" field in the
 * JSON header identifies the target session.
 */
#define TYPE_AndruavMessage_TELNET_ACTION                      6531
#define TYPE_AndruavMessage_TELNET_STATUS                      6532
#define TYPE_AndruavMessage_TELNET_DATA                        6533
#define TYPE_AndruavMessage_TELNET_REMOTE_EXECUTE              6534

/**
 * @brief Remote Telnet/Terminal messages.
 * @details Allows a webclient to open a remote shell session on a unit,
 * send keystrokes, and receive terminal output. The de_telnet module
 * owns the pty lifecycle; de_comm routes these messages like any other
 * module-class message.
 *
 * TELNET_ACTION_OPEN    - open a new session. Reply with TELNET_STATUS.
 * TELNET_ACTION_CLOSE   - close a session by session_id.
 * TELNET_ACTION_LIST    - request list of active sessions.
 * TELNET_ACTION_RESIZE  - resize pty window (cols/rows).
 * TELNET_ACTION_DATA    - keystrokes/input from client (binary payload).
 *
 * JSON fields (in "ms" / ANDRUAV_PROTOCOL_MESSAGE_CMD):
 *   "a": action code (TELNET_ACTION_*)
 *   "i": session_id (string, assigned by module on OPEN)
 *   "d": text data (string) for DATA action when not using binary attach
 *   "c": columns (int) for RESIZE
 *   "r": rows    (int) for RESIZE
 *   "sh": shell  (string, optional) override shell binary for OPEN
 *   "st": status code (int) for TELNET_STATUS
 *   "e": error message (string) for TELNET_STATUS on failure
 *   "l": array of session info objects for LIST reply
 *
 * Binary path: TELNET_DATA may carry raw bytes as the binary attachment
 * after the JSON header (see CModule::sendBMSG). The "i" field in the
 * JSON header identifies the target session.
 * @direction TELNET_ACTION: WEB_TO_MODULE. See TELNET_STATUS/TELNET_DATA below for their own direction/rate.
 * @rate ON_DEMAND - one-shot per user action (open/close/list/resize)
 * @discard NO - session-control commands, dropping "close" leaks a pty, dropping "open" silently fails a user action
 * fields: a int REQUIRED; i string REQUIRED for CLOSE/RESIZE; sh string
 *  OPTIONAL (OPEN); c int OPTIONAL default 80 (RESIZE); r int OPTIONAL default 24 (RESIZE)
 */
#define TYPE_AndruavMessage_TELNET_ACTION                      6531
/**
 * @brief Telnet session lifecycle status (opened/closed/list/error/resized).
 * @direction MODULE_TO_WEB
 * @rate ON_DEMAND - one-shot event per session lifecycle transition, not periodic
 * @discard NO - a lifecycle transition, must be delivered
 * fields: a int REQUIRED (TELNET_STATUS_*); i string (session_id); e
 *  string OPTIONAL (error, on ERROR); ec int (exit code, on CLOSED);
 *  c/r int (cols/rows, on RESIZED); l array (session list, on LIST)
 */
#define TYPE_AndruavMessage_TELNET_STATUS                      6532
/**
 * @brief Raw terminal I/O byte stream: keystrokes (web->module) and pty output (module->web).
 * @direction BIDIRECTIONAL
 * @rate REALTIME - continuous stream while a session is open (module pushes
 *  one message per pty output chunk, web pushes one per keystroke/input batch)
 * @discard NO - unlike telemetry/position streams, this is a reliable byte
 *  stream; each message carries unique unrepeatable content (terminal
 *  bytes), so dropping any of it corrupts the shell session. Do NOT apply
 *  keep-latest-only discard logic to this message despite its high frequency.
 * fields: i string REQUIRED (session_id); a int (status subtype, output
 *  direction only, TELNET_STATUS_DATA); input bytes as a binary
 *  attachment (preferred) or d string (fallback for small text input)
 */
#define TYPE_AndruavMessage_TELNET_DATA                        6533
/**
 * @brief DEAD/UNUSED - defined for possible future use; de_telnet's
 * parseRemoteExecute() is an explicit no-op stub, and no sender exists anywhere.
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavMessage_TELNET_REMOTE_EXECUTE              6534

/**
 * @brief Periodic self-reported module health/memory status (Layer 2 of the
 * DroneEngage Performance Monitor design - see servers/droneengage_performance_monitor
 * README). Sent by CFacade_Base::sendMemoryStatus(), called periodically from a
 * module's own main loop. Any C++ module linking de_common gets this "for free".
 *
 * fields:
 * [a]:  MODULE_HEALTH_ACTION_* (currently only MODULE_HEALTH_ACTION_STATUS)
 * [rs]: current resident memory (RSS) in MB
 * [pk]: peak resident memory (RSS) over the rolling history window in MB -
 *       NOT the kernel's lifetime VmPeak. This ages out the one-off startup
 *       allocation spike as the window rolls, so a high [pk] next to a low/stable
 *       [rs] no longer falsely suggests a leak. A [pk] that keeps climbing in
 *       step with [rs] across windows still indicates unreleased memory.
 * [sw]: swapped-out memory (VmSwap) in MB - non-zero/growing indicates memory
 *       pressure even before RSS itself looks alarming.
 * [th]: thread count - a leaking thread count is a distinct failure mode from a
 *       leaking heap and is cheap to include.
 * [sl]: RSS growth rate in MB/hour (linear regression over the rolling window)
 * [tr]: MODULE_HEALTH_TREND_* - UP/DOWN/STABLE, derived from [sl]
 * [hs]: MODULE_HEALTH_STATUS_* - OK/WARNING/CRITICAL, derived from [rs]/[sl]
 *       against configurable thresholds (CFacade_Base::configureMemoryStatus())
 * [up]: seconds since this module's health monitor started sampling - lets the
 *       receiver discount slope/trend during the warm-up period after a (re)start.
 *
 * Module identity (module_id/module_key/party_id) is not repeated here - it is
 * already carried by the surrounding sendJMSG() envelope.
 *
 * @direction MODULE_TO_WEB
 * @rate LOW - interval varies per caller: drone_engage_communication_pro
 *  ~15s (every 150 ticks of its 10Hz loop), drone_engage_rpi_gpio ~30s
 *  (explicit MEMORY_STATUS_INTERVAL_SEC=30). NOTE: modules that vendor
 *  de_module_health.hpp (sdr, telnet, tracking, camera_2025, mavlink) do
 *  NOT currently call sendMemoryStatus() at all - no health data flows
 *  from them today despite the shared infrastructure being available.
 * @discard YES - pure health telemetry with latest-value semantics, the
 *  single best candidate in the whole protocol to drop entirely under a
 *  degraded link with zero functional impact
 * @note servers/droneengage_performance_monitor/README.md's Layer 2
 *  section is stale - it still labels this as "Not yet implemented" /
 *  "Roadmap (future)" even though it's implemented and used by at least 2 modules.
 */
#define TYPE_AndruavMessage_MODULE_HEALTH_STATUS                6535

/**
 * @brief GCS or de_mavlink commands to the precision-landing module
 * (de_precland): disable/enable, select target_num from the layout file,
 * run selftest, or run chessboard camera calibration.
 * @direction WEB_TO_MODULE and MODULE_TO_MODULE - mapped to
 *  PERMISSION_ALLOW_TRACKING in de_comm (a vision-control action)
 * @rate ON_DEMAND
 * @discard NO - a discrete control action
 * fields: a int REQUIRED (PRECLAND_ACTION_* code);
 *  b int OPTIONAL (target_num for PRECLAND_ACTION_SET_TARGET);
 *  b-g int/double OPTIONAL (chessboard params for PRECLAND_ACTION_CALIBRATE:
 *   b=cols, c=rows, d=square_size_m, e=views, f=width, g=height)
 */
#define TYPE_AndruavMessage_PRECLAND_ACTION                     6536
/**
 * @brief Fused multi-tag precision-landing pose in body frame (FRD), metres.
 *  de_precland detects an AprilTag (tag36h11) multi-size target board, solves
 *  a single metric pose over all visible tags and publishes this; de_mavlink
 *  converts it into MAVLink LANDING_TARGET behind a safety gate.
 * @direction MODULE_TO_MODULE - intermodule only, de_precland -> de_mavlink;
 *  never forwarded to the WebClient
 * @rate HIGH - configurable, default 10Hz
 * @discard YES - latest pose supersedes older ones
 * fields: x/y/z double REQUIRED (body-frame forward/right/down metres);
 *  ax/ay double REQUIRED (LOS angles rad); n int REQUIRED (tags fused);
 *  e double REQUIRED (reprojection RMSE px); t int64 REQUIRED (capture
 *  timestamp usec monotonic, taken at frame grab NOT send time);
 *  v bool REQUIRED (position_valid); tn int OPTIONAL (target_num)
 */
#define TYPE_AndruavMessage_PRECLAND_TARGET                     6537
/**
 * @brief Precision-landing state (searching/locked/degraded/error) plus
 *  diagnostic counters for the GCS status widget.
 * @direction MODULE_TO_WEB and MODULE_TO_MODULE (de_precland -> GCS +
 *  de_mavlink); intentionally NOT permission-mapped in de_comm so view-mode
 *  GCS accounts can observe it
 * @rate LOW - ~2Hz plus immediately on every state change
 * @discard YES - latest status supersedes older ones
 * fields: a int REQUIRED (PRECLAND_STATUS_* state); b double (fps);
 *  c double (last reprojection RMSE px); d double (slant range m);
 *  e array<int> (tag IDs currently used); f int (PRECLAND_REASON_* gate
 *  reason code, NOMINAL when ok);
 *  g int OPTIONAL (PRECLAND_CALIB_STATUS_* calibration status, absent when
 *  no calibration is running);
 *  h int OPTIONAL (calibration views captured so far);
 *  i int OPTIONAL (calibration views total target);
 *  j double OPTIONAL (calibration RMS px, present on completion)
 */
#define TYPE_AndruavMessage_PRECLAND_STATUS                     6538

#define MODULE_HEALTH_ACTION_STATUS                             0

// MODULE_HEALTH_STATUS_* : field [hs]
#define MODULE_HEALTH_STATUS_OK                                 0
#define MODULE_HEALTH_STATUS_WARNING                            1
#define MODULE_HEALTH_STATUS_CRITICAL                           2

// MODULE_HEALTH_TREND_* : field [tr]
#define MODULE_HEALTH_TREND_STABLE                              0
#define MODULE_HEALTH_TREND_UP                                  1
#define MODULE_HEALTH_TREND_DOWN                                2

// TYPE_AndruavMessage_TELNET_ACTION
#define TELNET_ACTION_OPEN                                  0
#define TELNET_ACTION_CLOSE                                 1
#define TELNET_ACTION_LIST                                  2
#define TELNET_ACTION_RESIZE                                3
#define TELNET_ACTION_DATA                                  4   // input from client

// TYPE_AndruavMessage_TELNET_STATUS
#define TELNET_STATUS_OPENED                                0   // session opened ok
#define TELNET_STATUS_CLOSED                                1   // session closed
#define TELNET_STATUS_DATA                                  2   // output data from pty
#define TELNET_STATUS_LIST                                  3   // list of sessions
#define TELNET_STATUS_ERROR                                 4   // error (see "e" field)
#define TELNET_STATUS_RESIZED                               5   // resize ack

/**
 * @brief Test/debug only - used by de_databus client library examples
 * (C++/Python/Node test harnesses) to smoke-test the message bus, not by
 * any production module. Generic catch-all handler in de_module.cpp just
 * logs the raw message to stdout.
 * @direction DEAD/UNUSED in production traffic
 */
#define TYPE_AndruavMessage_DUMMY                              9999


// System Messages
/**
 * @brief Load saved tasks/mission-and-fence set - dual purpose: a
 * top-level System WS command (web<->storage server) AND reused as an
 * inter-module RemoteExecute "C" action code on the onboard UDP bus.
 * @direction BIDIRECTIONAL - WEB_TO_MODULE-equivalent (web -> comm/storage
 *  server over WebSocket) and MODULE_TO_MODULE (mavlink -> comm broker,
 *  intermodule, requests onboard reload)
 * @rate ON_DEMAND - triggered on connect / task-management UI action
 * @discard NO - a data-management request, not a snapshot
 */
#define TYPE_AndruavSystem_LoadTasks	              9001
/**
 * @brief Save tasks (fences/missions) to storage.
 * @direction WEB_TO_MODULE-equivalent (web -> comm/storage server, WebSocket)
 * @rate ON_DEMAND
 * @discard NO
 */
#define TYPE_AndruavSystem_SaveTasks	              9002
/**
 * @brief Delete saved tasks.
 * @direction WEB_TO_MODULE-equivalent (web -> comm/storage server, WebSocket)
 * @rate ON_DEMAND
 * @discard NO
 */
#define TYPE_AndruavSystem_DeleteTasks	              9003
/**
 * @brief Disable saved tasks.
 * @direction WEB_TO_MODULE-equivalent (web -> comm/storage server, WebSocket)
 * @rate ON_DEMAND
 * @discard NO
 */
#define TYPE_AndruavSystem_DisableTasks	              9004
/**
 * @brief Connection-layer heartbeat/keepalive (module <-> cloud comm server), not part of the web UI protocol.
 * @direction MODULE_TO_MODULE (module -> cloud comm server link keepalive)
 * @rate LOW - periodic, floor ~10s (MIN_RECONNECT_RATE_US), configurable via max_allowed_ping_delay_in_ms
 * @discard YES - a heartbeat; a dropped ping is recovered by the next one, that's its purpose
 * fields: t number REQUIRED (timestamp usec)
 */
#define TYPE_AndruavSystem_Ping                         9005
/**
 * @brief Deregister from the comm server (logout/disconnect).
 * @direction BIDIRECTIONAL - sender (module or web) requests, comm server acks
 * @rate ON_DEMAND
 * @discard NO - connection state, must be delivered for clean deregistration
 */
#define TYPE_AndruavSystem_LogoutCommServer             9006
/**
 * @brief Comm server's registration acknowledgment.
 * @direction MODULE_TO_MODULE-equivalent (comm server -> connecting module/web)
 * @rate ON_DEMAND - once per successful connect/registration handshake
 * @discard NO - connection state
 */
#define TYPE_AndruavSystem_ConnectedCommServer          9007
/**
 * @brief Request/confirm UDP proxy telemetry sockets.
 * @direction MODULE_TO_MODULE (mavlink <-> comm server)
 * @rate ON_DEMAND - on-demand telemetry proxy setup
 * @discard NO - a one-time setup handshake
 * fields: en bool REQUIRED (enable/disable); socket1/socket2 object
 *  REQUIRED, each {address string, port uint}
 */
#define TYPE_AndruavSystem_UDPProxy                     9008
/**
 * @brief DEAD/UNUSED - only appears as a constant in de_databus/nodejs/messages.js;
 * no construction or parsing anywhere. Not to be confused with the
 * separate, actively-used TYPE_AndruavMessage_LocalServer_ACTION/STATUS (6522-6524).
 * @direction DEAD/UNUSED
 */
#define TYPE_AndruavSystem_LocalServer                  9009

// Inter Module Commands
// (duplicate legacy block - see the documented TYPE_AndruavModule_* defines
//  near the top of the "Andruav Messages" section above; kept only because
//  older code may still reference this second copy, values are identical)
#define TYPE_AndruavModule_ID                           9100
#define TYPE_AndruavModule_RemoteExecute                9101
#define TYPE_AndruavModule_Location_Info                9102


// #define TYPE_AndruavMessage_Sonar_Info              13001
// #define TYPE_AndruavMessage_Sonar_Action            13002
// #define TYPE_AndruavMessage_Sonar_RemoteExecute     13003

// DEFINE YOUR MESSAGE NUMBER HERE
#define TYPE_AndruavMessage_USER_RANGE_START 80000
#define TYPE_AndruavMessage_USER_RANGE_END 90000

// Andruav Mission Types
#define TYPE_CMissionItem                                    0
#define TYPE_CMissionAction_Spline                           6
#define TYPE_CMissionItem_WayPointStep                      16 // same as mavlink
#define TYPE_CMissionAction_Circle                          18 // same as mavlink MAV_CMD_NAV_LOITER_TURNS
#define TYPE_CMissionAction_RTL                             20 // same as mavlink
#define TYPE_CMissionAction_Landing                         21 // same as mavlink
#define TYPE_CMissionAction_TakeOff                         22 // same as mavlink
#define TYPE_CMissionAction_CONTINUE_AND_CHANGE_ALT         30 // same as mavlink
#define TYPE_CMissionAction_Guided_Enabled                  92 // same as mavlink
#define TYPE_CMissionAction_Delay                           93 // same as mavlink
#define TYPE_CMissionAction_Delay_STATE_MACHINE            112 // same as mavlink
#define TYPE_CMissionAction_ChangeAlt                      113 // same as mavlink
#define TYPE_CMissionAction_ChangeHeading                  115 // same as mavlink
#define TYPE_CMissionAction_ChangeSpeed                    178 // same as mavlink
#define TYPE_CMissionAction_CameraControl                  203 // same as mavlink
#define TYPE_CMissionAction_CameraTrigger                  206 // same as mavlink
#define TYPE_CMissionAction_DummyMission                 99999



// P2P Parameters

#define P2P_ACTION_RESTART_TO_MAC                           0
#define P2P_ACTION_CONNECT_TO_MAC                           1
#define P2P_ACTION_CANDICATE_MAC                            2
#define P2P_ACTION_SCAN_NETWORK                             3
/**
 * @brief this is different from P2P_ACTION_CONNECT_TO_MAC
 * in that it does not require direct access
 * or specifies who is parent to whom.
 */
#define P2P_ACTION_ACCESS_TO_MAC                            4
#define P2P_ACTION_SEND_STATUS                              5

#define P2P_STATUS_CONNECTED_TO_MAC                         0
#define P2P_STATUS_DISCONNECTED_FROM_MAC                    1


// Remote Execute Commands
// All of these are sub-command ids carried in the "C" field of a
// TYPE_AndruavMessage_RemoteExecute (1005) envelope. @direction WEB_TO_MODULE,
// @rate ON_DEMAND, @discard NO for all of them (one-shot commands), unless noted otherwise.
#define RemoteCommand_GET_WAY_POINTS                           500 // get from andruav not FCB but you can still read from fcb and refresh all   ; DEAD - no receiving `case` in drone_engage_mavlink today
#define RemoteCommand_RELOAD_WAY_POINTS_FROM_FCB               501
#define RemoteCommand_CLEAR_WAY_POINTS                         502 // permission-gated: PERMISSION_ALLOW_GCS_WP_CONTROL
#define RemoteCommand_CLEAR_FENCE_DATA 	                     503 // andruav fence; fn OPTIONAL fence name, absent = clear all; permission-gated: PERMISSION_ALLOW_GCS_WP_CONTROL
#define RemoteCommand_SET_START_MISSION_ITEM                   504 // n: unsigned REQUIRED, zero-based mission item index (0=home)
#define RemoteCommand_REQUEST_PARA_LIST                        505 // list of FCB parameters
#define RemoteCommand_SET_UDPPROXY_CLIENT_PORT                 506 // P: int REQUIRED (<0xffff), persisted to local config; permission-gated: PERMISSION_ALLOW_GCS_FULL_CONTROL
#define RemoteCommand_MISSION_COUNT                            507
#define RemoteCommand_MISSION_CURRENT                          508 // shares handler with 507, no distinct web sender found


// SDR Parameters
#define SDR_ACTION_CONNECT                                  0
#define SDR_ACTION_DISCONNECT                               1
#define SDR_ACTION_LIST_SDR_DEVICES                         2
#define SDR_ACTION_SET_CONFIG                               3
#define SDR_ACTION_READ_DATA                                4
#define SDR_ACTION_PAUSE_DATA                               5
#define SDR_ACTION_SDR_INFO                                 6
#define SDR_ACTION_TRIGGER                                  7


// GPIO Parameters
#define GPIO_ACTION_PORT_CONFIG                             0
#define GPIO_ACTION_INFO                                    1
#define GPIO_ACTION_PORT_WRITE                              2
#define GPIO_ACTION_PORT_READ                               3


// CAMERA MODULE MESSAGES
#define EXTERNAL_CAMERA_TYPE_UNKNOWN                        0
#define EXTERNAL_CAMERA_TYPE_RTCWEBCAM                      2
#define EXTERNAL_CAMERA_SUPPORT_ZOOMING                     0x1
#define EXTERNAL_CAMERA_SUPPORT_ROTATION                    0x2
#define EXTERNAL_CAMERA_SUPPORT_RECORDING                   0x4
#define EXTERNAL_CAMERA_SUPPORT_PHOTO                       0x8
#define EXTERNAL_CAMERA_SUPPORT_DUAL_CAM                    0x10
#define EXTERNAL_CAMERA_SUPPORT_FLASHING                    0x20
// Sent inside TYPE_AndruavMessage_CameraList's per-camera "p" (type) and "s"
// (capability bitmask) fields. Built in drone_engage_camera_2025's
// getDeviceListAsJSON(); zoom/dual-cam/flashing bits are defined but never
// actually set by that module today (see CameraZoom/CameraSwitch/CameraFlash
// being dead above - the capability flags exist ahead of the features).




// Remote Execute Commands
// @direction WEB_TO_MODULE (camera module, via RemoteExecute), @rate ON_DEMAND unless noted.
#define RemoteCommand_MAKETILT                              100
#define RemoteCommand_TAKEIMAGE                             102
#define RemoteCommand_MAKEBEEP                              103 // Toggle siren/alarm sound
#define RemoteCommand_SENDSMS                               104 // Send SMS with GPS location to unit's configured recovery number
#define RemoteCommand_ROTATECAM                             105 // Rotate Camera; a: string REQUIRED (channel, ""=first available), r: int REQUIRED (rotation angle)
#define RemoteCommand_IMUCTRL                               106 // Enable/disable IMU data streaming
#define RemoteCommand_SMSwGPS                               107 // Send SMS with GPS location; optional "n" field selects receiver number
#define RemoteCommand_TELEMETRYCTRL                         108 // Telemetry streaming; Act: unsigned REQUIRED (ADJUST_RATE/REQUEST_PAUSE/REQUEST_RESUME), LVL: unsigned OPTIONAL (with ADJUST_RATE only); permission-gated: PERMISSION_ALLOW_GCS_FULL_CONTROL
#define RemoteCommand_NOTIFICATION                          109
#define RemoteCommand_STREAMVIDEO 		                  110 // DEAD-ish: handler only replies with CameraList, Act/CH/N fields sent by web are not consumed
#define RemoteCommand_RECORDVIDEO 		                  111 // T: string REQUIRED (track/channel), Act: bool REQUIRED (start/stop)
#define RemoteCommand_STREAMVIDEORESUME 	                  112 // DEAD - defined only, no sender or receiver found anywhere
#define RemoteCommand_ChangeUnitID                          113
#define RemoteCommand_SWITCHCAM 			           114 // DEAD in practice - module handler is a no-op stub (just replies CameraList), no active web sender (camera switch UI uses CameraSwitch(1050) instead, which is itself dead - see above)
#define RemoteCommand_SET_GPS_SOURCE                        115
#define RemoteCommand_SET_CONNECT                           116
#define RemoteCommand_MAKEFLASH                             117 // Toggle flash using LED and Screen
#define RemoteCommand_CONNECT_FCB                           118 // no-op on the receiving side (case exists, body is just `break;`)



// Drone Report
#define Drone_Report_NAV_ItemReached            1

// Error Info Types

#define NOTIFICATION_TYPE_REGISTRATION          22
#define NOTIFICATION_TYPE_TELEMETRY             33
#define NOTIFICATION_TYPE_PROTOCOL              44
#define NOTIFICATION_TYPE_LO7ETTA7AKOM          77
#define NOTIFICATION_TYPE_GEO_FENCE             88

// Error Numbers
#define ERROR_TYPE_LO7ETTA7AKOM                 5
#define ERROR_3DR                               7
#define ERROR_GPS                               10
#define ERROR_POWER                             11
#define ERROR_RCCONTROL                         12
#define ERROR_TYPE_ERROR_MODULE                 13
#define ERROR_TYPE_ERROR_P2P                    23
#define ERROR_TYPE_ERROR_SDR                    24
#define ERROR_GEO_FENCE_ERROR                   100
#define ERROR_USER_DEFINED                      1000

// 0	MAV_SEVERITY_EMERGENCY	System is unusable. This is a "panic" condition.
#define NOTIFICATION_TYPE_EMERGENCY             0
// 1	MAV_SEVERITY_ALERT	Action should be taken immediately. Indicates error in non-critical systems.
#define NOTIFICATION_TYPE_ALERT                 1
// 2	MAV_SEVERITY_CRITICAL	Action must be taken immediately. Indicates failure in a primary system.
#define NOTIFICATION_TYPE_CRITICAL              2
// 3	MAV_SEVERITY_ERROR	Indicates an error in secondary/redundant systems.
#define NOTIFICATION_TYPE_ERROR                 3
// 4	MAV_SEVERITY_WARNING	Indicates about a possible future error if this is not resolved within a given timeframe. Example would be a low battery warning.
#define NOTIFICATION_TYPE_WARNING               4
// 5	MAV_SEVERITY_NOTICE	An unusual event has occurred, though not an error condition. This should be investigated for the root cause.
#define NOTIFICATION_TYPE_NOTICE                5
// 6	MAV_SEVERITY_INFO	Normal operational messages. Useful for logging. No action is required for these messages.
#define NOTIFICATION_TYPE_INFO                  6
// 7	MAV_SEVERITY_DEBUG	Useful non-operational messages that can assist in debugging. These should not occur during normal operation.
#define NOTIFICATION_TYPE_DEBUG                 7







#define GPS_MODE_AUTO                           0
// .a.k.a mobile... i.e. gps info used bu de comm is not from the board
#define GPS_MODE_EXTERNAL                       1
#define GPS_MODE_FCB                            2




#define WAYPOINT_NO_CHUNK                       0
#define WAYPOINT_CHUNK                          1
#define WAYPOINT_LAST_CHUNK                     999


// Telemetry Request Remote Execute
#define CONST_TELEMETRY_REQUEST_START		1
#define CONST_TELEMETRY_REQUEST_END		2
#define CONST_TELEMETRY_REQUEST_RESUME		3
#define CONST_TELEMETRY_ADJUST_RATE		4
#define CONST_TELEMETRY_REQUEST_PAUSE            5


// Fence Soft & Hard actions
#define CONST_FENCE_ACTION_SOFT                   0
#define CONST_FENCE_ACTION_RTL                    2
#define CONST_FENCE_ACTION_LAND                  12
#define CONST_FENCE_ACTION_LOITER                10
#define CONST_FENCE_ACTION_BRAKE                 17
#define CONST_FENCE_ACTION_SMART_RTL             21


// TYPE_AndruavMessage_UpdateSwarm actions
#define SWARM_ADD                                   1
#define SWARM_DELETE                                2
// TYPE_AndruavMessage_FollowHim_Request actions
#define SWARM_FOLLOW                                1
#define SWARM_UNFOLLOW                              2
#define SWARM_CHANGE_FORMATION                      3

#define TASHKEEL_SERB_NO_SWARM                      0
#define TASHKEEL_SERB_THREAD                        1
#define TASHKEEL_SERB_VECTOR                        2
#define TASHKEEL_SERB_VECTOR_180                    3

// GCS Permissions
#define PERMISSION_ALLOW_GCS                0x00000001
#define PERMISSION_ALLOW_UNIT               0x00000010
#define PERMISSION_ALLOW_GCS_FULL_CONTROL   0x00000f00
#define PERMISSION_ALLOW_GCS_WP_CONTROL     0x00000100
#define PERMISSION_ALLOW_GCS_MODES_CONTROL  0x00000200
#define PERMISSION_ALLOW_GCS_MODES_SERVOS   0x00000400
#define PERMISSION_ALLOW_GCS_VIDEO          0x0000f000

// Category/action permission bits (bits 16-25). Synced from canonical
// de_comm messages.hpp.  When all are zero and the view-mode bit (29) is
// set, the account is a read-only view-mode GCS.
#define PERMISSION_CATEGORY_ACTION_MASK    0x03ffffff
#define PERMISSION_ALLOW_SWARM              0x00010000   // bit 16
#define PERMISSION_ALLOW_TRACKING           0x00020000   // bit 17
#define PERMISSION_ALLOW_GEOFENCE           0x00040000   // bit 18
#define PERMISSION_ALLOW_SOUND              0x00080000   // bit 19
#define PERMISSION_ALLOW_SDR                0x00100000   // bit 20
#define PERMISSION_ALLOW_GPIO               0x00200000   // bit 21
#define PERMISSION_ALLOW_TELNET             0x00400000   // bit 22
#define PERMISSION_ALLOW_P2P                0x00800000   // bit 23
#define PERMISSION_ALLOW_CHAT               0x01000000   // bit 24
#define PERMISSION_ALLOW_CONFIG             0x02000000   // bit 25
#define PERMISSION_ALLOW_VIEW_MODE          0x20000000   // bit 29


// DistinationLocation Types
#define DESTINATION_GUIDED_POINT            0
#define DESTINATION_SWARM_MY_LOCATION       1

#define SPECIAL_NAME_ANY                "_any_"
#define SPECIAL_NAME_ALL_RECEIVERS      "_generic_"
#define SPECIAL_NAME_VEHICLE_RECEIVERS  "_drone_"
#define SPECIAL_NAME_GCS_RECEIVERS      "_gcs_"

#define TRACKING_CAMERA_DIRECTION_NONE      0
#define TRACKING_CAMERA_DIRECTION_FRONT     1
#define TRACKING_CAMERA_DIRECTION_BACK      2
#define TRACKING_CAMERA_DIRECTION_DOWN      3
#define TRACKING_CAMERA_DIRECTION_UP        4

// TYPE_AndruavMessage_TrackingTarget_ACTION
#define TrackingTarget_ACTION_TRACKING_POINT            0
#define TrackingTarget_ACTION_TRACKING_REGION           1
#define TrackingTarget_ACTION_TRACKING_STOP             2
#define TrackingTarget_ACTION_TRACKING_PAUSE            3
#define TrackingTarget_ACTION_TRACKING_ENABLE           4
#define TrackingTarget_ACTION_TRACKING_QUERY_CONFIG     5
#define TrackingTarget_ACTION_TRACKING_AI_DRIVER_ENABLED  6
#define TrackingTarget_ACTION_TRACKING_AI_DRIVER_DISABLED 7

// TYPE_AndruavMessage_TrackingTarget_STATUS
#define TrackingTarget_STATUS_TRACKING_LOST                    0
#define TrackingTarget_STATUS_TRACKING_DETECTED                1
#define TrackingTarget_STATUS_TRACKING_ENABLED                 2
#define TrackingTarget_STATUS_TRACKING_STOPPED                 3
#define TrackingTarget_STATUS_TRACKING_CONFIG                  4

// TYPE_AndruavMessage_TrackingTarget_ACTION
#define TrackingTarget_ACTION_AI_Recognition_POINT          0
#define TrackingTarget_ACTION_AI_Recognition_SEARCH         1
#define TrackingTarget_ACTION_AI_Recognition_DISABLE        2
#define TrackingTarget_ACTION_AI_Recognition_ENABLE         3
#define TrackingTarget_ACTION_AI_Recognition_CLASS_LIST     4


// TYPE_AndruavMessage_AI_Recognition_STATUS
#define TrackingTarget_STATUS_AI_Recognition_LOST           0
#define TrackingTarget_STATUS_AI_Recognition_DETECTED       1
#define TrackingTarget_STATUS_AI_Recognition_ENABLED        2
#define TrackingTarget_STATUS_AI_Recognition_DISABLED       3
#define TrackingTarget_STATUS_AI_Recognition_CLASS_LIST     4


// TYPE_AndruavMessage_PRECLAND_ACTION
#define PRECLAND_ACTION_DISABLE                             0
#define PRECLAND_ACTION_ENABLE                              1
#define PRECLAND_ACTION_SET_TARGET                          2   // uses field b (target_num)
#define PRECLAND_ACTION_SELFTEST                            3   // validate camera.yaml + layout, report via STATUS
#define PRECLAND_ACTION_CALIBRATE                           4   // chessboard camera calibration -> camera.yaml
                                                            // fields b=cols c=rows d=square_size_m e=views
                                                            // T4/F-6: width/height (f/g) removed —
                                                            // calibration runs at the configured capture resolution.
                                                            // T6/F-8: requires PERMISSION_ALLOW_CONFIG (bit 25).
#define PRECLAND_ACTION_CALIBRATE_CANCEL                    5   // cancel a running calibration (no fields)

// PRECLAND_CALIB_STATUS_* : calibration sub-state reported in PRECLAND_STATUS field [g]
#define PRECLAND_CALIB_STATUS_IDLE                          0   // no calibration running
#define PRECLAND_CALIB_STATUS_CAPTURING                     1   // capturing chessboard views
#define PRECLAND_CALIB_STATUS_COMPUTING                     2   // running cv::calibrateCamera
#define PRECLAND_CALIB_STATUS_DONE                          3   // success, camera.yaml written (field [j] = RMS)
#define PRECLAND_CALIB_STATUS_FAILED                        4   // failed (not enough views / write error)

// TYPE_AndruavMessage_PRECLAND_STATUS
#define PRECLAND_STATUS_DISABLED                            0
#define PRECLAND_STATUS_SEARCHING                           1
#define PRECLAND_STATUS_LOCKED                              2
#define PRECLAND_STATUS_DEGRADED                            3   // detecting, but gated (RMSE/stale)
#define PRECLAND_STATUS_ERROR                               4   // no camera / no camera.yaml / bad layout

// PRECLAND_REASON_* : module-internal gate reason codes used by de_precland's
// publish gate (precland_gate.hpp). Not sent on the wire; the GCS only sees the
// resulting PRECLAND_STATUS_* state. preclandReasonName() maps these to text
// for local logs and the debug overlay.
#define PRECLAND_REASON_NOMINAL                             0
#define PRECLAND_REASON_NO_TARGET                           1
#define PRECLAND_REASON_STALE                               2
#define PRECLAND_REASON_MIN_TAGS                            3
#define PRECLAND_REASON_RMSE                                4
#define PRECLAND_REASON_ERROR                               5

// TYPE_AndruavMessage_CONFIG_ACTION
#define CONFIG_ACTION_Restart                               0
#define CONFIG_ACTION_APPLY_CONFIG                          1
#define CONFIG_REQUEST_FETCH_CONFIG_TEMPLATE                2
#define CONFIG_REQUEST_FETCH_CONFIG                         3
#define CONFIG_ACTION_SHUT_DOWN_HW                          4
#define CONFIG_ACTION_RESTART_HW                            5

#define CONFIG_STATUS_FETCH_CONFIG_TEMPLATE                 0
#define CONFIG_STATUS_FETCH_CONFIG                          1
