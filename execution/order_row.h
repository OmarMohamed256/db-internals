#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include "../storage/user_row.h"

/**
 * OrderRow: Fixed-width schema representing customer transaction/order records.
 * Used for relational join algorithms (Sprint 1.3).
 */
struct OrderRow {
    int64_t order_id{0};       // Primary Key for orders
    int64_t user_id{0};        // Foreign Key referencing UserRow.id
    double amount{0.0};        // Transaction amount
    uint64_t order_date{0};    // Epoch timestamp
    bool is_deleted{false};    // Tombstone flag

    OrderRow() = default;

    OrderRow(int64_t order_id, int64_t user_id, double amount, uint64_t order_date = 0)
        : order_id(order_id), user_id(user_id), amount(amount), order_date(order_date), is_deleted(false) {}

    inline bool operator==(const OrderRow &other) const {
        return order_id == other.order_id &&
               user_id == other.user_id &&
               amount == other.amount &&
               order_date == other.order_date &&
               is_deleted == other.is_deleted;
    }

    inline bool operator!=(const OrderRow &other) const {
        return !(*this == other);
    }

    inline std::string ToString() const {
        std::stringstream ss;
        ss << "OrderRow{order_id=" << order_id
           << ", user_id=" << user_id
           << ", amount=" << amount
           << ", order_date=" << order_date << "}";
        return ss.str();
    }
};

/**
 * JoinedUserOrderRow: Materialized result schema for relational joins
 * (e.g. Users JOIN Orders ON users.id = orders.user_id).
 */
struct JoinedUserOrderRow {
    int64_t user_id{0};
    char user_name[32]{0};
    double user_balance{0.0};
    int64_t order_id{0};
    double order_amount{0.0};

    JoinedUserOrderRow() = default;

    JoinedUserOrderRow(const UserRow &user, const OrderRow &order)
        : user_id(user.id),
          user_balance(user.balance),
          order_id(order.order_id),
          order_amount(order.amount) {
        std::strncpy(user_name, user.name, sizeof(user_name) - 1);
        user_name[sizeof(user_name) - 1] = '\0';
    }

    inline bool operator==(const JoinedUserOrderRow &other) const {
        return user_id == other.user_id &&
               user_balance == other.user_balance &&
               order_id == other.order_id &&
               order_amount == other.order_amount &&
               std::strncmp(user_name, other.user_name, sizeof(user_name)) == 0;
    }

    inline std::string ToString() const {
        std::stringstream ss;
        ss << "JoinedRow{user_id=" << user_id
           << ", name='" << user_name << "'"
           << ", balance=" << user_balance
           << ", order_id=" << order_id
           << ", amount=" << order_amount << "}";
        return ss.str();
    }
};
