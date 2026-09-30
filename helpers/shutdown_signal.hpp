#ifndef DE_SHUTDOWN_SIGNAL_HPP_
#define DE_SHUTDOWN_SIGNAL_HPP_

#include <csignal>
#include <chrono>
#include <thread>
#include <unistd.h>

/**
 * @brief Common SIGINT/SIGTERM handling for DroneEngage modules.
 *
 * @details The signal handler only raises a flag (async-signal-safe). The
 * module's main thread polls requested() / waitForRequest() and then runs its
 * own uninit() in normal context - never inside the handler, where joins,
 * mutexes and iostreams can deadlock or self-join.
 *
 * A second signal while shutdown is in progress forces an immediate _exit(),
 * so a shutdown stuck in a blocking call (e.g. network connect) can still be
 * interrupted.
 *
 * Usage:
 *   de::shutdown::installHandlers();   // in init()
 *   de::shutdown::waitForRequest();    // in main()
 *   uninit();
 */
namespace de
{
namespace shutdown
{
    inline volatile std::sig_atomic_t g_requested = 0;

    inline void writeStderr(const char *msg, const size_t len)
    {
        const ssize_t written = write(STDERR_FILENO, msg, len);
        (void)written;
    }

    inline void onSignal(int sig)
    {
        if (g_requested)
        {
            static const char msg[] = "\nFORCED EXIT\n";
            writeStderr(msg, sizeof(msg) - 1);
            _exit(128 + sig);
        }

        g_requested = 1;
        static const char msg[] = "\nTERMINATING AT USER REQUEST (repeat to force exit)\n";
        writeStderr(msg, sizeof(msg) - 1);
    }

    /**
     * @brief install handler for SIGINT and SIGTERM.
     */
    inline void installHandlers()
    {
        struct sigaction sa = {};
        sa.sa_handler = onSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);
    }

    /**
     * @brief true once a termination signal was received or request() was called.
     */
    inline bool requested() { return g_requested != 0; }

    /**
     * @brief request shutdown from code (same effect as a signal).
     */
    inline void request() { g_requested = 1; }

    /**
     * @brief block the calling thread until shutdown is requested.
     */
    inline void waitForRequest(const std::chrono::milliseconds poll = std::chrono::milliseconds(200))
    {
        while (!requested())
        {
            std::this_thread::sleep_for(poll);
        }
    }
}
}

#endif
