#pragma once

#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <iostream>

/**
 * ColumnStats: Statistics for a single table column.
 */
struct ColumnStats {
    std::string column_name;
    size_t distinct_count{1};     // V(A): Number of distinct values
    double min_value{0.0};        // Minimum value observed
    double max_value{1000.0};     // Maximum value observed
    bool is_primary_key{false};   // True if column is unique / PK

    ColumnStats() = default;

    ColumnStats(std::string name, size_t distinct_count, double min_val, double max_val, bool is_pk = false)
        : column_name(std::move(name)), distinct_count(distinct_count),
          min_value(min_val), max_value(max_val), is_primary_key(is_pk) {}
};

/**
 * TableStats: Authoritative catalog statistics for a table used by the Cost-Based Optimizer.
 * Tracks row count N, page count P, and per-column distributions.
 */
class TableStats {
public:
    TableStats() = default;

    TableStats(std::string table_name, size_t num_tuples, size_t num_pages)
        : table_name_(std::move(table_name)), num_tuples_(num_tuples), num_pages_(num_pages) {}

    void AddColumnStats(const ColumnStats &col) {
        columns_[col.column_name] = col;
    }

    inline const std::string &GetTableName() const { return table_name_; }
    inline size_t GetNumTuples() const { return num_tuples_; }
    inline size_t GetNumPages() const { return num_pages_; }

    inline void SetNumTuples(size_t n) { num_tuples_ = n; }
    inline void SetNumPages(size_t p) { num_pages_ = p; }

    inline bool HasColumn(const std::string &col_name) const {
        return columns_.find(col_name) != columns_.end();
    }

    inline const ColumnStats &GetColumnStats(const std::string &col_name) const {
        return columns_.at(col_name);
    }

    /**
     * Estimate selectivity for equality predicate: col = val.
     * Formula: sel = 1 / V(A).
     */
    double EstimateEqualitySelectivity(const std::string &col_name) const {
        auto it = columns_.find(col_name);
        if (it == columns_.end() || it->second.distinct_count == 0) {
            return 0.1; // Default fallback (10%)
        }
        if (it->second.is_primary_key && num_tuples_ > 0) {
            return 1.0 / static_cast<double>(num_tuples_);
        }
        return 1.0 / static_cast<double>(it->second.distinct_count);
    }

    /**
     * Estimate selectivity for range predicate: col BETWEEN low AND high.
     * Formula: sel = (clamp(high) - clamp(low)) / (max_val - min_val).
     */
    double EstimateRangeSelectivity(const std::string &col_name, double low, double high) const {
        auto it = columns_.find(col_name);
        if (it == columns_.end()) {
            return 0.2; // Default fallback (20%)
        }
        const auto &col = it->second;
        if (col.max_value <= col.min_value) {
            return 1.0;
        }
        double clamped_low = std::max(col.min_value, low);
        double clamped_high = std::min(col.max_value, high);
        if (clamped_high < clamped_low) {
            return 0.0;
        }
        double sel = (clamped_high - clamped_low) / (col.max_value - col.min_value);
        return std::clamp(sel, 0.0, 1.0);
    }

private:
    std::string table_name_;
    size_t num_tuples_{0}; // N
    size_t num_pages_{0};  // P
    std::unordered_map<std::string, ColumnStats> columns_;
};
