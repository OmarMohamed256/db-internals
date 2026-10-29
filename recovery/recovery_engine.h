#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <queue>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../buffer_pool_manager_skeleton/buffer_pool_manager.h"
#include "../storage/table_page.h"
#include "log_manager.h"
#include "log_record.h"

/**
 * Transaction status in ARIES Active Transaction Table (ATT).
 */
enum class TxnStatus { RUNNING, COMMITTING, ABORTING };

/**
 * Entry in the Active Transaction Table (ATT).
 */
struct ATTEntry {
  txn_id_t txn_id{0};
  lsn_t last_lsn{INVALID_LSN};
  TxnStatus status{TxnStatus::RUNNING};
};

/**
 * ARIES Recovery Engine (CMU 15-445 Lecture #22 & Mohan et al. 1992).
 *
 * Implements the landmark 3-phase ARIES recovery algorithm:
 *   1. Analysis Phase: Scans log forward, reconstructs ATT and DPT (with
 * recLSN), identifies loser transactions, and determines min_recLSN.
 *   2. Redo Phase (Repeating History): Scans forward from min_recLSN with
 * 3-tier disk I/O filter optimizations (skips disk fetch if page ∉ DPT or lsn <
 * recLSN).
 *   3. Undo Phase: Scans backward (bottom-up) across active loser transactions,
 *      reverses uncommitted modifications, and writes Compensation Log Records
 * (CLRs).
 */
class RecoveryEngine {
public:
  RecoveryEngine(BufferPoolManager *bpm, const std::string &log_filename,
                 LogManager *log_mgr = nullptr)
      : bpm_(bpm), log_filename_(log_filename), log_mgr_(log_mgr) {}

  /**
   * Executes the complete 3-Phase ARIES Crash Recovery algorithm.
   */
  void Recover(const std::string &meta_filename = "checkpoint.meta") {
    LoadLogRecords();
    Phase1Analysis(meta_filename);
    Phase2Redo();
    Phase3Undo();
  }

  /**
   * Generates an ARIES Fuzzy Checkpoint.
   * 1. Appends CHECKPOINT_BEGIN to WAL.
   * 2. Takes point-in-time snapshots of ATT and DPT without halting transactions.
   * 3. Appends CHECKPOINT_END containing the snapshot maps.
   * 4. Flushes WAL to disk.
   * 5. Persists the Master Record (checkpoint_begin_lsn) to disk file.
   */
  lsn_t CreateCheckpoint(const std::unordered_map<txn_id_t, lsn_t> &active_txns = {},
                         const std::unordered_map<page_id_t, lsn_t> &dirty_pages = {},
                         const std::string &meta_filename = "checkpoint.meta") {
    if (log_mgr_ == nullptr) {
      return INVALID_LSN;
    }

    // 1. Append CHECKPOINT_BEGIN
    LogRecord begin_rec(0, INVALID_LSN, LogRecordType::CHECKPOINT_BEGIN);
    lsn_t begin_lsn = log_mgr_->AppendRecord(&begin_rec);

    // 2. Snapshot ATT
    std::unordered_map<txn_id_t, lsn_t> att_snap;
    if (!active_txns.empty()) {
      att_snap = active_txns;
    } else {
      for (const auto &pair : att_) {
        att_snap[pair.first] = pair.second.last_lsn;
      }
    }

    // Snapshot DPT
    std::unordered_map<page_id_t, lsn_t> dpt_snap;
    if (!dirty_pages.empty()) {
      dpt_snap = dirty_pages;
    } else {
      dpt_snap = dpt_;
    }

    // 3. Append CHECKPOINT_END
    LogRecord end_rec(begin_lsn, att_snap, dpt_snap);
    lsn_t end_lsn = log_mgr_->AppendRecord(&end_rec);

    // 4. Force Flush WAL
    log_mgr_->Flush(end_lsn, true);

    // 5. Persist Master Record
    std::ofstream meta(meta_filename, std::ios::binary | std::ios::trunc);
    if (meta.is_open()) {
      meta.write(reinterpret_cast<const char *>(&begin_lsn), sizeof(lsn_t));
      meta.flush();
    }

    return begin_lsn;
  }

