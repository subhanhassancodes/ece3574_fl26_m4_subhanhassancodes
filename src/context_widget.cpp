#include "aiws/gui/context_widget.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {

void make_bold(QLabel* label) {
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}

constexpr int kMinBudget = 50;
constexpr int kMaxBudget = 1000;
constexpr int kBudgetStep = 50;
constexpr int kStartBudget = 300;

}  // namespace

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    auto* title = new QLabel("Context:", this);
    make_bold(title);

    auto* budget_label = new QLabel("Token budget:", this);

    context_budget_slider_ = new QSlider(Qt::Horizontal, this);
    context_budget_slider_->setRange(kMinBudget, kMaxBudget);
    context_budget_slider_->setSingleStep(kBudgetStep);
    context_budget_slider_->setPageStep(kBudgetStep);
    context_budget_slider_->setTickInterval(kBudgetStep);
    context_budget_slider_->setMinimumWidth(120);

    budget_value_label_ = new QLabel(this);
    budget_value_label_->setMinimumWidth(36);
    budget_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    build_context_button_ = new QPushButton("Build Context", this);

    auto* budget_row = new QHBoxLayout;
    budget_row->addWidget(budget_label);
    budget_row->addWidget(context_budget_slider_, 1);
    budget_row->addWidget(budget_value_label_);
    budget_row->addWidget(build_context_button_);

    context_text_ = new QPlainTextEdit(this);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addLayout(budget_row);
    layout->addWidget(context_text_, 1);

    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);

    reset();
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    if (items.empty()) {
        context_text_->setPlainText("No context items were returned.");
        return;
    }

    QString out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const ContextItem& item = items[i];
        if (i > 0) {
            out += "\n----------------------------------------\n\n";
        }
        out += "Document: " + QString::fromStdString(item.document_id) + "\n";
        out += QString("Chunk: %1\n").arg(item.chunk_sequence);
        out += QString("Tokens: %1\n").arg(item.token_count);
        out += QString("Score: %1\n").arg(item.score);
        out += QString("Truncated: %1\n").arg(item.truncated ? "Yes" : "No");
        out += "Text: " + QString::fromStdString(item.text) + "\n";
    }
    context_text_->setPlainText(out);
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(kStartBudget);
    budget_value_label_->setText(QString::number(kStartBudget));
    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    const int snapped = ((value + kBudgetStep / 2) / kBudgetStep) * kBudgetStep;
    if (snapped != value) {
        context_budget_slider_->setValue(snapped);
        return;
    }
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
