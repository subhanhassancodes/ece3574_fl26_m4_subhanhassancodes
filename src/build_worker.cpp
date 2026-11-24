#include "aiws/build_worker.hpp"

#include <QMetaObject>

#include <chrono>
#include <exception>
#include <utility>

namespace aiws {

BuildWorker::BuildWorker(QObject* parent) : QObject(parent) {
    thread_ = std::thread(&BuildWorker::run, this);
}

BuildWorker::~BuildWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        pending_.reset();
    }
#ifndef AIWS_FAULT_SILENT_SHUTDOWN
    wake_.notify_all();
#endif
    thread_.join();
}

std::uint64_t BuildWorker::submit(Workspace workspace, int delay_ms) {
    std::uint64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = next_id_++;
        wanted_ = id;
        pending_ = Request{id, std::move(workspace), delay_ms};
    }
    wake_.notify_all();
    return id;
}

void BuildWorker::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wanted_ = 0;
        pending_.reset();
    }
    wake_.notify_all();
}

std::optional<BuildWorker::Result> BuildWorker::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<Result> result = std::move(done_);
    done_.reset();
    if (result && result->id != wanted_) {
        result.reset();
    }
    return result;
}

std::thread::id BuildWorker::thread_id() const noexcept {
    return thread_.get_id();
}

void BuildWorker::notify_finished() {
    emit build_finished();
}

bool BuildWorker::should_stop(std::uint64_t id) const {
#ifdef AIWS_FAULT_NO_CANCEL
    (void)id;
    return stop_;
#else
    return stop_ || id != wanted_;
#endif
}

void BuildWorker::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
        if (stop_) {
            return;
        }

        Request request = std::move(*pending_);
        pending_.reset();

        Result result = build(request, lock);
        if (stop_ || result.id != wanted_) {
            continue;
        }

        done_ = std::move(result);
        QMetaObject::invokeMethod(this, "notify_finished", Qt::QueuedConnection);
    }
}

BuildWorker::Result BuildWorker::build(Request& request, std::unique_lock<std::mutex>& lock) {
    Result result;
    result.id = request.id;
    result.built_on = std::this_thread::get_id();

    const auto per_document = std::chrono::milliseconds(request.delay_ms);
    const std::size_t docs = request.workspace.documents().size();
    for (std::size_t i = 0; i < docs; ++i) {
        if (wake_.wait_for(lock, per_document, [&] { return should_stop(request.id); })) {
            return result;
        }
    }

    lock.unlock();
    try {
        auto core = std::make_unique<ProcessingCore>();
        core->rebuild(request.workspace);
        result.core = std::move(core);
    } catch (const std::exception& e) {
        result.error = e.what();
    } catch (...) {
        result.error = "unknown error";
    }
    lock.lock();
    return result;
}

}  // namespace aiws

