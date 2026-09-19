#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include "../buffer_pool_manager_skeleton/page.h"

/**
 * RID (Record Identifier) uniquely identifies the physical location
 * of a tuple within the database.
 * 
 * In standard disk-oriented architectures (CMU 15-445), an RID is a 64-bit value
 * consisting of a 32-bit Page ID and a 32-bit Slot Number.
 */
struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t slot_num{0};

    RID() = default;
    RID(page_id_t page_id, uint32_t slot_num) : page_id(page_id), slot_num(slot_num) {}

    inline bool operator==(const RID &other) const {
        return page_id == other.page_id && slot_num == other.slot_num;
    }

    inline bool operator!=(const RID &other) const {
        return !(*this == other);
    }

    inline bool operator<(const RID &other) const {
        if (page_id != other.page_id) {
            return page_id < other.page_id;
        }
        return slot_num < other.slot_num;
    }

    inline bool operator<=(const RID &other) const {
        return *this < other || *this == other;
    }

    inline bool operator>(const RID &other) const {
        return !(*this <= other);
    }

    inline bool operator>=(const RID &other) const {
        return !(*this < other);
    }

    inline bool IsValid() const {
        return page_id != INVALID_PAGE_ID;
    }

    inline std::string ToString() const {
        std::stringstream ss;
        ss << "[" << page_id << ", " << slot_num << "]";
        return ss.str();
    }
};
