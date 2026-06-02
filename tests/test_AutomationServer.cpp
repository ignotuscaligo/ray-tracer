// Tests for the editor automation command server (editor/AutomationServer.cpp).
//
// GL-free: AutomationServer is pure sockets + JSON + a main-thread command
// queue. It knows nothing about GL — the editor injects a handler. So we can
// drive the real server over a real 127.0.0.1 socket and verify the protocol
// and the cross-thread marshalling end to end, with no display.
//
// The key invariant under test: a request read on the socket/accept thread is
// enqueued and only executed when drain() is called (which, in the editor, runs
// on the main/GL thread). Here the test thread plays the role of the main thread
// and pumps drain() in a loop while a worker thread does blocking socket I/O.

#include <catch2/catch_all.hpp>

#include "AutomationServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace
{

// Connect to 127.0.0.1:port, send `request` + newline, read one line back.
std::string roundtrip(uint16_t port, const std::string& request)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    const std::string line = request + "\n";
    REQUIRE(::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size()));

    std::string out;
    char buf[1024];
    while (out.find('\n') == std::string::npos)
    {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    auto pos = out.find('\n');
    return pos == std::string::npos ? out : out.substr(0, pos);
}

// Pick an ephemeral port the OS hands us, then close it so the server can rebind
// (SO_REUSEADDR). Small race window, acceptable for a test.
uint16_t pickFreePort()
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;  // let the OS choose
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t len = sizeof(addr);
    REQUIRE(::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

// Run a client request on a worker thread while pumping drain() on this thread
// until the response arrives (or a timeout). Returns the parsed response.
json driveWithDrain(AutomationServer& server, uint16_t port, const std::string& request)
{
    std::atomic<bool> done{false};
    std::string responseLine;
    std::thread worker([&]() {
        responseLine = roundtrip(port, request);
        done.store(true);
    });

    const auto start = std::chrono::steady_clock::now();
    while (!done.load())
    {
        server.drain();
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Drain once more in case the command landed between the last drain and the
    // done flag flip.
    server.drain();
    worker.join();

    REQUIRE_FALSE(responseLine.empty());
    return json::parse(responseLine);
}

}  // namespace

TEST_CASE("AutomationServer dispatches a request to the handler on the draining thread", "[AutomationServer]")
{
    const std::thread::id mainThread = std::this_thread::get_id();
    std::atomic<bool> handlerRanOnMainThread{false};

    AutomationServer server;
    const uint16_t port = pickFreePort();
    REQUIRE(server.start(port, [&](const json& req) -> json {
        // The handler must execute on the thread that calls drain(), not the
        // socket/accept thread.
        handlerRanOnMainThread.store(std::this_thread::get_id() == mainThread);
        const std::string cmd = req.value("cmd", std::string{});
        if (cmd == "echo")
            return json{{"ok", true}, {"value", req.value("value", 0)}};
        return json{{"ok", false}, {"error", "unknown"}};
    }));
    REQUIRE(server.running());

    const json resp = driveWithDrain(server, port, R"({"cmd":"echo","value":42})");
    REQUIRE(resp["ok"] == true);
    REQUIRE(resp["value"] == 42);
    REQUIRE(handlerRanOnMainThread.load());

    server.stop();
    REQUIRE_FALSE(server.running());
}

TEST_CASE("AutomationServer returns a JSON error for malformed input", "[AutomationServer]")
{
    AutomationServer server;
    const uint16_t port = pickFreePort();
    REQUIRE(server.start(port, [](const json&) -> json { return json{{"ok", true}}; }));

    // Malformed JSON is rejected on the socket thread before it ever reaches the
    // queue, so no drain() pumping is needed — the worker gets its reply directly.
    const std::string line = roundtrip(port, "{ this is not json");
    const json resp = json::parse(line);
    REQUIRE(resp["ok"] == false);
    REQUIRE(resp.contains("error"));

    server.stop();
}

TEST_CASE("AutomationServer binds 127.0.0.1 only", "[AutomationServer]")
{
    AutomationServer server;
    const uint16_t port = pickFreePort();
    REQUIRE(server.start(port, [](const json&) -> json { return json{{"ok", true}}; }));

    // A connection to the loopback address must succeed.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(fd >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    ::close(fd);

    server.stop();
}

TEST_CASE("AutomationServer reports a quit request to the editor via the handler", "[AutomationServer]")
{
    AutomationServer server;
    const uint16_t port = pickFreePort();

    // Mirror the editor's wiring: the quit handler calls requestQuit().
    REQUIRE(server.start(port, [&](const json& req) -> json {
        if (req.value("cmd", std::string{}) == "quit")
        {
            server.requestQuit();
            return json{{"ok", true}, {"quitting", true}};
        }
        return json{{"ok", false}};
    }));

    REQUIRE_FALSE(server.shouldQuit());
    const json resp = driveWithDrain(server, port, R"({"cmd":"quit"})");
    REQUIRE(resp["ok"] == true);
    REQUIRE(resp["quitting"] == true);
    REQUIRE(server.shouldQuit());

    server.stop();
}

TEST_CASE("AutomationServer start fails cleanly on a port already in use", "[AutomationServer]")
{
    AutomationServer first;
    const uint16_t port = pickFreePort();
    REQUIRE(first.start(port, [](const json&) -> json { return json{{"ok", true}}; }));

    // A second server on the same port must fail to bind, not crash.
    AutomationServer second;
    REQUIRE_FALSE(second.start(port, [](const json&) -> json { return json{{"ok", true}}; }));
    REQUIRE_FALSE(second.running());

    first.stop();
}
