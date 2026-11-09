#include "aiws/gui/search_widget.hpp"

#include "aiws/gui/search_result_model.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace aiws::gui {

namespace {

void make_bold(QLabel* label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

}  // namespace

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    auto* query_label = new QLabel("Query:", this);
    make_bold(query_label);

    query_edit_ = new QLineEdit(this);
    query_edit_->setPlaceholderText("Search the current corpus");

    auto* count_label = new QLabel("Results:", this);
    result_count_spin_ = new QSpinBox(this);
    result_count_spin_->setRange(1, 100);
    result_count_spin_->setValue(3);

    search_button_ = new QPushButton("Search", this);

    auto* query_row = new QHBoxLayout;
    query_row->addWidget(query_label);
    query_row->addWidget(query_edit_, 1);
    query_row->addSpacing(12);
    query_row->addWidget(count_label);
    query_row->addWidget(result_count_spin_);
    query_row->addWidget(search_button_);

    result_model_ = new SearchResultModel(this);

    results_view_ = new QTableView(this);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->setWordWrap(false);
    results_view_->setTextElideMode(Qt::ElideRight);
    results_view_->verticalHeader()->setVisible(false);
    results_view_->horizontalHeader()->setStretchLastSection(true);
    results_view_->setColumnWidth(0, 60);
    results_view_->setColumnWidth(1, 220);
    results_view_->setColumnWidth(2, 70);
    results_view_->setColumnWidth(3, 90);

    auto* selected_label = new QLabel("Selected Result:", this);
    make_bold(selected_label);
    view_source_button_ = new QPushButton("View Source", this);

    auto* selected_row = new QHBoxLayout;
    selected_row->addWidget(selected_label);
    selected_row->addStretch();
    selected_row->addWidget(view_source_button_);

    selected_result_text_ = new QPlainTextEdit(this);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to inspect its details.");

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(query_row);
    layout->addWidget(results_view_, 3);
    layout->addLayout(selected_row);
    layout->addWidget(selected_result_text_, 2);

    connect(search_button_, &QPushButton::clicked,
            this, &SearchWidget::request_search);
    connect(query_edit_, &QLineEdit::textChanged,
            this, &SearchWidget::query_changed);
    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);
    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);

    reset();
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    results_view_->clearSelection();
    result_model_->set_results(std::move(results));
    show_selected_result();
}

void SearchWidget::clear_results() {
    results_view_->clearSelection();
    result_model_->clear();
    show_selected_result();
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    emit search_requested(query(), result_count());
}

void SearchWidget::show_selected_result() {
    const QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    const SearchResult* result = nullptr;
    int row = -1;
    if (rows.size() == 1) {
        row = rows.first().row();
        result = result_model_->result_at(row);
    }

    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    QString details;
    details += QString("Rank: %1\n").arg(row + 1);
    details += "Document: " + QString::fromStdString(result->document_id) + "\n";
    details += QString("Chunk: %1 (%2)\n")
                   .arg(result->chunk_sequence)
                   .arg(QString::fromStdString(result->chunk_id));
    details += QString("Score: %1\n").arg(result->score);
    details += QString("Matched terms: %1\n\n").arg(result->matched_terms);
    details += QString::fromStdString(result->text);

    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);
    emit selected_chunk_changed(QString::fromStdString(result->chunk_id));
}

void SearchWidget::request_source_document() {
    const QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    if (rows.size() != 1) {
        return;
    }
    const SearchResult* result = result_model_->result_at(rows.first().row());
    if (result == nullptr) {
        return;
    }
    emit source_document_requested(QString::fromStdString(result->document_id));
}

}  // namespace aiws::gui
