#pragma once

#include <QObject>

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace aiws {

/**
 * @brief One background thread that runs every corpus build until it is destroyed.
 *
 * submit(), cancel(), take() and the destructor are called on the GUI thread.
 * Everything the two threads share is in the members under mutex_.
 */
class BuildWorker final : public QObject {
    Q_OBJECT

public:
    struct Result {
        std::uint64_t id{0};
        std::unique_ptr<ProcessingCore> core;
        std::string error;
        std::thread::id built_on;
    };

    explicit BuildWorker(QObject* parent = nullptr);
    ~BuildWorker() override;

    BuildWorker(const BuildWorker&) = delete;
    BuildWorker& operator=(const BuildWorker&) = delete;

    /**
     * @brief Hands a build to the worker without waiting.
     *
     * Replaces any request the worker has not taken yet, and makes a running
     * build unwanted.
     * @return Id of the request, returned again in its Result.
     */
    std::uint64_t submit(Workspace workspace, int delay_ms);

    /** @brief Drops the waiting request and stops the running build. */
    void cancel();

    /**
     * @brief Returns the finished result if it is still the wanted build.
     *
     * Called after build_finished(). A result for a cancelled or replaced
     * request is thrown away here.
     */
    std::optional<Result> take();

    std::thread::id thread_id() const noexcept;

signals:
    /** @brief Emitted on the GUI thread after the worker stored a result. */
    void build_finished();

private slots:
    void notify_finished();

private:
    struct Request {
        std::uint64_t id{0};
        Workspace workspace;
        int delay_ms{0};
    };

    void run();
    Result build(Request& request, std::unique_lock<std::mutex>& lock);
    bool should_stop(std::uint64_t id) const;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> pending_;
    std::optional<Result> done_;
    std::uint64_t wanted_{0};
    bool stop_{false};

    std::uint64_t next_id_{1};
    std::thread thread_;
};

}  // namespace aiws