  /**
   * Phase 1: Analysis Phase (Checkpoint-Aware)
   * If a valid Master Record exists (checkpoint.meta), loads ATT and DPT snapshots
   * from CHECKPOINT_END and starts scanning forward from CHECKPOINT_BEGIN LSN,
   * bypassing ancient log history. Otherwise, falls back to full-log scan from LSN 1.
   */
  void Phase1Analysis(const std::string &meta_filename = "checkpoint.meta") {
    if (log_records_.empty()) {
      LoadLogRecords();
    }
    att_.clear();
    dpt_.clear();
    checkpoint_bypassed_records_count_ = 0;

    lsn_t checkpoint_begin_lsn = INVALID_LSN;
    std::ifstream meta(meta_filename, std::ios::binary);
    if (meta.is_open()) {
      meta.read(reinterpret_cast<char *>(&checkpoint_begin_lsn), sizeof(lsn_t));
    }

    size_t start_index = 0;

    if (checkpoint_begin_lsn != INVALID_LSN) {
      // 1. Locate CHECKPOINT_END matching checkpoint_begin_lsn
      for (size_t i = 0; i < log_records_.size(); ++i) {
        const auto &rec = log_records_[i];
        if (rec.type == LogRecordType::CHECKPOINT_END && rec.prev_lsn == checkpoint_begin_lsn) {
          for (const auto &pair : rec.checkpoint_att) {
            att_[pair.first] = {pair.first, pair.second, TxnStatus::RUNNING};
          }
          for (const auto &pair : rec.checkpoint_dpt) {
            dpt_[pair.first] = pair.second;
          }
          break;
        }
      }

      // 2. Locate CHECKPOINT_BEGIN to start forward scanning from there
      for (size_t i = 0; i < log_records_.size(); ++i) {
        if (log_records_[i].lsn >= checkpoint_begin_lsn) {
          start_index = i;
          break;
        }
      }
      checkpoint_bypassed_records_count_ = start_index;
    }

    // Scan forward from start_index to EOF
    for (size_t i = start_index; i < log_records_.size(); ++i) {
      const auto &rec = log_records_[i];
      // 1. Transaction starts
      if (rec.type == LogRecordType::BEGIN) {
        att_[rec.txn_id] = {rec.txn_id, rec.lsn, TxnStatus::RUNNING};
      }
      // 2. Transaction commits: remove from active loser set!
      else if (rec.type == LogRecordType::COMMIT) {
        att_.erase(rec.txn_id);
      }
      // 3. Transaction aborts
      else if (rec.type == LogRecordType::ABORT) {
        if (att_.find(rec.txn_id) != att_.end()) {
          att_[rec.txn_id].status = TxnStatus::ABORTING;
          att_[rec.txn_id].last_lsn = rec.lsn;
        }
      }
      // 4. Data modification or CLR record
      else if (rec.type == LogRecordType::INSERT ||
               rec.type == LogRecordType::UPDATE ||
               rec.type == LogRecordType::MARK_DELETE ||
               rec.type == LogRecordType::APPLY_DELETE ||
               rec.type == LogRecordType::CLR) {
        // Update ATT
        att_[rec.txn_id].txn_id = rec.txn_id;
        att_[rec.txn_id].last_lsn = rec.lsn;
        if (att_[rec.txn_id].status != TxnStatus::ABORTING) {
          att_[rec.txn_id].status = TxnStatus::RUNNING;
        }

        // Update DPT: record first dirtying LSN (recLSN)
        if (dpt_.find(rec.page_id) == dpt_.end()) {
          dpt_[rec.page_id] = rec.lsn;
        }
      }
    }
  }

