#pragma once

#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "../buffer_pool_manager_skeleton/page.h"
#include "../storage/user_row.h"

using lsn_t = int64_t;
using txn_id_t = int64_t;

static constexpr lsn_t INVALID_LSN = -1;

/**
 * LogRecordType defines the semantic categories of physical/logical WAL records
 * in ARIES.
 */
enum class LogRecordType : uint32_t {
  INVALID = 0,
  BEGIN = 1,
  COMMIT = 2,
  ABORT = 3,
  INSERT = 4,
  UPDATE = 5,
  MARK_DELETE = 6,
  APPLY_DELETE = 7,
  CLR = 8, // Compensation Log Record (ARIES Undo Logging)
  CHECKPOINT_BEGIN = 9,
  CHECKPOINT_END = 10,
};

/**
 * LogRecord represents a single Write-Ahead Logging (WAL) entry under the ARIES
 * recovery protocol.
 *
 * Binary Layout:
 * +-----------------------------------------------------------------------------------+
 * | Header (40 bytes): | |   size (4B) | lsn (8B) | prev_lsn (8B) | txn_id (8B)
 * | type (4B) | undo_next (8B) |
 * +-----------------------------------------------------------------------------------+
 * | Payload (0, 88, or 168 bytes): | |   BEGIN / COMMIT / ABORT: None (total 40
 * bytes)                                   | |   INSERT / MARK_DELETE / CLR:
 * page_id (4B), slot_num (4B), after_image (80B)      | |   UPDATE: page_id
 * (4B), slot_num (4B), before_image (80B), after_image (80B)      |
 * +-----------------------------------------------------------------------------------+
 */
struct LogRecord {
  static constexpr size_t HEADER_SIZE = 40;

  uint32_t size{0};
  lsn_t lsn{INVALID_LSN};
  lsn_t prev_lsn{INVALID_LSN};
  txn_id_t txn_id{0};
  LogRecordType type{LogRecordType::INVALID};
  lsn_t undo_next_lsn{INVALID_LSN}; // ARIES: Next LSN to undo for this
                                    // transaction during rollback

  // Payload for tuple-level operations
  page_id_t page_id{INVALID_PAGE_ID};
  uint32_t slot_num{0};
  UserRow before_image;
  UserRow after_image;

  // Checkpoint snapshots for CHECKPOINT_END
  std::unordered_map<txn_id_t, lsn_t> checkpoint_att;
  std::unordered_map<page_id_t, lsn_t> checkpoint_dpt;

  LogRecord() = default;

