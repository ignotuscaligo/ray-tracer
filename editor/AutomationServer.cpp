#include "AutomationServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

using nlohmann::json;

AutomationServer::~AutomationServer()
{
    stop();
}

bool AutomationServer::start(uint16_t port, Handler handler)
{
    if (m_running.load())
    {
        std::fprintf(stderr, "AutomationServer already running\n");
        return false;
    }

    m_handler = std::move(handler);

    m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0)
    {
        std::fprintf(stderr, "AutomationServer: socket() failed: %s\n",
                     std::strerror(errno));
        return false;
    }

    int yes = 1;
    ::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // 127.0.0.1 ONLY — never INADDR_ANY. No external exposure.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(m_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::fprintf(stderr, "AutomationServer: bind(127.0.0.1:%u) failed: %s\n",
                     static_cast<unsigned>(port), std::strerror(errno));
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    if (::listen(m_listenFd, 4) < 0)
    {
        std::fprintf(stderr, "AutomationServer: listen() failed: %s\n",
                     std::strerror(errno));
        ::close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    m_port = port;
    m_running.store(true);
    m_acceptThread = std::thread([this]() { acceptLoop(); });

    std::fprintf(stderr, "AutomationServer listening on 127.0.0.1:%u\n",
                 static_cast<unsigned>(port));
    return true;
}

void AutomationServer::stop()
{
    if (!m_running.exchange(false))
    {
        // Already stopped; still join in case start failed partway.
        if (m_acceptThread.joinable())
        {
            m_acceptThread.join();
        }
        return;
    }

    // Shut down the listening socket to unblock accept().
    if (m_listenFd >= 0)
    {
        ::shutdown(m_listenFd, SHUT_RDWR);
        ::close(m_listenFd);
        m_listenFd = -1;
    }

    if (m_acceptThread.joinable())
    {
        m_acceptThread.join();
    }

    // Fail any commands still queued so blocked workers (if any) unblock.
    std::lock_guard<std::mutex> lock(m_queueMutex);
    while (!m_queue.empty())
    {
        auto cmd = m_queue.front();
        m_queue.pop();
        try
        {
            cmd->response.set_value(
                json{{"ok", false}, {"error", "server shutting down"}});
        }
        catch (...)
        {
            // promise already satisfied; ignore.
        }
    }
}

void AutomationServer::acceptLoop()
{
    while (m_running.load())
    {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientFd =
            ::accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientFd < 0)
        {
            if (!m_running.load())
            {
                break;  // stop() closed the socket.
            }
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        // Serve this client to completion before accepting the next. The
        // automation protocol is meant for a single driving agent; serialized
        // handling keeps command ordering well-defined.
        handleConnection(clientFd);
        ::close(clientFd);
    }
}

namespace
{

// Read one '\n'-terminated line from fd into `line` (without the newline).
// Returns false on EOF/error with nothing more to read.
bool readLine(int fd, std::string& carry, std::string& line)
{
    // Drain any complete line already buffered in carry.
    auto extract = [&carry, &line]() -> bool {
        auto pos = carry.find('\n');
        if (pos == std::string::npos)
        {
            return false;
        }
        line = carry.substr(0, pos);
        carry.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        return true;
    };

    if (extract())
    {
        return true;
    }

    char buf[4096];
    while (true)
    {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0)
        {
            return false;
        }
        carry.append(buf, static_cast<size_t>(n));
        if (extract())
        {
            return true;
        }
    }
}

bool writeAll(int fd, const std::string& data)
{
    size_t off = 0;
    while (off < data.size())
    {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n <= 0)
        {
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

void AutomationServer::handleConnection(int clientFd)
{
    std::string carry;
    std::string line;
    while (m_running.load() && readLine(clientFd, carry, line))
    {
        if (line.empty())
        {
            continue;
        }

        json response;
        json request;
        bool parsed = true;
        try
        {
            request = json::parse(line);
        }
        catch (const std::exception& e)
        {
            parsed = false;
            response = json{{"ok", false},
                            {"error", std::string("invalid JSON: ") + e.what()}};
        }

        if (parsed)
        {
            response = dispatch(request);
        }

        if (!writeAll(clientFd, response.dump() + "\n"))
        {
            break;
        }

        // The "quit" command shuts the editor down; close this connection after
        // replying so the client gets its ack.
        if (parsed && request.value("cmd", std::string{}) == "quit")
        {
            break;
        }
    }
}

nlohmann::json AutomationServer::dispatch(const nlohmann::json& request)
{
    auto cmd = std::make_shared<PendingCommand>();
    cmd->request = request;
    std::future<json> fut = cmd->response.get_future();

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (!m_running.load())
        {
            return json{{"ok", false}, {"error", "server not running"}};
        }
        m_queue.push(cmd);
    }

    // Block until the main thread (drain) fulfills the promise.
    return fut.get();
}

int AutomationServer::drain()
{
    int count = 0;
    while (true)
    {
        std::shared_ptr<PendingCommand> cmd;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_queue.empty())
            {
                break;
            }
            cmd = m_queue.front();
            m_queue.pop();
        }

        json response;
        try
        {
            response = m_handler ? m_handler(cmd->request)
                                 : json{{"ok", false}, {"error", "no handler"}};
        }
        catch (const std::exception& e)
        {
            response = json{{"ok", false}, {"error", e.what()}};
        }

        try
        {
            cmd->response.set_value(std::move(response));
        }
        catch (...)
        {
            // promise already satisfied (shouldn't happen); ignore.
        }
        ++count;
    }
    return count;
}
