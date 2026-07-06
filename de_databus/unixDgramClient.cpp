#include "unixDgramClient.hpp"
#include "chunk_protocol.hpp"
#include "../helpers/colors.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <chrono>
#include <map>

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

            const int sent = sendto(m_SocketFD, chunkMsg, chunkLength + 2 * sizeof(uint8_t),
                              0, (const struct sockaddr*)m_BrokerAddress,
                              sizeof(struct sockaddr_un));

            if (sent < 0) {
                std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "sendto failed: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
                break;
            }

            // No delay needed for Unix domain sockets (local IPC, no packet loss)
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
