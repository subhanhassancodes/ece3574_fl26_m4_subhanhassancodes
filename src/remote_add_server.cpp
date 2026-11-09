#include "aiws/remote_add_server.hpp"

#include "aiws/remote_protocol.hpp"

#include <QFile>
#include <QMetaObject>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace aiws {

namespace {

constexpr int kPollStepMs = 100;
constexpr auto kClientTimeout = std::chrono::seconds(5);

std::string check_readable(const QString& path) {
    if (path.isEmpty() || !path.startsWith('/')) {
        return "path must be absolute";
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return "cannot read " + path.toStdString();
    }
    return {};
}

}  // namespace

RemoteAddServer::RemoteAddServer(QObject* parent) : QObject(parent) {}

RemoteAddServer::~RemoteAddServer() {
    stop();
}

bool RemoteAddServer::start(const std::string& socket_path) {
    if (thread_.joinable()) {
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path.empty() || socket_path.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size() + 1);

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    ::unlink(socket_path.c_str());
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
        ::listen(fd, 8) < 0) {
        ::close(fd);
        return false;
    }

    socket_path_ = socket_path;
    listen_fd_ = fd;
    stop_ = false;
    thread_ = std::thread(&RemoteAddServer::serve, this);
    return true;
}

void RemoteAddServer::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stop_ = true;
    thread_.join();
    ::close(listen_fd_);
    listen_fd_ = -1;
    ::unlink(socket_path_.c_str());
}

bool RemoteAddServer::is_running() const noexcept {
    return thread_.joinable();
}

void RemoteAddServer::deliver(const QString& path) {
    emit add_requested(path);
}

void RemoteAddServer::serve() {
    while (!stop_) {
        pollfd p{listen_fd_, POLLIN, 0};
        const int ready = ::poll(&p, 1, kPollStepMs);
        if (ready <= 0) {
            continue;
        }

        const int client = ::accept(listen_fd_, nullptr, nullptr);
        if (client < 0) {
            continue;
        }
        handle_client(client);
        ::close(client);
    }
}

void RemoteAddServer::handle_client(int fd) {
    const auto deadline = remote::Clock::now() + kClientTimeout;

    std::uint8_t tag = 0;
    std::string payload;
    if (!remote::read_message(fd, tag, payload, deadline, &stop_)) {
        return;
    }

    std::string reason;
    if (tag != remote::kAdd) {
        reason = "unknown request";
    } else {
        reason = check_readable(QString::fromStdString(payload));
    }

    if (!reason.empty()) {
        remote::send_all(fd, remote::encode(remote::kRejected, reason), deadline);
        return;
    }

    remote::send_all(fd, remote::encode(remote::kOk, {}), deadline);
    QMetaObject::invokeMethod(this, "deliver", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(payload)));
}

}  // namespace aiws
