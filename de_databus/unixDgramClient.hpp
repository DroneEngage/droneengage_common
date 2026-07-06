#ifndef CUNIXDGRAMCLIENT_H
#define CUNIXDGRAMCLIENT_H

#include <string>
#include <thread>
#include <mutex>
#include <sys/un.h>
#include "udpClient.hpp"

#ifndef MAXLINE
#define MAXLINE 65507
#endif

#define MAX_UNIX_DGRAM_PACKET_SIZE 0xffff
#define DEFAULT_UNIX_DGRAM_PACKET_SIZE 8192

namespace de {
namespace comm {

class CUnixDgramClient {
    public:
        CUnixDgramClient(CCallBack_UDPClient* callback) {
            m_callback = callback;
        }

        ~CUnixDgramClient();

        void init(const char* brokerSocketPath, const char* ownSocketPath, int chunkSize);
        void start();
        void stop();
        void setJsonId(std::string jsonID);
        void sendMSG(const char* msg, const int length);

        inline bool isStarted() const { return m_started; }

    protected:
        void startReceiver();
        void startSenderID();

        void InternalReceiverEntry();
        void InternelSenderIDEntry();

        struct sockaddr_un* m_OwnAddress = nullptr;
        struct sockaddr_un* m_BrokerAddress = nullptr;
        int m_SocketFD = -1;
        std::thread m_threadSenderID, m_threadCreateUnixSocket;

        std::string m_JsonID;
        CCallBack_UDPClient* m_callback = nullptr;

    protected:
        bool m_started = false;
        bool m_stopped_called = false;
        std::mutex m_lock;
        std::mutex m_lock2;

        char buffer[MAXLINE];
        int m_chunkSize;
};

} // namespace comm
} // namespace de

#endif // CUNIXDGRAMCLIENT_H
