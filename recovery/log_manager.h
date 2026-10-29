#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "log_record.h"

static constexpr size_t LOG_BUFFER_SIZE = 64 * 1024; // 64 KB double-buffers

/**
 * LogManager coordinates sequential append-only Write-Ahead Logging (WAL)
 * and durable disk synchronization under Group Commit.
 *
 * Mechanical Sympathy & Natural Batching:
 * - CPU memory writes execute in nanoseconds; disk fsync takes 50-200
 * microseconds.
 * - Under concurrent commits, instead of every thread stalling disk queues with
 * serial fsyncs, one thread becomes the Leader/Flusher.
 * - While the Leader flushes and fsyncs to disk, Followers naturally accumulate
 * their commits in memory without blocking.
 * - When the Leader finishes, the next flush automatically processes the entire
 * batch that accumulated during the slow I/O cycle with zero artificial sleep
 * delays!
 */
class LogManager {
public:
  explicit LogManager(const std::string &log_filename)
      : log_filename_(log_filename) {
    log_fd_ = ::open(log_filename.c_str(), O_CREAT | O_RDWR | O_APPEND, 0644);
    if (log_fd_ < 0) {
      std::perror("Failed to open WAL log file");
    }
    write_buffer_ = new char[LOG_BUFFER_SIZE];
    flush_buffer_ = new char[LOG_BUFFER_SIZE];
  }

  ~LogManager() {
    Flush(next_lsn_.load() - 1, true);
    if (log_fd_ >= 0) {
      ::close(log_fd_);
    }
    delete[] write_buffer_;
    delete[] flush_buffer_;
  }

  /**
   * Appends a LogRecord to the active in-memory buffer.
   * Assigns a monotonic LSN and returns the assigned LSN.
   */
  lsn_t AppendRecord(LogRecord *record) {
    std::unique_lock<std::mutex> lock(latch_);
    if (write_buffer_offset_ + record->GetSize() > LOG_BUFFER_SIZE) {
      while (is_flushing_) {
        cv_flush_done_.wait(lock);
      }
      FlushInternal(lock, true);
    }
    record->lsn = next_lsn_++;
    record->Serialize(write_buffer_ + write_buffer_offset_);

    write_buffer_offset_ += record->GetSize();
    return record->lsn;
  }

  /**
   * Flushes the WAL buffer up to target_lsn to persistent disk.
   * Implements Leader-Follower Group Commit with Natural Batching.
   */
  void Flush(lsn_t target_lsn, bool force = false) {
    // 1. Fast path: if target_lsn is already flushed, return immediately
    if (target_lsn <= flushed_lsn_.load() && !force) {
      return;
    }

    std::unique_lock<std::mutex> lock(latch_);
    // 2. Double-check condition under lock
    if (target_lsn <= flushed_lsn_.load() && !force) {
      return;
    }

    // 3. While another thread is currently flushing, act as a follower and wait
    while (is_flushing_) {
      cv_flush_done_.wait(lock, [this, target_lsn, force]() {
        return (flushed_lsn_.load() >= target_lsn && !force) || !is_flushing_;
      });
      if (target_lsn <= flushed_lsn_.load() && !force) {
        return;
      }
    }

    // 4. We are the Leader: execute physical flush
    FlushInternal(lock, force);
  }

  inline lsn_t GetFlushedLSN() const { return flushed_lsn_.load(); }
  inline lsn_t GetNextLSN() const { return next_lsn_.load(); }
  inline const std::string &GetLogFilename() const { return log_filename_; }
  inline size_t GetFlushCount() const { return flush_count_.load(); }
  inline size_t GetTotalRecordsFlushed() const {
    return total_records_flushed_.load();
  }

private:
  /**
   * Internal flush routine executed by the Leader under Mechanical Sympathy.
   * Swaps double-buffers, releases lock during slow disk I/O, then notifies
   * followers.
   */
  void FlushInternal(std::unique_lock<std::mutex> &lock, bool force) {
    if (write_buffer_offset_ == 0 && !force)
      return;
    is_flushing_ = true;
    std::swap(write_buffer_, flush_buffer_);
    size_t bytes_to_write = write_buffer_offset_;
    write_buffer_offset_ = 0;
    lsn_t flusing_up_to_lsn = next_lsn_.load() - 1;
    lock.unlock();

    if (bytes_to_write > 0 && log_fd_ >= 0) {
      ssize_t written = ::write(log_fd_, flush_buffer_, bytes_to_write);
      if (written < 0) {
        std::perror("WAL write failed");
      }
      ::fdatasync(log_fd_);
    }
    lock.lock();
    flushed_lsn_.store(flusing_up_to_lsn);
    flush_count_++;
    is_flushing_ = false;
    cv_flush_done_.notify_all();
  }

  std::string log_filename_;
  int log_fd_{-1};

  std::mutex latch_;
  std::condition_variable cv_flush_done_;
  bool is_flushing_{false};

  std::atomic<lsn_t> next_lsn_{1};
  std::atomic<lsn_t> flushed_lsn_{0};

  char *write_buffer_{nullptr};
  char *flush_buffer_{nullptr};
  size_t write_buffer_offset_{0};

  std::atomic<size_t> flush_count_{0};
  std::atomic<size_t> total_records_flushed_{0};
};
