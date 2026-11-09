#include "aiws/gui/term_inspector_widget.hpp"

#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {

void make_bold(QLabel* label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

QString blank_value() {
    return QString(QChar(0x2014));
}

}  // namespace

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    auto* title = new QLabel("Term Inspector:", this);
    make_bold(title);

    auto* term_label = new QLabel("Term:", this);
    term_edit_ = new QLineEdit(this);
    term_edit_->setPlaceholderText("Enter a term");
    inspect_button_ = new QPushButton("Inspect", this);

    auto* term_row = new QHBoxLayout;
    term_row->addWidget(term_label);
    term_row->addWidget(term_edit_, 1);
    term_row->addWidget(inspect_button_);

    auto* df_label = new QLabel("Document Frequency:", this);
    document_frequency_value_ = new QLabel(this);
    make_bold(document_frequency_value_);

    auto* tf_label = new QLabel("Selected Chunk TF:", this);
    chunk_frequency_value_ = new QLabel(this);
    make_bold(chunk_frequency_value_);

    auto* stats = new QGridLayout;
    stats->addWidget(df_label, 0, 0);
    stats->addWidget(document_frequency_value_, 0, 1);
    stats->addWidget(tf_label, 0, 2);
    stats->addWidget(chunk_frequency_value_, 0, 3);
    stats->setColumnStretch(1, 1);
    stats->setColumnStretch(3, 1);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addLayout(term_row);
    layout->addLayout(stats);
    layout->addStretch();

    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);

    reset();
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    if (chunk_id != selected_chunk_id_) {
        chunk_frequency_value_->setText(blank_value());
    }
    selected_chunk_id_ = chunk_id;
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(document_frequency));
    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(QString::number(chunk_frequency));
    } else {
        chunk_frequency_value_->setText(blank_value());
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText(blank_value());
    chunk_frequency_value_->setText(blank_value());
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();
    if (term.isEmpty()) {
        return;
    }
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
