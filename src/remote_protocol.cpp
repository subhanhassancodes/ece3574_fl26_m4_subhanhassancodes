#include "aiws/remote_protocol.hpp"

#include <cerrno>
#include <cstdlib>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace aiws::remote {

namespace {

constexpr int kPollStepMs = 100;

int wait_ms(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now()).count();
    if (left <= 0) return 0;
    return left < kPollStepMs ? static_cast<int>(left) : kPollStepMs;
}

bool read_exact(int fd, char* out, std::size_t n, Clock::time_point deadline,
                const std::atomic<bool>* stop) {
    std::size_t got = 0;
    while (got < n) {
        if (stop != nullptr && stop->load()) return false;
        if (Clock::now() >= deadline) return false;

        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, wait_ms(deadline));
        if (ready < 0 && errno != EINTR) return false;
        if (ready <= 0) continue;

        const ssize_t r = ::recv(fd, out + got, n - got, 0);
        if (r == 0) return false;
        if (r < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return false;
        }
        got += static_cast<std::size_t>(r);
    }
    return true;
}

}  // namespace

std::string default_socket_path() {
    const char* user = std::getenv("USER");
    if (user != nullptr && *user != '\0') {
        return std::string("/tmp/aiws-") + user + ".sock";
    }
    return "/tmp/aiws-" + std::to_string(::getuid()) + ".sock";
}

std::string encode(std::uint8_t tag, const std::string& payload) {
    const auto n = static_cast<std::uint32_t>(payload.size());
    std::string out;
    out.reserve(5 + payload.size());
    out.push_back(static_cast<char>(tag));
    out.push_back(static_cast<char>((n >> 24) & 0xFF));
    out.push_back(static_cast<char>((n >> 16) & 0xFF));
    out.push_back(static_cast<char>((n >> 8) & 0xFF));
    out.push_back(static_cast<char>(n & 0xFF));
    out += payload;
    return out;
}

bool send_all(int fd, const std::string& data, Clock::time_point deadline) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        if (Clock::now() >= deadline) return false;

        pollfd p{fd, POLLOUT, 0};
        const int ready = ::poll(&p, 1, wait_ms(deadline));
        if (ready < 0 && errno != EINTR) return false;
        if (ready <= 0) continue;

        const ssize_t w = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return false;
        }
        sent += static_cast<std::size_t>(w);
    }
    return true;
}

bool read_message(int fd, std::uint8_t& tag, std::string& payload,
                  Clock::time_point deadline, const std::atomic<bool>* stop) {
    unsigned char header[5];
    if (!read_exact(fd, reinterpret_cast<char*>(header), 5, deadline, stop)) return false;

    const std::uint32_t n = (std::uint32_t{header[1]} << 24) | (std::uint32_t{header[2]} << 16) |
                            (std::uint32_t{header[3]} << 8) | std::uint32_t{header[4]};
    if (n > kMaxPayload) return false;

    tag = header[0];
    payload.assign(n, '\0');
    return n == 0 || read_exact(fd, payload.data(), n, deadline, stop);
}

}  // namespace aiws::remote