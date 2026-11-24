#pragma once

#include "aiws/processing_core.hpp"

#include <QString>

#include <memory>
#include <string>

namespace aiws::gui {

/**
 * @brief What a corpus state may ask of the window. Implemented by MainWindow.
 */
class CorpusContext {
public:
    virtual ~CorpusContext() = default;

    virtual void start_build() = 0;
    virtual void cancel_build() = 0;
    virtual void install_corpus(std::unique_ptr<ProcessingCore> core) = 0;
    virtual void clear_outputs() = 0;
    virtual QString chunk_summary() const = 0;
    virtual void show_corpus_status(const QString& text) = 0;
    virtual void show_status(const QString& text) = 0;
};

/**
 * @brief One state of the corpus lifecycle (State pattern).
 *
 * Each event handler returns the next state, or nullptr to stay. States are
 * created, called and destroyed only on the GUI thread.
 */
class CorpusState {
public:
    virtual ~CorpusState() = default;

    virtual void enter(CorpusContext& ctx) = 0;

    virtual bool corpus_current() const { return false; }
    virtual bool can_build() const { return false; }

    virtual std::unique_ptr<CorpusState> workspace_changed(CorpusContext& ctx) = 0;
    virtual std::unique_ptr<CorpusState> build_requested(CorpusContext& ctx);
    virtual std::unique_ptr<CorpusState> reset(CorpusContext& ctx);
    virtual std::unique_ptr<CorpusState> build_succeeded(CorpusContext& ctx,
                                                         std::unique_ptr<ProcessingCore> core);
    virtual std::unique_ptr<CorpusState> build_failed(CorpusContext& ctx, const std::string& error);
};

/** @brief No documents in the Workspace. */
class EmptyState final : public CorpusState {
public:
    void enter(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> workspace_changed(CorpusContext& ctx) override;
};

/** @brief Documents present, no current corpus. label is the corpus status shown. */
class UnbuiltState final : public CorpusState {
public:
    explicit UnbuiltState(QString label);

    void enter(CorpusContext& ctx) override;
    bool can_build() const override { return true; }
    std::unique_ptr<CorpusState> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> build_requested(CorpusContext& ctx) override;

private:
    QString label_;
};

/** @brief A build for the present Workspace was handed to the worker. */
class BuildingState final : public CorpusState {
public:
    explicit BuildingState(QString fallback_label);

    void enter(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> reset(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> build_succeeded(CorpusContext& ctx,
                                                 std::unique_ptr<ProcessingCore> core) override;
    std::unique_ptr<CorpusState> build_failed(CorpusContext& ctx, const std::string& error) override;

private:
    QString fallback_label_;
};

/** @brief The corpus is current for the present Workspace. */
class CurrentState final : public CorpusState {
public:
    void enter(CorpusContext& ctx) override;
    bool corpus_current() const override { return true; }
    bool can_build() const override { return true; }
    std::unique_ptr<CorpusState> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> build_requested(CorpusContext& ctx) override;
};

/** @brief The last build for the present Workspace failed. */
class FailedState final : public CorpusState {
public:
    FailedState(QString fallback_label, std::string error);

    void enter(CorpusContext& ctx) override;
    bool can_build() const override { return true; }
    std::unique_ptr<CorpusState> workspace_changed(CorpusContext& ctx) override;
    std::unique_ptr<CorpusState> build_requested(CorpusContext& ctx) override;

private:
    QString fallback_label_;
    std::string error_;
};

}  // namespace aiws::gui

