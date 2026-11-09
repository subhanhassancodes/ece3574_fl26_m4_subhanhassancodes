#include "aiws/corpus_builder.hpp"

#include <QMetaObject>

#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace aiws {

namespace {

constexpr int kDelayStepMs = 10;

void simulate_delay(int total_ms, const std::atomic<bool>& cancel) {
    for (int waited = 0; waited < total_ms && !cancel.load(); waited += kDelayStepMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kDelayStepMs));
    }
}

}  // namespace

CorpusBuilder::CorpusBuilder(QObject* parent) : QObject(parent) {}

CorpusBuilder::~CorpusBuilder() {
    for (auto& [id, job] : jobs_) {
        job->cancel = true;
    }
    for (auto& [id, job] : jobs_) {
        if (job->thread.joinable()) {
            job->thread.join();
        }
    }
}

std::uint64_t CorpusBuilder::start(Workspace workspace, std::uint64_t version, int delay_ms) {
    const std::uint64_t id = next_id_++;

    auto job = std::make_unique<BuildJob>();
    job->version = version;
    std::promise<std::unique_ptr<ProcessingCore>> promise;
    job->result = promise.get_future();

    BuildJob& ref = *job;
    jobs_.emplace(id, std::move(job));
    ref.thread = std::thread(&CorpusBuilder::run, this, id, std::ref(ref),
                             std::move(workspace), delay_ms, std::move(promise));
    return id;
}

CorpusBuilder::Result CorpusBuilder::take(std::uint64_t job_id) {
    Result result;
    auto it = jobs_.find(job_id);
    if (it == jobs_.end()) {
        result.error = "unknown build";
        return result;
    }

    BuildJob& job = *it->second;
    job.thread.join();
    result.version = job.version;
    try {
        result.core = job.result.get();
    } catch (const std::exception& e) {
        result.error = e.what();
    } catch (...) {
        result.error = "unknown error";
    }
    jobs_.erase(it);
    return result;
}

std::size_t CorpusBuilder::job_count() const noexcept {
    return jobs_.size();
}

void CorpusBuilder::job_done(quint64 job_id) {
    emit build_finished(job_id);
}

void CorpusBuilder::run(std::uint64_t job_id, BuildJob& job, Workspace workspace,
                        int delay_ms, std::promise<std::unique_ptr<ProcessingCore>> promise) {
    try {
        const auto docs = static_cast<int>(workspace.documents().size());
        simulate_delay(delay_ms * docs, job.cancel);
        if (job.cancel) {
            throw std::runtime_error("build cancelled");
        }
        auto core = std::make_unique<ProcessingCore>();
        core->rebuild(workspace);
        promise.set_value(std::move(core));
    } catch (...) {
        promise.set_exception(std::current_exception());
#ifdef AIWS_FAULT_LOSE_FAILURE
        return;
#endif
    }

    QMetaObject::invokeMethod(this, "job_done", Qt::QueuedConnection,
                              Q_ARG(quint64, static_cast<quint64>(job_id)));
}

}  // namespace aiws