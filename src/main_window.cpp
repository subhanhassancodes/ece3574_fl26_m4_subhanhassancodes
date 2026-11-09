#include "aiws/gui/main_window.hpp"

#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace aiws::gui {

namespace {

void make_bold(QLabel* label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

QFrame* make_line(QWidget* parent, QFrame::Shape shape) {
    auto* line = new QFrame(parent);
    line->setFrameShape(shape);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

QString count_text(std::size_t n, const QString& word) {
    return QString("%1 %2%3").arg(n).arg(word).arg(n == 1 ? "" : "s");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      builder_(std::make_unique<CorpusBuilder>()),
      remote_server_(std::make_unique<RemoteAddServer>()) {
    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);

    auto* header = new QFrame(central);
    header->setObjectName("header");
    header->setStyleSheet(
        "#header { background-color: #6b2c33; }"
        "#header QLabel { color: white; background: transparent; }");

    auto* badge = new QLabel("AI", header);
    badge->setStyleSheet(
        "QLabel { background-color: #8e4a52; border: 2px solid white;"
        " border-radius: 16px; padding: 6px 7px; font-weight: bold; }");

    auto* title = new QLabel("AI Workspace", header);
    title->setStyleSheet("font-size: 22px; font-weight: bold;");
    auto* subtitle = new QLabel("Document Search and Context Explorer", header);
    subtitle->setStyleSheet("font-style: italic;");

    auto* title_box = new QVBoxLayout;
    title_box->setSpacing(2);
    title_box->addWidget(title);
    title_box->addWidget(subtitle);

    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(16, 10, 16, 10);
    header_layout->addWidget(badge);
    header_layout->addSpacing(8);
    header_layout->addLayout(title_box);
    header_layout->addStretch();

    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    auto* bottom_row = new QHBoxLayout;
    bottom_row->addWidget(term_inspector_widget_, 1);
    bottom_row->addWidget(make_line(central, QFrame::VLine));
    bottom_row->addWidget(context_widget_, 1);

    auto* status_title = new QLabel("Status:", central);
    make_bold(status_title);
    status_label_ = new QLabel(central);
    status_label_->setObjectName("status_label");

    reset_button_ = new QPushButton("Reset", central);
    reset_button_->setStyleSheet(
        "QPushButton { background-color: #2f6690; color: white; border: 1px solid #234e6e;"
        " border-radius: 4px; padding: 5px 18px; }"
        "QPushButton:hover { background-color: #3a7cab; }"
        "QPushButton:pressed { background-color: #234e6e; }");

    exit_button_ = new QPushButton("Exit", central);
    exit_button_->setStyleSheet(
        "QPushButton { background-color: #a83232; color: white; border: 1px solid #7d2424;"
        " border-radius: 4px; padding: 5px 18px; }"
        "QPushButton:hover { background-color: #c03c3c; }"
        "QPushButton:pressed { background-color: #7d2424; }");

    auto* status_row = new QHBoxLayout;
    status_row->addWidget(status_title);
    status_row->addSpacing(4);
    status_row->addWidget(status_label_, 1);
    status_row->addWidget(reset_button_);
    status_row->addWidget(exit_button_);

    auto* body = new QVBoxLayout;
    body->setContentsMargins(10, 6, 10, 10);
    body->addWidget(workspace_widget_, 3);
    body->addWidget(make_line(central, QFrame::HLine));
    body->addWidget(search_widget_, 4);
    body->addWidget(make_line(central, QFrame::HLine));
    body->addLayout(bottom_row, 2);
    body->addWidget(make_line(central, QFrame::HLine));
    body->addLayout(status_row);

    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addLayout(body, 1);

    setCentralWidget(central);

    connect(workspace_widget_, &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);

    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

    connect(builder_.get(), &CorpusBuilder::build_finished,
            this, &MainWindow::build_finished);
    connect(remote_server_.get(), &RemoteAddServer::add_requested,
            this, &MainWindow::add_document_file);

    status_label_->setText("Ready");
    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

bool MainWindow::add_document_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const QFileInfo info(path);
    const QString title = info.fileName();
    std::string text(bytes.constData(), static_cast<std::size_t>(bytes.size()));

    try {
        workspace_.add_document(Document(info.absoluteFilePath().toStdString(),
                                         title.toStdString(), std::move(text)));
    } catch (const std::exception&) {
        return false;
    }

    workspace_widget_->add_document(title);
    workspace_changed();
    invalidate_corpus();
    status_label_->setText("Added " + title + ". Build the corpus to process it.");
    return true;
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = ms_per_document < 0 ? 0 : ms_per_document;
}

bool MainWindow::start_remote_add(const QString& socket_path) {
    return remote_server_->start(socket_path.toStdString());
}

std::size_t MainWindow::active_builds() const noexcept {
    return builder_->job_count();
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Add Document", QString(), "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }

    if (!add_document_file(path)) {
        QMessageBox::warning(this, "Add Document", "Could not open " + path);
        status_label_->setText("Could not open the selected file.");
    }
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty() || building_) {
        return;
    }

    building_ = true;
    builder_->start(workspace_, workspace_version_, build_delay_ms_);

    workspace_widget_->set_corpus_status("Building...");
    status_label_->setText("Building corpus in the background.");
    update_action_states();
}

void MainWindow::build_finished(quint64 job_id) {
    CorpusBuilder::Result result = builder_->take(job_id);

    bool current = result.version == workspace_version_;
#ifdef AIWS_FAULT_ACCEPT_STALE
    current = true;
#endif
    if (!current) {
        return;
    }
    building_ = false;

    if (!result.core) {
        corpus_current_ = false;
        clear_processing_outputs();
        workspace_widget_->set_corpus_status("Build failed");
        status_label_->setText("Corpus build failed: " +
                               QString::fromStdString(result.error));
        update_action_states();
        return;
    }

    processing_core_ = std::move(*result.core);
    corpus_current_ = true;
    clear_processing_outputs();

    const QString chunks = count_text(processing_core_.chunk_count(), "chunk");
    workspace_widget_->set_corpus_status(chunks);
    status_label_->setText("Corpus built: " + chunks + ".");
    update_action_states();
}

void MainWindow::document_selected(int row) {
    const std::vector<Document>& docs = workspace_.documents();
    if (row < 0 || static_cast<std::size_t>(row) >= docs.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    const Document& doc = docs[static_cast<std::size_t>(row)];
    workspace_widget_->show_selected_document(QString::fromStdString(doc.title()),
                                              QString::fromStdString(doc.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    if (!corpus_current_ || query.trimmed().isEmpty()) {
        return;
    }

    std::vector<SearchResult> results;
    try {
        results = processing_core_.search(query.toStdString(), k);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Search", e.what());
        status_label_->setText("Search failed.");
        return;
    }

    const std::size_t n = results.size();
    search_widget_->set_results(std::move(results));
    context_widget_->clear_context();
    status_label_->setText("Search returned " + count_text(n, "result") + ".");
    update_action_states();
}

void MainWindow::query_changed(const QString& query) {
    (void)query;
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    if (!corpus_current_ || !search_widget_->has_query()) {
        return;
    }

    std::vector<ContextItem> items;
    try {
        items = processing_core_.build_context(search_widget_->query().toStdString(),
                                               search_widget_->result_count(),
                                               static_cast<std::size_t>(token_budget));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Build Context", e.what());
        status_label_->setText("Context build failed.");
        return;
    }

    context_widget_->set_context(items);
    status_label_->setText("Context built: " + count_text(items.size(), "item") + ".");
}

void MainWindow::source_document_requested(const QString& document_id) {
    const std::string id = document_id.toStdString();
    const std::vector<Document>& docs = workspace_.documents();

    for (std::size_t i = 0; i < docs.size(); ++i) {
        if (docs[i].id() == id) {
            workspace_widget_->select_document(static_cast<int>(i));
            status_label_->setText("Showing source: " +
                                   QString::fromStdString(docs[i].title()));
            return;
        }
    }
    status_label_->setText("Source document is not in the workspace.");
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    const QString trimmed = term.trimmed();
    if (!corpus_current_ || trimmed.isEmpty()) {
        return;
    }

    const std::string t = trimmed.toStdString();
    const bool has_chunk = !chunk_id.isEmpty();
    std::size_t df = 0;
    std::size_t tf = 0;

    try {
        df = processing_core_.document_frequency(t);
        if (has_chunk) {
            tf = processing_core_.term_frequency(t, chunk_id.toStdString());
        }
    } catch (const std::exception& e) {
        term_inspector_widget_->clear_statistics();
        QMessageBox::warning(this, "Term Inspector", e.what());
        status_label_->setText("Could not inspect \"" + trimmed + "\".");
        return;
    }

    term_inspector_widget_->set_statistics(df, has_chunk, tf);
    status_label_->setText("Inspected \"" + trimmed + "\".");
}

void MainWindow::reset_application() {
    workspace_.clear();
    workspace_changed();
    processing_core_ = ProcessingCore();
    corpus_current_ = false;

    Resettable* components[] = {workspace_widget_, search_widget_,
                                term_inspector_widget_, context_widget_};
    for (Resettable* component : components) {
        component->reset();
    }

    status_label_->setText("Application reset.");
    update_action_states();
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    const bool has_documents = !workspace_.documents().empty();
    const bool has_query = search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(has_documents && !building_);
    search_widget_->set_search_enabled(corpus_current_ && has_query);
    term_inspector_widget_->set_inspection_enabled(corpus_current_);
    context_widget_->set_build_context_enabled(corpus_current_ && has_query);
}

void MainWindow::workspace_changed() {
    ++workspace_version_;
    building_ = false;
}

void MainWindow::invalidate_corpus() {
    const bool was_built = processing_core_.chunk_count() > 0;
    corpus_current_ = false;
    clear_processing_outputs();
    workspace_widget_->set_corpus_status(was_built ? "Out of date (rebuild required)"
                                                   : "Not built");
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui