#include "aiws/gui/search_result_model.hpp"

#include <QString>

#include <utility>

namespace aiws::gui {

namespace {

enum Column { kRank = 0, kDocument, kChunk, kScore, kText, kColumnCount };

}  // namespace

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return kColumnCount;
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }

    const SearchResult* result = result_at(index.row());
    if (result == nullptr) {
        return {};
    }

    switch (index.column()) {
        case kRank:
            return index.row() + 1;
        case kDocument:
            return QString::fromStdString(result->document_id);
        case kChunk:
            return static_cast<qulonglong>(result->chunk_sequence);
        case kScore:
            return result->score;
        case kText:
            return QString::fromStdString(result->text).simplified();
        default:
            return {};
    }
}

QVariant SearchResultModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }

    switch (section) {
        case kRank:
            return QString("Rank");
        case kDocument:
            return QString("Document");
        case kChunk:
            return QString("Chunk");
        case kScore:
            return QString("Score");
        case kText:
            return QString("Text");
        default:
            return {};
    }
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if (row < 0 || static_cast<std::size_t>(row) >= results_.size()) {
        return nullptr;
    }
    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