  // Transaction lifecycle constructor (BEGIN, COMMIT, ABORT)
  LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type)
      : prev_lsn(prev_lsn), txn_id(txn_id), type(type) {
    size = HEADER_SIZE;
  }

  // Insert / Delete constructor
  LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type,
            page_id_t page_id, uint32_t slot_num, const UserRow &tuple_image)
      : prev_lsn(prev_lsn), txn_id(txn_id), type(type), page_id(page_id),
        slot_num(slot_num), after_image(tuple_image) {
    size = HEADER_SIZE + sizeof(page_id_t) + sizeof(uint32_t) + sizeof(UserRow);
  }

  // Update constructor (before_image and after_image for full physical
  // undo/redo)
  LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type,
            page_id_t page_id, uint32_t slot_num, const UserRow &before_image,
            const UserRow &after_image)
      : prev_lsn(prev_lsn), txn_id(txn_id), type(type), page_id(page_id),
        slot_num(slot_num), before_image(before_image),
        after_image(after_image) {
    size = HEADER_SIZE + sizeof(page_id_t) + sizeof(uint32_t) +
           (2 * sizeof(UserRow));
  }

  // ARIES Compensation Log Record (CLR) constructor
  LogRecord(txn_id_t txn_id, lsn_t prev_lsn, lsn_t undo_next_lsn,
            page_id_t page_id, uint32_t slot_num,
            const UserRow &compensated_image)
      : prev_lsn(prev_lsn), txn_id(txn_id), type(LogRecordType::CLR),
        undo_next_lsn(undo_next_lsn), page_id(page_id), slot_num(slot_num),
        after_image(compensated_image) {
    size = HEADER_SIZE + sizeof(page_id_t) + sizeof(uint32_t) + sizeof(UserRow);
  }

  // ARIES Checkpoint END constructor
  LogRecord(lsn_t checkpoint_begin_lsn,
            const std::unordered_map<txn_id_t, lsn_t> &att,
            const std::unordered_map<page_id_t, lsn_t> &dpt)
      : prev_lsn(checkpoint_begin_lsn), txn_id(0), type(LogRecordType::CHECKPOINT_END),
        checkpoint_att(att), checkpoint_dpt(dpt) {
    size = HEADER_SIZE + sizeof(uint32_t) + (att.size() * (sizeof(txn_id_t) + sizeof(lsn_t))) +
           sizeof(uint32_t) + (dpt.size() * (sizeof(page_id_t) + sizeof(lsn_t)));
  }

  inline size_t GetSize() const { return size; }
  inline lsn_t GetLSN() const { return lsn; }
  inline void SetLSN(lsn_t new_lsn) { lsn = new_lsn; }

  /**
   * Serializes this LogRecord into a contiguous binary destination buffer.
   * Returns total bytes written (matches size).
   */
  size_t Serialize(char *dest) const {
    char *ptr = dest;

    // --- Pack the 40-byte fixed header ---
    std::memcpy(ptr, &size, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    std::memcpy(ptr, &lsn, sizeof(lsn_t));
    ptr += sizeof(lsn_t);
    std::memcpy(ptr, &prev_lsn, sizeof(lsn_t));
    ptr += sizeof(lsn_t);
    std::memcpy(ptr, &txn_id, sizeof(txn_id_t));
    ptr += sizeof(txn_id_t);

    uint32_t type_val = static_cast<uint32_t>(type);
    std::memcpy(ptr, &type_val, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    std::memcpy(ptr, &undo_next_lsn, sizeof(lsn_t));
    ptr += sizeof(lsn_t);

    if (type == LogRecordType::INSERT || type == LogRecordType::MARK_DELETE ||
        type == LogRecordType::APPLY_DELETE || type == LogRecordType::CLR) {

      std::memcpy(ptr, &page_id, sizeof(page_id_t));
      ptr += sizeof(page_id_t);
      std::memcpy(ptr, &slot_num, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      std::memcpy(ptr, &after_image, sizeof(UserRow));
      ptr += sizeof(UserRow);
    } else if (type == LogRecordType::UPDATE) {
      std::memcpy(ptr, &page_id, sizeof(page_id_t));
      ptr += sizeof(page_id_t);
      std::memcpy(ptr, &slot_num, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      std::memcpy(ptr, &before_image, sizeof(UserRow));
      ptr += sizeof(UserRow);
      std::memcpy(ptr, &after_image, sizeof(UserRow));
      ptr += sizeof(UserRow);
    } else if (type == LogRecordType::CHECKPOINT_END) {
      uint32_t num_att = static_cast<uint32_t>(checkpoint_att.size());
      std::memcpy(ptr, &num_att, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      for (const auto &pair : checkpoint_att) {
        std::memcpy(ptr, &pair.first, sizeof(txn_id_t));
        ptr += sizeof(txn_id_t);
        std::memcpy(ptr, &pair.second, sizeof(lsn_t));
        ptr += sizeof(lsn_t);
      }

      uint32_t num_dpt = static_cast<uint32_t>(checkpoint_dpt.size());
      std::memcpy(ptr, &num_dpt, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      for (const auto &pair : checkpoint_dpt) {
        std::memcpy(ptr, &pair.first, sizeof(page_id_t));
        ptr += sizeof(page_id_t);
        std::memcpy(ptr, &pair.second, sizeof(lsn_t));
        ptr += sizeof(lsn_t);
      }
    }
    // BEGIN, COMMIT, ABORT, CHECKPOINT_BEGIN have no payload

    // --- Return total bytes written ---
    return static_cast<size_t>(ptr - dest);
  }

  /**
   * Deserializes a LogRecord from contiguous binary bytes.
   * Returns true if successfully parsed, false if buffer is too small or
   * corrupt.
   */
  static bool Deserialize(const char *src, size_t max_bytes, LogRecord *out) {
    if (src == nullptr || max_bytes < HEADER_SIZE || out == nullptr) {
      return false;
    }

    const char *ptr = src;

    // 1. Read size first to validate buffer bounds
    std::memcpy(&out->size, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);

    if (out->size > max_bytes || out->size < HEADER_SIZE) {
      return false;
    }

    // 2. Unpack the 40-byte fixed header in exact layout order
    std::memcpy(&out->lsn, ptr, sizeof(lsn_t));
    ptr += sizeof(lsn_t);

    std::memcpy(&out->prev_lsn, ptr, sizeof(lsn_t));
    ptr += sizeof(lsn_t);

    std::memcpy(&out->txn_id, ptr, sizeof(txn_id_t));
    ptr += sizeof(txn_id_t);

    uint32_t type_val = 0;
    std::memcpy(&type_val, ptr, sizeof(uint32_t));
    ptr += sizeof(uint32_t);
    out->type = static_cast<LogRecordType>(type_val);

    std::memcpy(&out->undo_next_lsn, ptr, sizeof(lsn_t));
    ptr += sizeof(lsn_t);

    // 3. Unpack payload conditionally based on record type
    if (out->type == LogRecordType::INSERT ||
        out->type == LogRecordType::MARK_DELETE ||
        out->type == LogRecordType::APPLY_DELETE ||
        out->type == LogRecordType::CLR) {

      size_t expected_size =
          HEADER_SIZE + sizeof(page_id_t) + sizeof(uint32_t) + sizeof(UserRow);
      if (out->size < expected_size) {
        return false;
      }

      std::memcpy(&out->page_id, ptr, sizeof(page_id_t));
      ptr += sizeof(page_id_t);

      std::memcpy(&out->slot_num, ptr, sizeof(uint32_t));
      ptr += sizeof(uint32_t);

      std::memcpy(&out->after_image, ptr, sizeof(UserRow));
      ptr += sizeof(UserRow);

    } else if (out->type == LogRecordType::UPDATE) {

      size_t expected_size = HEADER_SIZE + sizeof(page_id_t) +
                             sizeof(uint32_t) + (2 * sizeof(UserRow));
      if (out->size < expected_size) {
        return false;
      }

      std::memcpy(&out->page_id, ptr, sizeof(page_id_t));
      ptr += sizeof(page_id_t);

      std::memcpy(&out->slot_num, ptr, sizeof(uint32_t));
      ptr += sizeof(uint32_t);

      std::memcpy(&out->before_image, ptr, sizeof(UserRow));
      ptr += sizeof(UserRow);

      std::memcpy(&out->after_image, ptr, sizeof(UserRow));
      ptr += sizeof(UserRow);
    } else if (out->type == LogRecordType::CHECKPOINT_END) {
      if (out->size < HEADER_SIZE + (2 * sizeof(uint32_t))) {
        return false;
      }
      uint32_t num_att = 0;
      std::memcpy(&num_att, ptr, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      out->checkpoint_att.clear();
      for (uint32_t i = 0; i < num_att; ++i) {
        txn_id_t tid = 0;
        lsn_t last_l = INVALID_LSN;
        std::memcpy(&tid, ptr, sizeof(txn_id_t));
        ptr += sizeof(txn_id_t);
        std::memcpy(&last_l, ptr, sizeof(lsn_t));
        ptr += sizeof(lsn_t);
        out->checkpoint_att[tid] = last_l;
      }

      uint32_t num_dpt = 0;
      std::memcpy(&num_dpt, ptr, sizeof(uint32_t));
      ptr += sizeof(uint32_t);
      out->checkpoint_dpt.clear();
      for (uint32_t i = 0; i < num_dpt; ++i) {
        page_id_t pid = INVALID_PAGE_ID;
        lsn_t rec_l = INVALID_LSN;
        std::memcpy(&pid, ptr, sizeof(page_id_t));
        ptr += sizeof(page_id_t);
        std::memcpy(&rec_l, ptr, sizeof(lsn_t));
        ptr += sizeof(lsn_t);
        out->checkpoint_dpt[pid] = rec_l;
      }
    }
    // BEGIN, COMMIT, ABORT, CHECKPOINT_BEGIN have no payload

    return true;
  }

  std::string ToString() const {
    std::stringstream ss;
    ss << "LogRecord{lsn=" << lsn << ", prev_lsn=" << prev_lsn
       << ", txn_id=" << txn_id << ", type=";
    switch (type) {
    case LogRecordType::BEGIN:
      ss << "BEGIN";
      break;
    case LogRecordType::COMMIT:
      ss << "COMMIT";
      break;
    case LogRecordType::ABORT:
      ss << "ABORT";
      break;
    case LogRecordType::INSERT:
      ss << "INSERT(page=" << page_id << ", slot=" << slot_num << ")";
      break;
    case LogRecordType::UPDATE:
      ss << "UPDATE(page=" << page_id << ", slot=" << slot_num << ")";
      break;
    case LogRecordType::MARK_DELETE:
      ss << "MARK_DELETE(page=" << page_id << ", slot=" << slot_num << ")";
      break;
    case LogRecordType::APPLY_DELETE:
      ss << "APPLY_DELETE(page=" << page_id << ", slot=" << slot_num << ")";
      break;
    case LogRecordType::CLR:
      ss << "CLR(undo_next=" << undo_next_lsn << ", page=" << page_id
         << ", slot=" << slot_num << ")";
      break;
    case LogRecordType::CHECKPOINT_BEGIN:
      ss << "CHECKPOINT_BEGIN";
      break;
    case LogRecordType::CHECKPOINT_END:
      ss << "CHECKPOINT_END(att_size=" << checkpoint_att.size()
         << ", dpt_size=" << checkpoint_dpt.size() << ")";
      break;
    default:
      ss << "INVALID";
      break;
    }
    ss << ", size=" << size << "B}";
    return ss.str();
  }
};
