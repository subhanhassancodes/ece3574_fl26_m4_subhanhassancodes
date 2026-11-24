#include "aiws/gui/corpus_state.hpp"

#include <utility>

namespace aiws::gui {

namespace {

const char* kNotBuilt = "Not built";
const char* kOutOfDate = "Out of date (rebuild required)";

}  // namespace

std::unique_ptr<CorpusState> CorpusState::build_requested(CorpusContext&) {
    return nullptr;
}

std::unique_ptr<CorpusState> CorpusState::reset(CorpusContext&) {
    return std::make_unique<EmptyState>();
}

std::unique_ptr<CorpusState> CorpusState::build_succeeded(CorpusContext&,
                                                          std::unique_ptr<ProcessingCore>) {
    return nullptr;
}

std::unique_ptr<CorpusState> CorpusState::build_failed(CorpusContext&, const std::string&) {
    return nullptr;
}

void EmptyState::enter(CorpusContext& ctx) {
    ctx.clear_outputs();
    ctx.show_corpus_status(kNotBuilt);
}

std::unique_ptr<CorpusState> EmptyState::workspace_changed(CorpusContext&) {
    return std::make_unique<UnbuiltState>(kNotBuilt);
}

UnbuiltState::UnbuiltState(QString label) : label_(std::move(label)) {}

void UnbuiltState::enter(CorpusContext& ctx) {
    ctx.clear_outputs();
    ctx.show_corpus_status(label_);
}

std::unique_ptr<CorpusState> UnbuiltState::workspace_changed(CorpusContext&) {
    return std::make_unique<UnbuiltState>(label_);
}

std::unique_ptr<CorpusState> UnbuiltState::build_requested(CorpusContext& ctx) {
    ctx.start_build();
    return std::make_unique<BuildingState>(label_);
}

BuildingState::BuildingState(QString fallback_label)
    : fallback_label_(std::move(fallback_label)) {}

void BuildingState::enter(CorpusContext& ctx) {
    ctx.clear_outputs();
    ctx.show_corpus_status("Building...");
    ctx.show_status("Building corpus in the background.");
}

std::unique_ptr<CorpusState> BuildingState::workspace_changed(CorpusContext& ctx) {
    ctx.cancel_build();
    return std::make_unique<UnbuiltState>(fallback_label_);
}

std::unique_ptr<CorpusState> BuildingState::reset(CorpusContext& ctx) {
    ctx.cancel_build();
    return std::make_unique<EmptyState>();
}

std::unique_ptr<CorpusState> BuildingState::build_succeeded(CorpusContext& ctx,
                                                            std::unique_ptr<ProcessingCore> core) {
    ctx.install_corpus(std::move(core));
    return std::make_unique<CurrentState>();
}

std::unique_ptr<CorpusState> BuildingState::build_failed(CorpusContext&, const std::string& error) {
    return std::make_unique<FailedState>(fallback_label_, error);
}

void CurrentState::enter(CorpusContext& ctx) {
    ctx.clear_outputs();
    const QString chunks = ctx.chunk_summary();
    ctx.show_corpus_status(chunks);
    ctx.show_status("Corpus built: " + chunks + ".");
}

std::unique_ptr<CorpusState> CurrentState::workspace_changed(CorpusContext&) {
    return std::make_unique<UnbuiltState>(kOutOfDate);
}

std::unique_ptr<CorpusState> CurrentState::build_requested(CorpusContext& ctx) {
    ctx.start_build();
    return std::make_unique<BuildingState>(kOutOfDate);
}

FailedState::FailedState(QString fallback_label, std::string error)
    : fallback_label_(std::move(fallback_label)), error_(std::move(error)) {}

void FailedState::enter(CorpusContext& ctx) {
    ctx.clear_outputs();
    ctx.show_corpus_status("Build failed");
    ctx.show_status("Corpus build failed: " + QString::fromStdString(error_));
}

std::unique_ptr<CorpusState> FailedState::workspace_changed(CorpusContext&) {
    return std::make_unique<UnbuiltState>(fallback_label_);
}

std::unique_ptr<CorpusState> FailedState::build_requested(CorpusContext& ctx) {
    ctx.start_build();
    return std::make_unique<BuildingState>(fallback_label_);
}

}  // namespace aiws::gui

