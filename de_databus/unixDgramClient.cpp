#include "unixDgramClient.hpp"
#include "chunk_protocol.hpp"
#include "../helpers/colors.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <chrono>
#include <map>
#include <string>

namespace de {
namespace comm {

CUnixDgramClient::~CUnixDgramClient() {
    stop();
    
    if (m_OwnAddress) {
        delete m_OwnAddress;
        m_OwnAddress = nullptr;
    }
    
    if (m_BrokerAddress) {
        delete m_BrokerAddress;
        m_BrokerAddress = nullptr;
    }
}

void CUnixDgramClient::init(const char* brokerSocketPath, const char* ownSocketPath, int chunkSize) {
    if (chunkSize <= 0)
    {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Invalid chunk size (must be > 0): " << chunkSize << _NORMAL_CONSOLE_TEXT_ << std::endl;
        exit(EXIT_FAILURE);
    }

    m_chunkSize = chunkSize;
    
    // Create own socket address
    m_OwnAddress = new struct sockaddr_un();
    memset(m_OwnAddress, 0, sizeof(struct sockaddr_un));
    m_OwnAddress->sun_family = AF_UNIX;
    strncpy(m_OwnAddress->sun_path, ownSocketPath, sizeof(m_OwnAddress->sun_path) - 1);
    
    // Create broker socket address
    m_BrokerAddress = new struct sockaddr_un();
    memset(m_BrokerAddress, 0, sizeof(struct sockaddr_un));
    m_BrokerAddress->sun_family = AF_UNIX;
    strncpy(m_BrokerAddress->sun_path, brokerSocketPath, sizeof(m_BrokerAddress->sun_path) - 1);
    
    // Ensure the parent directory of the own socket path exists (mkdir -p).
    // bind() on AF_UNIX requires the containing directory to already exist;
    // it will not create it. This lets modules run without a pre-created
    // /run/de_comm, even if they start before de_comm has created the dir.
    //
    // Multi-module / multi-instance safety:
    //  - The parent dir (/run/de_comm) is SHARED by all modules and the broker.
    //    Concurrent mkdir() is race-safe: the first caller creates it, the rest
    //    get EEXIST which we silently ignore (standard mkdir -p semantics).
    //  - The socket FILE is unique per module instance:
    //      /run/de_comm/de_comm_<module_id>_<module_key>.sock
    //    (see de_module.cpp). Each module only unlinks+binds its OWN file, so
    //    multiple modules of the same type (or different types) can run
    //    simultaneously without colliding. This block never touches the socket
    //    file itself — only the parent directory tree.
    {
        std::string path(ownSocketPath);
        std::size_t pos = path.find_last_of('/');
        if (pos != std::string::npos && pos > 0) {
            std::string parent = path.substr(0, pos);
            std::size_t start = 0;
            while (true) {
                std::size_t next = parent.find('/', start + 1);
                std::string sub = (next == std::string::npos) ? parent : parent.substr(0, next);
                if (!sub.empty()) {
                    if (mkdir(sub.c_str(), 0775) == 0) {
#ifdef DEBUG_UNIX
                        std::cout << _INFO_CONSOLE_TEXT << "CUnixDgramClient::init - created dir: " << sub << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
                    } else if (errno != EEXIST) {
                        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Failed to create socket dir '" << sub << "': " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
                    }
                }
                if (next == std::string::npos) break;
                start = next;
            }
        }
    }
    
    // Remove stale socket file if exists
    unlink(ownSocketPath);
}

void CUnixDgramClient::start() {
    // Create Unix domain socket
    m_SocketFD = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_SocketFD < 0) {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Failed to create Unix socket: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return;
    }
    
    // Bind to own socket path
    if (bind(m_SocketFD, (struct sockaddr*)m_OwnAddress, sizeof(struct sockaddr_un)) < 0) {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Failed to bind Unix socket: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        close(m_SocketFD);
        m_SocketFD = -1;
        return;
    }
    
    startReceiver();
    startSenderID();
    