  /**
   * Phase 2: Redo Phase (Repeating History with 3-Tier Disk-I/O Filter)
   * Starts from min_recLSN in DPT.
   */
  size_t Phase2Redo() {
    lsn_t min_rec_lsn = GetMinRecLSN();
    if (min_rec_lsn == INVALID_LSN) {
      return 0; // No dirty pages in DPT!
    }

    size_t replayed_count = 0;

    for (const auto &rec : log_records_) {
      // Skip records prior to earliest dirty page
      if (rec.lsn < min_rec_lsn)
        continue;

      // Skip non-data records (BEGIN, COMMIT, ABORT)
      if (rec.type != LogRecordType::INSERT &&
          rec.type != LogRecordType::UPDATE &&
          rec.type != LogRecordType::MARK_DELETE &&
          rec.type != LogRecordType::APPLY_DELETE &&
          rec.type != LogRecordType::CLR) {
        continue;
      }

      // TIER 1: DPT Membership Filter (0 DISK I/O)
      // If page is not in DPT, it was never dirty or was flushed before crash!
      auto dpt_it = dpt_.find(rec.page_id);
      if (dpt_it == dpt_.end()) {
        disk_io_skipped_count_++;
        continue;
      }

      // TIER 2: recLSN Filter (0 DISK I/O)
      // If record.lsn < recLSN, changes up to recLSN are already durable!
      if (rec.lsn < dpt_it->second) {
        disk_io_skipped_count_++;
        continue;
      }

      // TIER 3: Page LSN Check (Read Disk ONLY when necessary)
      Page *raw_page = bpm_->FetchPage(rec.page_id);
      if (raw_page == nullptr)
        continue;

      auto *table_page = reinterpret_cast<TablePage *>(raw_page->GetData());
      if (rec.lsn <= static_cast<lsn_t>(table_page->GetLSN())) {
        bpm_->UnpinPage(rec.page_id, false); // Already durable, unpin clean
        disk_io_skipped_count_++;
        continue;
      }

      // Apply modification (Repeating History)
      ApplyRecord(table_page, rec);
      table_page->SetLSN(rec.lsn);
      bpm_->UnpinPage(rec.page_id, true); // Dirty page, unpin dirty
      replayed_count++;
    }

    return replayed_count;
  }

  /**
   * Phase 3: Undo Phase (Bottom-Up Rollback of Losers with CLRs)
   */
  size_t Phase3Undo() {
    if (att_.empty()) {
      return 0; // No active loser transactions to undo!
    }

    // Priority queue (max-heap) tracks the most recent LSN to undo across all loser transactions
    std::priority_queue<lsn_t> to_undo;
    std::unordered_map<lsn_t, LogRecord> lsn_map;

    for (const auto &rec : log_records_) {
      lsn_map[rec.lsn] = rec;
    }

    for (const auto &pair : att_) {
      if (pair.second.last_lsn != INVALID_LSN) {
        to_undo.push(pair.second.last_lsn);
      }
    }

    size_t undone_count = 0;

    while (!to_undo.empty()) {
      lsn_t cur_lsn = to_undo.top();
      to_undo.pop();

      auto it = lsn_map.find(cur_lsn);
      if (it == lsn_map.end()) {
        continue;
      }

      const LogRecord &rec = it->second;

      if (rec.type == LogRecordType::CLR) {
        // CLR record encountered: follow undo_next_lsn (bypasses already undone records!)
        if (rec.undo_next_lsn != INVALID_LSN) {
          to_undo.push(rec.undo_next_lsn);
        }
      } else if (rec.type == LogRecordType::INSERT ||
                 rec.type == LogRecordType::UPDATE ||
                 rec.type == LogRecordType::MARK_DELETE) {
        // Revert operation in database buffer pool:
        Page *raw_page = bpm_->FetchPage(rec.page_id);
        if (raw_page != nullptr) {
          auto *table_page = reinterpret_cast<TablePage *>(raw_page->GetData());
          RevertRecord(table_page, rec);

          // Write Compensation Log Record (CLR) to WAL
          lsn_t clr_lsn = INVALID_LSN;
          if (log_mgr_ != nullptr) {
            LogRecord clr(rec.txn_id, rec.lsn, rec.prev_lsn,
                          rec.page_id, rec.slot_num, rec.before_image);
            clr_lsn = log_mgr_->AppendRecord(&clr);
            table_page->SetLSN(clr_lsn);
          }
          bpm_->UnpinPage(rec.page_id, true);
          undone_count++;
        }

        // Continue backward down the transaction's undo chain
        if (rec.prev_lsn != INVALID_LSN) {
          to_undo.push(rec.prev_lsn);
        }
      } else if (rec.type == LogRecordType::BEGIN) {
        // Reached start of loser transaction: write END / ABORT record
        if (log_mgr_ != nullptr) {
          LogRecord abort_rec(rec.txn_id, rec.lsn, LogRecordType::ABORT);
          log_mgr_->AppendRecord(&abort_rec);
        }
      }
    }

    att_.clear();
    return undone_count;
  }

  /**
   * Backward-compatible simple redo for basic crash scenarios.
   */
  inline size_t Redo() {
    LoadLogRecords();
    Phase1Analysis();
    return Phase2Redo();
  }

