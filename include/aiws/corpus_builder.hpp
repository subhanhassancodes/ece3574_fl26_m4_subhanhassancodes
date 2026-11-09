#pragma once

#include <QObject>

#include "aiws/processing_core.hpp"
#include "aiws/workspace.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <thread>

namespace aiws {

/**
 * @brief Runs corpus builds on background std::threads.
 *
 * Public functions are called on the GUI thread. Each build works on its own
 * copy of the Workspace and its own ProcessingCore, so a worker shares nothing
 * with the GUI except its job's cancel flag and the future's shared state.
 * Whether a finished build is still valid is decided by the caller.
 */
class CorpusBuilder final : public QObject {
    Q_OBJECT

public:
    struct Result {
        std::uint64_t version{0};
        std::unique_ptr<ProcessingCore> core;
        std::string error;
    };

    explicit CorpusBuilder(QObject* parent = nullptr);
    ~CorpusBuilder() override;

    CorpusBuilder(const CorpusBuilder&) = delete;
    CorpusBuilder& operator=(const CorpusBuilder&) = delete;

    /**
     * @brief Starts a build of workspace in the background.
     * @param version Workspace version the build is for, returned in its Result.
     * @param delay_ms Extra time per document, used for --build-delay-ms.
     * @return Job id later passed to build_finished() and take().
     */
    std::uint64_t start(Workspace workspace, std::uint64_t version, int delay_ms);

    /**
     * @brief Joins a job that reported build_finished() and returns its result.
     *
     * Only called after build_finished(job_id), which is the last thing the
     * worker does, so the join returns right away.
     */
    Result take(std::uint64_t job_id);

    /** @brief Number of jobs that have not been taken yet. */
    std::size_t job_count() const noexcept;

signals:
    /** @brief Emitted on the GUI thread when a job has a result or an error ready. */
    void build_finished(quint64 job_id);

private slots:
    void job_done(quint64 job_id);

private:
    struct BuildJob {
        std::uint64_t version{0};
        std::atomic<bool> cancel{false};
        std::future<std::unique_ptr<ProcessingCore>> result;
        std::thread thread;
    };

    void run(std::uint64_t job_id, BuildJob& job, Workspace workspace, int delay_ms,
             std::promise<std::unique_ptr<ProcessingCore>> promise);

    std::map<std::uint64_t, std::unique_ptr<BuildJob>> jobs_;
    std::uint64_t next_id_{1};
};

}  // namespace aiws