    m_started = true;
}

void CUnixDgramClient::stop() {
    if (!m_started) return;
    
    m_stopped_called = true;
    
    if (m_SocketFD >= 0) {
        shutdown(m_SocketFD, SHUT_RDWR);
    }
    
    if (m_threadCreateUnixSocket.joinable()) {
        m_threadCreateUnixSocket.join();
    }
    
    if (m_threadSenderID.joinable()) {
        m_threadSenderID.join();
    }
    
    if (m_SocketFD >= 0) {
        close(m_SocketFD);
        m_SocketFD = -1;
    }
    
    // Remove socket file
    if (m_OwnAddress) {
        unlink(m_OwnAddress->sun_path);
    }
    
    m_started = false;
}

void CUnixDgramClient::setJsonId(std::string jsonID) {
    m_JsonID = jsonID;
}

void CUnixDgramClient::sendMSG(const char* msg, const int length) {
    if (m_chunkSize <= 0) {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Invalid chunk size: " << m_chunkSize << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_lock);

#ifndef DE_DISABLE_TRY
    try {
#endif
        int remainingLength = length;
        int offset = 0;
        int chunk_number = 0;

        while (remainingLength > 0) {
            int chunkLength = std::min(m_chunkSize, remainingLength);
            remainingLength -= chunkLength;

            // Create a new message with the chunk size + 2 * sizeof(uint8_t)
            char chunkMsg[chunkLength + 2 * sizeof(uint8_t)];

            // Prepare chunk header using shared helper
            chunk_protocol::prepareChunkHeader(chunk_number, (remainingLength == 0), 
                                              reinterpret_cast<uint8_t*>(chunkMsg));

#ifdef DDEBUG
            std::cout << "chunkNumber:" << chunk_number << " :chunkLength :" << chunkLength << std::endl;
#endif

            std::memcpy(chunkMsg + 2 * sizeof(uint8_t), msg + offset, chunkLength);

            int sent = sendto(m_SocketFD, chunkMsg, chunkLength + 2 * sizeof(uint8_t),
                              0, (const struct sockaddr*)m_BrokerAddress,
                              sizeof(struct sockaddr_un));

            if (sent < 0) {
                // Retry on ENOENT (broker socket not yet created) with exponential backoff
                if (errno == ENOENT && chunk_number == 0) {
                    for (int retry = 0; retry < 5; ++retry) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100 * (1 << retry)));
                        sent = sendto(m_SocketFD, chunkMsg, chunkLength + 2 * sizeof(uint8_t),
                                      0, (const struct sockaddr*)m_BrokerAddress,
                                      sizeof(struct sockaddr_un));
                        if (sent >= 0) break;
                    }
                }
                if (sent < 0) {
                    std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "sendto failed: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
                    break;
                }
            }

            if (remainingLength != 0)
            {
                // fast sending causes packet loss even on Unix domain sockets
                // (bounded SO_RCVBUF can overflow just like UDP).
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            offset += chunkLength;
            chunk_number++;
        }
#ifndef DE_DISABLE_TRY
    }
    catch (const std::exception& e) {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Error in sendMSG: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }
#endif
}

void CUnixDgramClient::startReceiver() {
    m_threadCreateUnixSocket = std::thread(&CUnixDgramClient::InternalReceiverEntry, this);
}

void CUnixDgramClient::startSenderID() {
    m_threadSenderID = std::thread(&CUnixDgramClient::InternelSenderIDEntry, this);
}

void CUnixDgramClient::InternalReceiverEntry() {
#ifdef DEBUG
    std::cout << "CUnixDgramClient::InternalReceiverEntry called" << std::endl;
#endif

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(m_SocketFD, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    struct sockaddr_un cliaddr;
    std::map<std::string, std::vector<std::vector<uint8_t>>> receivedChunksBySource;

#ifndef DE_DISABLE_TRY
    try {
#endif
        while (!m_stopped_called) {
            __socklen_t sender_address_size = sizeof(cliaddr);
            const int n = recvfrom(m_SocketFD, (char*)buffer, MAXLINE, MSG_WAITALL,
                                   (struct sockaddr*)&cliaddr, &sender_address_size);
#ifdef DDEBUG
            std::cout << "CUnixDgramClient::InternalReceiverEntry recvfrom" << std::endl;
#endif

            if (n > 0) {
                if (n < 2) {
                    std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Received packet too small: " << n << " bytes" << _NORMAL_CONSOLE_TEXT_ << std::endl;
                    continue;
                }

                const uint16_t chunkNumber = chunk_protocol::parseChunkNumber(reinterpret_cast<uint8_t*>(buffer));
                const bool end = chunk_protocol::isEndChunk(chunkNumber);

                if (chunkNumber == 0)
                    receivedChunksBySource[cliaddr.sun_path].clear();

                // Store the received chunk in the map
                std::vector<std::vector<uint8_t>>& chunkVector = receivedChunksBySource[cliaddr.sun_path];
                chunkVector.emplace_back(buffer + 2 * sizeof(uint8_t), buffer + n);

                if (end) {
                    // Reassemble chunks using shared helper
                    std::vector<uint8_t> concatenatedData = chunk_protocol::reassembleChunks(chunkVector);
                    
                    // Add null terminator for compatibility
                    concatenatedData.push_back(0);

                    // Call the onReceive callback
                    if (m_callback != nullptr) {
                        m_callback->onReceive((const char*)concatenatedData.data(), concatenatedData.size());
                    }

                    // Clear the map for the next set of chunks
                    receivedChunksBySource[cliaddr.sun_path].clear();
                }
            }
            else {
#ifdef DEBUG_UNIX
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "recvfrom failed: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
                }
#endif
                if (m_stopped_called)
                    break;
            }
        }
#ifndef DE_DISABLE_TRY
    }
    catch (const std::exception& e) {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Error in InternalReceiverEntry: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }
#endif

#ifdef DDEBUG
    std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: InternalReceiverEntry EXIT" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
}

void CUnixDgramClient::InternelSenderIDEntry() {
    while (!m_stopped_called) {
        if (!m_JsonID.empty()) {
            sendMSG(m_JsonID.c_str(), m_JsonID.length());
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace comm
} // namespace de
