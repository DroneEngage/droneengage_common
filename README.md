# de_common Module

## UDP Client Communication Protocol

This module implements a UDP-based client used by DroneEngage modules to exchange messages with the `de_comm` applications. The client supports chunked transmission of large messages, reassembly on reception, and periodic module identification broadcasting.

For a codemap view and more implementation details, see:
https://windsurf.com/codemaps/c0a6e865-aaee-48ba-9658-2b9e1ee231b8-bf8b4864a72d0584

### 1. Initialization and Startup
- **Callback-based design**
  - The client is constructed with a callback implementing `CCallBack_UDPClient::onReceive()`.
  - All fully reassembled messages are delivered through this callback.
- **Socket setup**
  - A UDP socket is created and configured for datagram communication.
  - The socket is bound to a local port to allow incoming messages.
- **Thread startup**
  - A receiver thread is started to handle incoming UDP datagrams.
  - A sender-ID thread is started to periodically broadcast the module identifier.

### 2. Message Sending with Chunking
- **Entry point**
  - `sendMSG()` is the public API used by modules to send a message buffer.
- **Chunking scheme**
  - Messages larger than the configured maximum UDP payload are split into multiple chunks.
  - Each chunk has a 2-byte header at the beginning that encodes the sequence number.
  - The final chunk is specially marked with an end-of-message marker (`0xFFFF`).
- **Transmission**
  - Chunks are sent sequentially via UDP.
  - A small delay (about 10 ms) is inserted between chunks to reduce packet loss.
  - Sending is protected by a mutex to ensure thread-safe access from multiple callers.

### 3. Reception and Reassembly
- **Receiver loop**
  - The receiver thread runs an internal loop that blocks on `recvfrom()` for incoming UDP packets.
- **Chunk processing**
  - For each received packet, the first 2 bytes are interpreted as the chunk header.
  - The header encodes the chunk number; a value of `0xFFFF` indicates the final chunk.
  - When a new message sequence starts, any previously stored chunks are cleared.
  - The payload (excluding the 2-byte header) is stored as a chunk.
- **Message reconstruction**
  - Once the final chunk is received, all stored chunks are concatenated in order.
  - The reassembled buffer represents the original message sent by the peer.
- **Delivery to application**
  - The reconstructed message is passed to the application via the callback interface.

### 4. Periodic Module Identification
- **ID configuration**
  - `setJsonId()` is used externally to configure the module JSON identifier.
  - The identifier is stored internally (e.g., in `m_JsonID`).
- **Broadcast loop**
  - The sender-ID thread periodically wakes (about once per second).
  - If a JSON ID is set, it is sent using the same `sendMSG()` mechanism.
  - This provides network discovery and identification of the module.

### 5. Integration with DE Modules
- **Callback interface**
  - Modules implement `CCallBack_UDPClient` and override `onReceive()` to handle messages.
- **UDP client usage**
  - A `CUDPClient` instance is created within each module.
  - Modules use `sendMSG()` to transmit messages through the databus.
  - `isStarted()` can be used to query whether the UDP client is fully initialized and running.

This protocol allows DroneEngage modules to exchange arbitrarily sized messages reliably over UDP using a simple chunking and reassembly scheme, while also supporting automatic module discovery through periodic ID broadcasting.

## UDP Client Protocol Diagram

```text
UDP Client Initialization Flow
├── Constructor with callback setup <-- 1a
├── Socket creation & configuration <-- udpClient.cpp:45
│   ├── UDP socket creation <-- 1b
│   └── Socket binding to local port <-- 1c
└── Thread startup sequence <-- udpClient.cpp:97
    ├── startReceiver() call <-- udpClient.cpp:113
    ├── startSenderID() call <-- udpClient.cpp:114
    └── Mark client as started <-- 1d
```

```text
Message Sending with Chunking Protocol
├── sendMSG() entry point <-- udpClient.cpp:303
│   ├── Chunk size calculation <-- 2a
│   │   ├── Create chunk buffer <-- udpClient.cpp:327
│   │   │   ├── Encode chunk header <-- 2c
│   │   │   │   └── Special end packet <-- 2b
│   │   │   └── Copy message payload <-- udpClient.cpp:346
│   │   └── UDP transmission <-- 2d
│   │       └── Prevent packet loss <-- 2e
│   └── Loop for remaining chunks <-- udpClient.cpp:321
└── Thread-safe sending <-- 2f
    └── Mutex lock protection <-- udpClient.cpp:311
```

```text
UDP Reception Loop
├── InternalReceiverEntry() main loop <-- udpClient.cpp:183
│   ├── recvfrom() blocks for data <-- 3a
│   ├── Extract chunk header from buffer <-- udpClient.cpp:212
│   │   ├── Decode chunk number <-- 3b
│   │   └── Check for end marker (0xFFFF) <-- 3c
│   ├── Handle chunk sequencing <-- 3d
│   │   ├── Clear chunks on new message <-- 3e
│   │   └── Store payload data <-- 3f
│   ├── Message reassembly when complete <-- 3g
│   │   └── Concatenate all chunks <-- 3h
│   └── Deliver to application <-- 3i
│       └── Invoke callback with data <-- 3j
└── Error handling and loop continuation <-- 3k
```

