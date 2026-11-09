#include "aiws/gui/workspace_widget.hpp"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {

void make_bold(QLabel* label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

}  // namespace

WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    auto* workspace_label = new QLabel("Workspace", this);
    make_bold(workspace_label);

    add_document_button_ = new QPushButton("Add Document", this);
    build_corpus_button_ = new QPushButton("Build Corpus", this);

    auto* button_row = new QHBoxLayout;
    button_row->addWidget(workspace_label);
    button_row->addWidget(add_document_button_);
    button_row->addWidget(build_corpus_button_);
    button_row->addStretch();

    document_list_ = new QListWidget(this);
    document_list_->setSelectionMode(QAbstractItemView::SingleSelection);

    auto* corpus_label = new QLabel("Corpus:", this);
    corpus_status_label_ = new QLabel(this);
    corpus_status_label_->setObjectName("corpus_status");
    make_bold(corpus_status_label_);

    auto* corpus_row = new QHBoxLayout;
    corpus_row->addWidget(corpus_label);
    corpus_row->addWidget(corpus_status_label_);
    corpus_row->addStretch();

    auto* left = new QVBoxLayout;
    left->addLayout(button_row);
    left->addWidget(document_list_, 1);
    left->addLayout(corpus_row);

    document_title_label_ = new QLabel(this);
    make_bold(document_title_label_);

    document_text_ = new QPlainTextEdit(this);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText("Select a document to view its contents.");

    auto* right = new QVBoxLayout;
    right->addWidget(document_title_label_);
    right->addWidget(document_text_, 1);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Sunken);

    auto* layout = new QHBoxLayout(this);
    layout->addLayout(left, 1);
    layout->addWidget(divider);
    layout->addLayout(right, 1);

    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);
    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);
    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);

    reset();
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    if (row < 0 || row >= document_list_->count()) {
        return;
    }
    document_list_->setCurrentRow(row);
    document_list_->scrollToItem(document_list_->item(row));
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText("Selected Document - " + title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->setText("Selected Document -");
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status("Not built");
    add_document_button_->setEnabled(true);
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
