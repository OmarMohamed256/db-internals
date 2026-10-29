#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

/**
 * UserRow: A fixed-width table schema representing a user record.
 * 
 * Using a fixed-width record
 * eliminates low-yield variable-length byte defragmentation while exercising 100%
 * of the core database invariants:
 *   - Slotted-page tuple storage and boundaries
 *   - Record identifier (RID) addressing
 *   - Buffer pool frame overlays
 *   - Secondary/Primary B+ Tree index mapping
 *   - Transaction visibility headers (xmin/xmax ready for MVCC)
 */
struct UserRow {
    int64_t id{0};              // Primary key (indexed via B+ Tree)
    char name[32]{0};           // In-line fixed string
    double balance{0.0};        // User balance
    uint64_t created_at{0};     // Epoch microsecond timestamp
    bool is_deleted{false};     // Logical deletion tombstone
    int64_t xmin{0};            // Transaction ID of creator (for MVCC in Sprint 1.5)
    int64_t xmax{0};            // Transaction ID of deleter (0 = active, for MVCC)

    UserRow() = default;

    UserRow(int64_t id, const char *name_str, double balance, uint64_t created_at = 0, int64_t xmin = 0)
        : id(id), balance(balance), created_at(created_at), is_deleted(false), xmin(xmin), xmax(0) {
        if (name_str != nullptr) {
            std::strncpy(name, name_str, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        } else {
            name[0] = '\0';
        }
    }

    inline bool operator==(const UserRow &other) const {
        return id == other.id &&
               balance == other.balance &&
               created_at == other.created_at &&
               is_deleted == other.is_deleted &&
               xmin == other.xmin &&
               xmax == other.xmax &&
               std::strncmp(name, other.name, sizeof(name)) == 0;
    }

    inline bool operator!=(const UserRow &other) const {
        return !(*this == other);
    }

    inline std::string ToString() const {
        std::stringstream ss;
        ss << "UserRow{id=" << id
           << ", name='" << name << "'"
           << ", balance=" << balance
           << ", created_at=" << created_at
           << ", deleted=" << (is_deleted ? "true" : "false")
           << ", xmin=" << xmin
           << ", xmax=" << xmax << "}";
        return ss.str();
    }
};
