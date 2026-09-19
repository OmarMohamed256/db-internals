#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "io_uring_manager.h"
#include <cstdio>
#include <fcntl.h>
#include <liburing.h>
#include <unistd.h>

IOUringManager::IOUringManager(const std::string &db_file, unsigned sq_depth) {
  db_io_ = open(db_file.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0666);
  if (db_io_ < 0) {
    perror("Failed to open DB file");
  }
  // setup submission queue entries limit (cq is usually double)
  int q = io_uring_queue_init(sq_depth, &ring_, 0);
  if (q < 0) {
    perror("faliure intializing the queue");
  }

  // Pre-allocate request pool and free indices
  request_pool_.resize(sq_depth);
  free_indices_.reserve(sq_depth);
  for (int i = static_cast<int>(sq_depth) - 1; i >= 0; --i) {
    free_indices_.push_back(i);
  }
}

IOUringManager::~IOUringManager() {
  if (db_io_ >= 0) {
    close(db_io_);
  }
  io_uring_queue_exit(&ring_);
}

void IOUringManager::AsyncReadPage(page_id_t page_id, char *page_data,
                                   IOCompletionCallback callback) {
  // If no free slots, wait/poll until one becomes free
  while (free_indices_.empty()) {
    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&ring_, &cqe) == 0) {
      ProcessCompletedEntry(cqe);
    } else {
      break;
    }
  }

  struct io_uring_sqe *sqe_entry = io_uring_get_sqe(&ring_);
  if (!sqe_entry) {
    // If SQ is full, submit and try again
    io_uring_submit(&ring_);
    sqe_entry = io_uring_get_sqe(&ring_);
    if (!sqe_entry) {
      if (callback) {
        callback(false);
      }
      return;
    }
  }

  size_t slot_idx = free_indices_.back();
  free_indices_.pop_back();

  IORequest &req = request_pool_[slot_idx];
  req.callback = std::move(callback);
  req.page_id = page_id;
  req.is_write = false;
  req.active = true;

  off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
  io_uring_prep_read(sqe_entry, db_io_, page_data, PAGE_SIZE, offset);
  io_uring_sqe_set_data(sqe_entry, reinterpret_cast<void *>(slot_idx));

  // Submit the read request to the kernel
  io_uring_submit(&ring_);
}

void IOUringManager::AsyncWritePage(page_id_t page_id, const char *page_data,
                                    IOCompletionCallback callback) {
  // If no free slots, wait/poll until one becomes free
  while (free_indices_.empty()) {
    struct io_uring_cqe *cqe;
    if (io_uring_wait_cqe(&ring_, &cqe) == 0) {
      ProcessCompletedEntry(cqe);
    } else {
      break;
    }
  }

  struct io_uring_sqe *sqe_entry = io_uring_get_sqe(&ring_);
  if (!sqe_entry) {
    // If SQ is full, submit and try again
    io_uring_submit(&ring_);
    sqe_entry = io_uring_get_sqe(&ring_);
    if (!sqe_entry) {
      if (callback) {
        callback(false);
      }
      return;
    }
  }

  size_t slot_idx = free_indices_.back();
  free_indices_.pop_back();

  IORequest &req = request_pool_[slot_idx];
  req.callback = std::move(callback);
  req.page_id = page_id;
  req.is_write = true;
  req.active = true;

  off_t offset = static_cast<off_t>(page_id) * PAGE_SIZE;
  io_uring_prep_write(sqe_entry, db_io_, page_data, PAGE_SIZE, offset);
  io_uring_sqe_set_data(sqe_entry, reinterpret_cast<void *>(slot_idx));

  // Submit the write request to the kernel
  io_uring_submit(&ring_);
}

int IOUringManager::PollCompletions() {
  struct io_uring_cqe *cqe;
  int completed_count = 0;

  // Peek and process all completed CQEs in the queue
  while (io_uring_peek_cqe(&ring_, &cqe) == 0) {
    if (cqe) {
      ProcessCompletedEntry(cqe);
      completed_count++;
    }
  }

  return completed_count;
}

void IOUringManager::ProcessCompletedEntry(struct io_uring_cqe *cqe) {
  size_t slot_idx = reinterpret_cast<size_t>(io_uring_cqe_get_data(cqe));

  if (slot_idx < request_pool_.size()) {
    IORequest &req = request_pool_[slot_idx];
    if (req.active) {
      bool success = (cqe->res >= 0);

      if (req.callback) {
        req.callback(success);
        req.callback = nullptr;
      }

      req.active = false;
      free_indices_.push_back(slot_idx);
    }
  }

  io_uring_cqe_seen(&ring_, cqe);
}
