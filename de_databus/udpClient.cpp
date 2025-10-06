#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "../helpers/colors.hpp"
#include "../helpers/json_nlohmann.hpp"
using Json_de = nlohmann::json;

#include "udpClient.hpp"

#ifndef MAXLINE
#define MAXLINE 0xffff
#endif

de::comm::CUDPClient::~CUDPClient()
{
#ifdef DEBUG
    std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: ~CUDPClient" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif

    if (!m_stopped_called)
    {
#ifdef DEBUG
        std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: ~CUDPClient calling stop" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
        stop();
    }

#ifdef DEBUG
    std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: ~CUDPClient completed" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
}

/**
 * @brief
 *
 * @param targetIP communication server ip
 * @param broadcatsPort communication server port
 * @param host de-module listening ips default is 0.0.0.0
 * @param listenningPort de-module listerning port.
 */
void de::comm::CUDPClient::init(const char *targetIP, int broadcatsPort, const char *host, int listenningPort, int chunkSize)
{
    // pthread initialization
    m_thread = pthread_self();                  // get pthread ID
    pthread_setschedprio(m_thread, SCHED_FIFO); // setting priority

    if (chunkSize >= MAX_UDP_DATABUS_PACKET_SIZE)
    {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Invalid UDP packet size: " << chunkSize << _NORMAL_CONSOLE_TEXT_ << std::endl;
        exit(EXIT_FAILURE);
    }

    m_chunkSize = chunkSize;

    // Create socket
    m_SocketFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_SocketFD < 0)
    {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Socket creation failed: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        exit(EXIT_FAILURE);
    }

    m_ModuleAddress = new (struct sockaddr_in)();
    m_CommunicatorModuleAddress = new (struct sockaddr_in)();
    memset(m_ModuleAddress, 0, sizeof(struct sockaddr_in));
    memset(m_CommunicatorModuleAddress, 0, sizeof(struct sockaddr_in));

    // Configure module address
    m_ModuleAddress->sin_family = AF_INET;
    m_ModuleAddress->sin_port = htons(listenningPort);
    m_ModuleAddress->sin_addr.s_addr = inet_addr(host);

    // Configure communicator address
    m_CommunicatorModuleAddress->sin_family = AF_INET;
    m_CommunicatorModuleAddress->sin_port = htons(broadcatsPort);
    m_CommunicatorModuleAddress->sin_addr.s_addr = inet_addr(targetIP);

    // Bind socket
    if (bind(m_SocketFD, (const struct sockaddr *)m_ModuleAddress, sizeof(struct sockaddr_in)) < 0)
    {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "UDP bind failed: " << host << ":" << listenningPort << " - " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
        close(m_SocketFD);
        delete m_ModuleAddress;
        delete m_CommunicatorModuleAddress;
        exit(EXIT_FAILURE);
    }

    std::cout << _LOG_CONSOLE_BOLD_TEXT << "UDP Listener at " << _INFO_CONSOLE_TEXT << host << ":" << listenningPort << _NORMAL_CONSOLE_TEXT_ << std::endl;
    std::cout << _LOG_CONSOLE_BOLD_TEXT << "Expected Comm Server at " << _INFO_CONSOLE_TEXT << targetIP << ":" << broadcatsPort << _NORMAL_CONSOLE_TEXT_ << std::endl;
    std::cout << _LOG_CONSOLE_BOLD_TEXT << "UDP Max Packet Size " << _INFO_CONSOLE_TEXT << chunkSize << _NORMAL_CONSOLE_TEXT_ << std::endl;
}

void de::comm::CUDPClient::start()
{
#ifndef DE_DISABLE_TRY
    try
    {
#endif
        if (m_starrted)
        {
            std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Start called twice" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#ifndef DE_DISABLE_TRY
            throw std::runtime_error("Start called twice");
#else
        return;
#endif
        }

        startReceiver();
        startSenderID();
        m_starrted = true;
#ifndef DE_DISABLE_TRY
    }
    catch (const std::exception &e)
    {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Error in start: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }
#endif
}

void de::comm::CUDPClient::startReceiver()
{
    m_threadCreateUDPSocket = std::thread{[&]()
                                          { InternalReceiverEntry(); }};
}

void de::comm::CUDPClient::startSenderID()
{
    m_threadSenderID = std::thread{[&]()
                                   { InternelSenderIDEntry(); }};
}

void de::comm::CUDPClient::stop()
{
#ifdef DEBUG
    std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: Stop" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif

    m_stopped_called = true;

#ifndef DE_DISABLE_TRY
    try
    {
#endif
        if (m_SocketFD != -1)
        {
            std::cout << _SUCCESS_CONSOLE_BOLD_TEXT_ << "Close UDP Socket" << _NORMAL_CONSOLE_TEXT_ << std::endl;
            shutdown(m_SocketFD, SHUT_RDWR);
            close(m_SocketFD);
            m_SocketFD = -1;
        }

        if (m_starrted)
        {
            if (m_threadCreateUDPSocket.joinable())
                m_threadCreateUDPSocket.join();
            if (m_threadSenderID.joinable())
                m_threadSenderID.join();
            m_starrted = false;
        }

        delete m_ModuleAddress;
        delete m_CommunicatorModuleAddress;
        m_ModuleAddress = nullptr;
        m_CommunicatorModuleAddress = nullptr;

#ifdef DEBUG
        std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: Stop completed" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
#ifndef DE_DISABLE_TRY
    }
    catch (const std::exception &e)
    {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Error in stop: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }
#endif
}

