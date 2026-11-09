#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <string>
#include <thread>

namespace aiws {

/**
 * @brief Serves aiws_send requests on a POSIX Unix domain stream socket.
 *
 * Requests are received and answered on the server's own std::thread. A
 * readable file is acknowledged right away and then handed to the GUI thread
 * through add_requested(), so replies never wait for the GUI.
 */
class RemoteAddServer final : public QObject {
    Q_OBJECT

public:
    explicit RemoteAddServer(QObject* parent = nullptr);
    ~RemoteAddServer() override;

    RemoteAddServer(const RemoteAddServer&) = delete;
    RemoteAddServer& operator=(const RemoteAddServer&) = delete;

    /**
     * @brief Binds socket_path, replacing a stale socket file, and starts serving.
     * @return false if the server is already running or the socket cannot be bound.
     */
    bool start(const std::string& socket_path);

    /** @brief Stops serving, joins the server thread, and removes the socket file. */
    void stop();

    bool is_running() const noexcept;

signals:
    /** @brief Emitted on the GUI thread for each accepted request. */
    void add_requested(const QString& path);

private slots:
    void deliver(const QString& path);

private:
    void serve();
    void handle_client(int fd);

    std::string socket_path_;
    int listen_fd_{-1};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace aiws