  inline lsn_t GetMinRecLSN() const {
    lsn_t min_rec_lsn = INVALID_LSN;
    for (const auto &pair : dpt_) {
      if (min_rec_lsn == INVALID_LSN || pair.second < min_rec_lsn) {
        min_rec_lsn = pair.second;
      }
    }
    return min_rec_lsn;
  }

  inline const std::unordered_map<txn_id_t, ATTEntry> &GetATT() const {
    return att_;
  }
  inline const std::unordered_map<page_id_t, lsn_t> &GetDPT() const {
    return dpt_;
  }
  inline size_t GetDiskIOSkippedCount() const { return disk_io_skipped_count_; }
  inline size_t GetCheckpointBypassedCount() const {
    return checkpoint_bypassed_records_count_;
  }

private:
  void LoadLogRecords() {
    log_records_.clear();
    int fd = ::open(log_filename_.c_str(), O_RDONLY);
    if (fd < 0)
      return;

    off_t file_size = ::lseek(fd, 0, SEEK_END);
    ::lseek(fd, 0, SEEK_SET);
    if (file_size <= 0) {
      ::close(fd);
      return;
    }

    std::vector<char> buffer(file_size);
    ssize_t bytes_read = ::read(fd, buffer.data(), file_size);
    ::close(fd);
    if (bytes_read <= 0)
      return;

    size_t offset = 0;
    while (offset + LogRecord::HEADER_SIZE <= static_cast<size_t>(bytes_read)) {
      LogRecord record;
      if (!LogRecord::Deserialize(buffer.data() + offset, bytes_read - offset,
                                  &record)) {
        break;
      }
      log_records_.push_back(record);
      offset += record.GetSize();
    }
  }

  void ApplyRecord(TablePage *page, const LogRecord &record) {
    if (page->GetFreeSpaceRemaining() == 0 && page->GetNumRecords() == 0) {
      page_id_t prev_id = (record.page_id > 0) ? (record.page_id - 1) : INVALID_PAGE_ID;
      page->Init(record.page_id, prev_id, INVALID_PAGE_ID);
      if (prev_id != INVALID_PAGE_ID) {
        Page *prev_raw = bpm_->FetchPage(prev_id);
        if (prev_raw != nullptr) {
          auto *prev_tp = reinterpret_cast<TablePage *>(prev_raw->GetData());
          prev_tp->SetNextPageId(record.page_id);
          bpm_->UnpinPage(prev_id, true);
        }
      }
    }

    if (record.type == LogRecordType::INSERT || record.type == LogRecordType::CLR) {
      if (record.slot_num >= page->GetNumRecords()) {
        RID rid;
        page->InsertTuple(record.after_image, &rid);
      } else {
        std::memcpy(page->GetData() + page->GetSlots()[record.slot_num].offset,
                    &record.after_image, sizeof(UserRow));
        page->GetSlots()[record.slot_num].size = sizeof(UserRow);
      }
    } else if (record.type == LogRecordType::UPDATE) {
      page->UpdateTuple(record.slot_num, record.after_image);
    } else if (record.type == LogRecordType::MARK_DELETE ||
               record.type == LogRecordType::APPLY_DELETE) {
      page->MarkDelete(record.slot_num);
    }
  }

  void RevertRecord(TablePage *page, const LogRecord &record) {
    if (record.type == LogRecordType::INSERT) {
      page->MarkDelete(record.slot_num);
    } else if (record.type == LogRecordType::UPDATE) {
      page->UpdateTuple(record.slot_num, record.before_image);
    } else if (record.type == LogRecordType::MARK_DELETE) {
      std::memcpy(page->GetData() + page->GetSlots()[record.slot_num].offset,
                  &record.after_image, sizeof(UserRow));
      page->GetSlots()[record.slot_num].size = sizeof(UserRow);
    }
  }

  BufferPoolManager *bpm_{nullptr};
  std::string log_filename_;
  LogManager *log_mgr_{nullptr};

  std::vector<LogRecord> log_records_;
  std::unordered_map<txn_id_t, ATTEntry> att_;
  std::unordered_map<page_id_t, lsn_t> dpt_;
  size_t disk_io_skipped_count_{0};
  size_t checkpoint_bypassed_records_count_{0};
};
