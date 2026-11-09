#include "aiws/remote_protocol.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr int kRejected = 1;
constexpr int kUnreachable = 2;

int usage() {
    std::cerr << "usage: aiws_send [--socket PATH] add FILE\n";
    return kUnreachable;
}

std::string absolute_path(const std::string& file) {
    if (!file.empty() && file[0] == '/') {
        return file;
    }
    char buf[4096];
    if (::getcwd(buf, sizeof(buf)) == nullptr) {
        return file;
    }
    return std::string(buf) + "/" + file;
}

int connect_to(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        return -1;
    }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        return fd;
    }
    if (errno == EINPROGRESS || errno == EAGAIN) {
        pollfd p{fd, POLLOUT, 0};
        int err = 0;
        socklen_t len = sizeof(err);
        if (::poll(&p, 1, 2000) == 1 &&
            ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
            return fd;
        }
    }
    ::close(fd);
    return -1;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string socket_path = aiws::remote::default_socket_path();
    int i = 1;
    if (i < argc && std::string(argv[i]) == "--socket") {
        if (i + 1 >= argc) return usage();
        socket_path = argv[i + 1];
        i += 2;
    }
    if (argc - i != 2 || std::string(argv[i]) != "add") {
        return usage();
    }
    const std::string file = absolute_path(argv[i + 1]);

    const int fd = connect_to(socket_path);
    if (fd < 0) {
        std::cerr << "aiws_send: no application at " << socket_path << "\n";
        return kUnreachable;
    }

    const auto deadline = aiws::remote::Clock::now() + std::chrono::seconds(5);
    std::uint8_t tag = 0;
    std::string reason;
    const bool ok =
        aiws::remote::send_all(fd, aiws::remote::encode(aiws::remote::kAdd, file), deadline) &&
        aiws::remote::read_message(fd, tag, reason, deadline);
    ::close(fd);

    if (!ok) {
        std::cerr << "aiws_send: communication failed\n";
        return kUnreachable;
    }
    if (tag == aiws::remote::kOk) {
        return 0;
    }
    if (tag == aiws::remote::kRejected) {
        std::cerr << "aiws_send: " << reason << "\n";
        return kRejected;
    }
    std::cerr << "aiws_send: unexpected reply\n";
    return kUnreachable;
}