```text
Module ID Broadcasting System
├── setJsonId() external call <-- 4a
│   └── m_JsonID assignment <-- 4b
└── InternelSenderIDEntry() thread <-- udpClient.cpp:282
    ├── while (!stopped) loop <-- 4c
    │   ├── check if ID is set <-- 4d
    │   ├── sendMSG() call <-- 4e
    │   │   └── uses main send mechanism <-- 4f
    │   └── sleep_for(1 second) <-- 4g
    └── startSenderID() launch <-- 4h
        └── std::thread creation <-- 4i
```

```text
DE Module Architecture Integration
├── Abstract Callback Interface <-- udpClient.hpp:21
│   └── CCallBack_UDPClient base class <-- 5a
├── UDP Client Implementation <-- udpClient.hpp:27
│   ├── CUDPClient class <-- udpClient.hpp:27
│   │   ├── sendMSG() public API <-- 5b
│   │   └── isStarted() status check <-- 5c
│   └── Thread-based communication <-- udpClient.hpp:60
│       ├── Receiver thread <-- 5d
│       └── ID broadcaster thread <-- 5e
└── Module System Usage
    ├── Inherits callback interface <-- 5f
    ├── Instantiates UDP client <-- 5g
    └── Implements onReceive() method <-- 5h
        └── Processes reassembled messages <-- 5i
```

## DroneEngage Module Communication Architecture

This section summarizes how the DroneEngage module, facade, parser, and UDP transport work together to provide bidirectional communication using the `de_common` databus.

For an architectural codemap and more details, see:
https://windsurf.com/codemaps/558eb2f3-28d4-488c-b503-96e8017e4d8a-bf8b4864a72d0584

### 1. Module Initialization and Registration
- **Module definition**
  - Application calls `CModule::defineModule()` to set module class, ID, key, version, and message filter.
- **Initialization flow**
  - Application calls `CModule::init()` with server IP, broadcast port, and listening port.
  - `cUDPClient.init()` configures the UDP socket, bind address, and server endpoint.
  - `createJSONID(true)` builds the `TYPE_AndruavModule_ID` JSON identification message.
  - `cUDPClient.start()` launches:
    - The receiver thread (`startReceiver()` → `InternalReceiverEntry`).
    - The periodic ID sender thread (`startSenderID()` → `InternelSenderIDEntry`).
  - After initialization, the module is ready for bidirectional communication with the communicator server.

### 2. Outbound Message Flow (Application → Facade → Module → UDP)
- **Facade entry point**
  - Application uses `CFacade_Base` APIs (e.g. `requestID()`) to send high-level commands.
  - These methods call `m_module.sendJMSG()` on the underlying `CModule` singleton.
- **Module send path**
  - `CModule::sendJMSG()` constructs a JSON message with:
    - Module key and identity metadata.
    - Target party / group IDs.
    - Routing type (intermodule, group, individual).
    - Message type and command.
  - The JSON is serialized to a string.
  - The serialized string is passed to `cUDPClient.sendMSG()`.
- **UDP transport**
  - `CUDPClient::sendMSG()` splits the payload into chunks, adds headers, and sends via UDP to the communicator server (as described in the UDP protocol section above).

### 3. Inbound Message Flow (UDP → Module → Parser → Application)
- **UDP reception**
  - The receiver thread (`InternalReceiverEntry`) blocks on `recvfrom()` and reassembles chunks into full messages.
  - When a complete message is available, `m_callback->onReceive()` is invoked.
- **Module onReceive**
  - `CModule` implements `onReceive()` from `CCallBack_UDPClient`.
  - It parses the raw message into `Json_de`, validates required fields, and checks routing type.
  - Special intermodule messages (e.g. ID registration) are handled internally.
  - If an application callback (`m_OnReceive`) is registered, `CModule` forwards the message (raw buffer and JSON) to it.
- **Parser processing**
  - Typically, `m_OnReceive` calls into a `CAndruavMessageParserBase`-derived parser.
  - `parseMessage()` extracts the message type and:
    - Optionally handles `TYPE_AndruavMessage_RemoteExecute` via `parseRemoteExecute()`.
    - Uses `parseDefaultCommand()` for common commands (e.g. config actions).
    - Dispatches to `parseCommand()` (virtual) for module-specific handling.

### 4. Parser and Facade Collaboration
- **Parser-to-facade link**
  - `CAndruavMessageParserBase` holds a reference to `CFacade_Base::getInstance()`.
  - When needed (e.g. responding to a configuration request), the parser uses facade APIs to send replies back through the normal outbound path.

### 5. Singleton and Callback Architecture
- **Singletons**
  - `CModule::getInstance()` provides a single global module instance.
  - `CFacade_Base::getInstance()` holds a reference to this module singleton.
  - Parsers hold a reference to the facade singleton.
- **Callback chain**
  - `CModule` inherits from `CCallBack_UDPClient` and passes `this` to `CUDPClient` in its constructor.
  - `CUDPClient` stores the callback pointer and calls `onReceive()` after reassembly.
  - `CModule` exposes `setMessageOnReceive()` allowing the application to register a function pointer (`m_OnReceive`).
  - This function pointer is invoked to hand messages off to the parser or other application logic.

Overall, this architecture provides a clear separation of concerns:
- UDP transport (`CUDPClient`).
- Module-level routing and registration (`CModule`).
- High-level API for application code (`CFacade_Base`).
- Extensible message handling via virtual parser interfaces (`CAndruavMessageParserBase`).
