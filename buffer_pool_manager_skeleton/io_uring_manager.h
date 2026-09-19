#pragma once

#include "page.h"
#include <functional>
#include <liburing.h>
#include <string>
#include <vector>

// Callback invoked when an asynchronous I/O operation completes.
// Passed boolean indicating true for success, false for failure.
using IOCompletionCallback = std::function<void(bool success)>;

/**
 * @brief Manages asynchronous disk I/O operations using the Linux io_uring
 * interface.
 *
 * Provides high-performance, non-blocking page-level read and write
 * capabilities, mapping asynchronous requests to completion callbacks using an
 * event loop model.
 */
class IOUringManager {
public:
  /**
   * @brief Construct a new IOUringManager.
   *
   * @param db_file Path to the database file.
   * @param queue_depth The depth/size of the Submission Queue (SQ).
   */
  explicit IOUringManager(const std::string &db_file,
                          unsigned queue_depth = 128);
  ~IOUringManager();

  // Disable copy semantics to prevent duplicate management of file descriptors
  // and io_uring rings.
  IOUringManager(const IOUringManager &) = delete;
  IOUringManager &operator=(const IOUringManager &) = delete;

  /**
   * @brief Submits an asynchronous read request for a specific page.
   *
   * @param page_id The physical page ID on disk.
   * @param page_data Buffer where the read page content will be stored.
   * @param callback Callback to invoke upon completion of the read.
   */
  void AsyncReadPage(page_id_t page_id, char *page_data,
                     IOCompletionCallback callback);

  /**
   * @brief Submits an asynchronous write request for a specific page.
   *
   * @param page_id The physical page ID on disk.
   * @param page_data Buffer containing the page content to write.
   * @param callback Callback to invoke upon completion of the write.
   */
  void AsyncWritePage(page_id_t page_id, const char *page_data,
                      IOCompletionCallback callback);

  /**
   * @brief Polls the io_uring completion queue (CQ) for finished I/O
   * operations.
   *
   * Iterates through any completed requests, invokes their associated
   * callbacks, and clears them from the ring.
   *
   * @return The number of completed operations processed, or a negative value
   * on error.
   */
  int PollCompletions();

private:
  int db_io_;            // File descriptor for the database file.
  struct io_uring ring_; // The io_uring instance used for asynchronous I/O.

  // Internal structure used as the user data payload for each submission queue
  // entry (SQE). This allows the manager to route completions back to their
  // respective callbacks.
  struct IORequest {
    IOCompletionCallback callback;
    page_id_t page_id;
    bool is_write;
    bool active = false;
  };

  std::vector<IORequest> request_pool_;
  std::vector<size_t> free_indices_;

  /**
   * @brief Processes a completed completion queue entry (CQE).
   *
   * @param cqe Pointer to the completed CQE.
   */
  void ProcessCompletedEntry(struct io_uring_cqe *cqe);
};