void de::comm::CUDPClient::InternalReceiverEntry()
{
#ifdef DEBUG
    std::cout << "CUDPClient::InternalReceiverEntry called" << std::endl;
#endif

    struct sockaddr_in cliaddr;
    __socklen_t sender_address_size = sizeof(cliaddr);
    std::vector<std::vector<uint8_t>> receivedChunks;

#ifndef DE_DISABLE_TRY
    try
    {
#endif
        while (!m_stopped_called)
        {
            const int n = recvfrom(m_SocketFD, (char *)buffer, MAXLINE, MSG_WAITALL, (struct sockaddr *)&cliaddr, &sender_address_size);
#ifdef DDEBUG
            std::cout << "CUDPClient::InternalReceiverEntry recvfrom" << std::endl;
#endif

            if (n > 0)
            {
                if (n < 2)
                {
                    std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Received packet too small: " << n << " bytes" << _NORMAL_CONSOLE_TEXT_ << std::endl;
                    continue;
                }

                const uint16_t chunkNumber = (buffer[1] << 8) | buffer[0];
                
                // Last packet is always equal to 0xFFFF regardless of its actual number.
                const bool end = (chunkNumber == 0xFFFF);

                if (chunkNumber == 0)
                   // clear any corrupted/incomplete packets
                   receivedChunks.clear();

                // Store the received chunk in the map
                receivedChunks.emplace_back(buffer + 2 * sizeof(uint8_t), buffer + n);

                if (end)
                {
                    // Concatenate the chunks in order
                    std::vector<uint8_t> concatenatedData;
                    for (auto &chunk : receivedChunks)
                    {
                        concatenatedData.insert(concatenatedData.end(), chunk.begin(), chunk.end());
                    }
                    // NOTICE WE DONT KNOW
                    // if this is a test message or text and binary
                    // so we inject null at the end
                    // it should be removed later if it is binary.
                    concatenatedData.push_back(0);

                    // Call the onReceive callback with the concatenated data
                    if (m_callback != nullptr)
                    {
                        m_callback->onReceive((const char *)concatenatedData.data(), concatenatedData.size());
                    }

                    // Clear the map for the next set of chunks
                    receivedChunks.clear();
                }
            }
            else
            {
// If socket was shutdown, break loop; otherwise continue
#ifdef DEBUG
                std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "recvfrom failed: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
                if (m_stopped_called)
                    break;
            }
        }
#ifndef DE_DISABLE_TRY
    }
    catch (const std::exception &e)
    {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Error in InternalReceiverEntry: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }
#endif

#ifdef DDEBUG
    std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: InternalReceiverEntry EXIT" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
}

/**
 * Store ID Card in JSON
 */
void de::comm::CUDPClient::setJsonId(std::string jsonID)
{
    m_JsonID = jsonID;
}

/**
 * Sending ID Periodically
 **/
void de::comm::CUDPClient::InternelSenderIDEntry()
{
#ifdef DEBUG
    std::cout << "InternelSenderIDEntry called" << std::endl;
#endif

    while (!m_stopped_called)
    {
        std::lock_guard<std::mutex> lock(m_lock2);
        if (!m_JsonID.empty())
        {
            sendMSG(m_JsonID.c_str(), m_JsonID.length());
        }
        std::this_thread::sleep_for(std::chrono::seconds(1)); 
    }

#ifdef DDEBUG
    std::cout << __FILE__ << "." << __FUNCTION__ << " line:" << __LINE__ << "  " << _LOG_CONSOLE_TEXT << "DEBUG: InternelSenderIDEntry EXIT" << _NORMAL_CONSOLE_TEXT_ << std::endl;
#endif
}

void de::comm::CUDPClient::sendMSG(const char *msg, const int length)
{
    if (m_chunkSize <= 0)
    {
        std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "Invalid chunk size: " << m_chunkSize << _NORMAL_CONSOLE_TEXT_ << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(m_lock);

#ifndef DE_DISABLE_TRY
    try
    {
#endif
        int remainingLength = length;
        int offset = 0;
        int chunk_number = 0;

        while (remainingLength > 0)
        {
            int chunkLength = std::min(m_chunkSize, remainingLength);
            remainingLength -= chunkLength;

            // Create a new message with the chunk size + sizeof(uint8_t)
            char chunkMsg[chunkLength + 2 * sizeof(uint8_t)];

            // Set the first byte as chunk number
            if (remainingLength == 0)
            {
                // IMPORTANT: Last packet is always equal to 255 (0xff) regardless if its actual number.
                chunkMsg[0] = 0xFF;
                chunkMsg[1] = 0xFF;
            }
            else
            {
                chunkMsg[0] = static_cast<uint8_t>(chunk_number & 0xFF);
                chunkMsg[1] = static_cast<uint8_t>((chunk_number >> 8) & 0xFF);
            }

#ifdef DDEBUG
            std::cout << "chunkNumber:" << chunk_number << " :chunkLength :" << chunkLength << std::endl;
#endif

            std::memcpy(chunkMsg + 2 * sizeof(uint8_t), msg + offset, chunkLength);

            const int sent = sendto(m_SocketFD, chunkMsg, chunkLength + 2 * sizeof(uint8_t),
                              MSG_CONFIRM, (const struct sockaddr *)m_CommunicatorModuleAddress,
                              sizeof(struct sockaddr_in));

            if (sent < 0)
            {
                std::cout << _ERROR_CONSOLE_BOLD_TEXT_ << "sendto failed: " << strerror(errno) << _NORMAL_CONSOLE_TEXT_ << std::endl;
                break;
            }

            if (remainingLength != 0)
            {
                // fast sending causes packet loss.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            offset += chunkLength;
            chunk_number++;
        }
#ifndef DE_DISABLE_TRY
    }
    catch (const std::exception &e)
    {
        std::cerr << _ERROR_CONSOLE_BOLD_TEXT_ << "Error in sendMSG: " << e.what() << _NORMAL_CONSOLE_TEXT_ << std::endl;
    }
#endif
}