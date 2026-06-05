#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

// Localhost automation command server for the editor.
//
// Design constraints:
//   * GL and ImGui state must be touched only on the main thread. So the socket
//     accept/read loop runs on a worker thread, but it does NOT execute
//     commands. Instead it enqueues each parsed command and blocks on a future
//     for the result. The editor's main loop calls drain() once per frame,
//     which pops queued commands, runs the handler (on the main/GL thread), and
//     fulfills the futures.
//   * Bound to 127.0.0.1 only. Never 0.0.0.0. No external exposure.
//   * Line-delimited JSON: one request object per line, one response object per
//     line. Keeps the client trivial (stdlib socket + json).
//
// The handler maps a request JSON object to a response JSON object. It runs on
// the main thread inside drain(), so it can freely call GL/ImGui/editor state.
class AutomationServer
{
public:
    // A command handler: takes the parsed request, returns the response JSON.
    // Invoked on the main thread (from drain()).
    using Handler = std::function<nlohmann::json(const nlohmann::json&)>;

    AutomationServer() = default;
    ~AutomationServer();

    AutomationServer(const AutomationServer&) = delete;
    AutomationServer& operator=(const AutomationServer&) = delete;

    // Start listening on 127.0.0.1:port and spawn the accept thread. Returns
    // false (and logs to stderr) if the socket cannot be created/bound. The
    // handler is stored and invoked from drain() on the main thread.
    bool start(uint16_t port, Handler handler);

    // Pop and execute all queued commands on the calling (main) thread. Call
    // once per frame from the editor loop. Returns the number of commands run.
    // If a handler requests shutdown (see requestQuit()), drain() still returns
    // normally; the editor checks shouldQuit().
    int drain();

    // True once a command handler has requested application shutdown (the "quit"
    // command). The editor's main loop should break when this becomes true.
    bool shouldQuit() const { return m_shouldQuit.load(); }

    // Called by a handler (on the main thread) to request shutdown.
    void requestQuit() { m_shouldQuit.store(true); }

    // Stop the accept thread and close all sockets. Safe to call multiple times.
    void stop();

    bool running() const { return m_running.load(); }
    uint16_t port() const { return m_port; }

private:
    // One queued unit of work: the parsed request plus a promise the worker
    // thread is waiting on for the response.
    struct PendingCommand
    {
        nlohmann::json request;
        std::promise<nlohmann::json> response;
    };

    void acceptLoop();
    void handleConnection(int clientFd);

    // Enqueue a request from a worker thread and block until the main thread
    // (drain) produces a response.
    nlohmann::json dispatch(const nlohmann::json& request);

    Handler m_handler;

    std::atomic<int> m_listenFd{-1};
    uint16_t m_port = 0;
    std::thread m_acceptThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shouldQuit{false};

    std::mutex m_queueMutex;
    std::queue<std::shared_ptr<PendingCommand>> m_queue;
};
