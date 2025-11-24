# de_common Module

## UDP Client Communication Protocol

This module implements a UDP-based client used by DroneEngage modules to exchange messages with the `de_comm` applications. The client supports chunked transmission of large messages, reassembly on reception, and periodic module identification broadcasting.

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
