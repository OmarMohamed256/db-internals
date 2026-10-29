#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NUM_THREADS 10
#define CONNS_PER_THREAD 50
#define SERVER_PORT 3490
#define SERVER_IP "127.0.0.1"

typedef struct {
  int thread_id;
  int success_connects;
  long bytes_written;
  double conn_phase_sec;
  double data_phase_sec;
  double total_thread_sec;
} thread_stats_t;

// Returns high-resolution monotonic time in seconds
static double get_time_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

void* worker_thread(void* arg) {
  int thread_id = *(int*)arg;
  int socks[CONNS_PER_THREAD];
  struct sockaddr_in serv_addr;

  thread_stats_t* stats = malloc(sizeof(thread_stats_t));
  memset(stats, 0, sizeof(thread_stats_t));
  stats->thread_id = thread_id;

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SERVER_PORT);
  inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

  printf("[Thread %d] Starting connection phase...\n", thread_id);

  double thread_start_time = get_time_sec();
  double conn_start_time = get_time_sec();

  // 1. Connect Phase
  for (int i = 0; i < CONNS_PER_THREAD; i++) {
    socks[i] = socket(AF_INET, SOCK_STREAM, 0);
    if (socks[i] < 0) {
      fprintf(stderr, "[Thread %d] socket() failed at conn %d: %s\n", thread_id,
              i, strerror(errno));
      socks[i] = -1;
      continue;
    }

    // Attempt connection
    int res =
        connect(socks[i], (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    if (res < 0) {
      // Handle non-blocking or background handshake in progress
      if (errno == EINPROGRESS || errno == EALREADY) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(socks[i], &writefds);

        struct timeval tv;
        tv.tv_sec = 2;  // 2 second connection timeout
        tv.tv_usec = 0;

        int sel_res = select(socks[i] + 1, NULL, &writefds, NULL, &tv);
        if (sel_res > 0) {
          int error = 0;
          socklen_t len = sizeof(error);
          getsockopt(socks[i], SOL_SOCKET, SO_ERROR, &error, &len);

          if (error == 0) {
            stats->success_connects++;
          } else {
            fprintf(stderr, "[Thread %d] async connect error at conn %d: %s\n",
                    thread_id, i, strerror(error));
            close(socks[i]);
            socks[i] = -1;
          }
        } else {
          fprintf(stderr, "[Thread %d] connect timeout (select) at conn %d\n",
                  thread_id, i);
          close(socks[i]);
          socks[i] = -1;
        }
      } else {
        fprintf(stderr,
                "[Thread %d] connect() failed at conn %d: %s (errno=%d)\n",
                thread_id, i, strerror(errno), errno);
        close(socks[i]);
        socks[i] = -1;
      }
    } else {
      // Connection completed instantly
      stats->success_connects++;
    }
  }

  double conn_end_time = get_time_sec();
  stats->conn_phase_sec = conn_end_time - conn_start_time;

  printf(
      "[Thread %d] Connections ready: %d/%d (took %.2f ms). Sending data...\n",
      thread_id, stats->success_connects, CONNS_PER_THREAD,
      stats->conn_phase_sec * 1000.0);

  // 2. Data Exchange Phase
  double data_start_time = get_time_sec();
  char msg[] = "Hello from stress client\n";
  size_t msg_len = strlen(msg);

  for (int i = 0; i < CONNS_PER_THREAD; i++) {
    if (socks[i] >= 0) {
      ssize_t bytes_sent = write(socks[i], msg, msg_len);
      if (bytes_sent > 0) {
        stats->bytes_written += bytes_sent;
      } else if (bytes_sent < 0) {
        fprintf(stderr, "[Thread %d] write() failed on conn %d: %s\n",
                thread_id, i, strerror(errno));
      }
    }
  }

  double data_end_time = get_time_sec();
  stats->data_phase_sec = data_end_time - data_start_time;

  // Small delay to keep connections open briefly before teardown
  usleep(10000);

  // 3. Teardown Phase
  for (int i = 0; i < CONNS_PER_THREAD; i++) {
    if (socks[i] >= 0) {
      close(socks[i]);
    }
  }

  double thread_end_time = get_time_sec();
  stats->total_thread_sec = thread_end_time - thread_start_time;

  printf("[Thread %d] Completed in %.2f ms.\n", thread_id,
         stats->total_thread_sec * 1000.0);
  return stats;
}

int main() {
  pthread_t threads[NUM_THREADS];
  int thread_ids[NUM_THREADS];

  printf("Launching %d threads, %d connections each (%d total)...\n",
         NUM_THREADS, CONNS_PER_THREAD, NUM_THREADS * CONNS_PER_THREAD);

  double global_start_time = get_time_sec();

  for (int i = 0; i < NUM_THREADS; i++) {
    thread_ids[i] = i;
    if (pthread_create(&threads[i], NULL, worker_thread, &thread_ids[i]) != 0) {
      fprintf(stderr, "Failed to create thread %d: %s\n", i, strerror(errno));
    }
  }

  int total_success_connects = 0;
  long total_bytes_written = 0;
  double total_conn_time = 0.0;
  double total_data_time = 0.0;

  for (int i = 0; i < NUM_THREADS; i++) {
    thread_stats_t* stats = NULL;
    pthread_join(threads[i], (void**)&stats);

    if (stats != NULL) {
      total_success_connects += stats->success_connects;
      total_bytes_written += stats->bytes_written;
      total_conn_time += stats->conn_phase_sec;
      total_data_time += stats->data_phase_sec;
      free(stats);
    }
  }

  double global_end_time = get_time_sec();
  double total_elapsed_sec = global_end_time - global_start_time;

  // Calculate Benchmark Metrics
  double conn_rate = (total_elapsed_sec > 0)
                         ? (total_success_connects / total_elapsed_sec)
                         : 0;
  double throughput_kb =
      (total_elapsed_sec > 0)
          ? ((total_bytes_written / 1024.0) / total_elapsed_sec)
          : 0;
  double avg_thread_conn_ms =
      (NUM_THREADS > 0) ? ((total_conn_time / NUM_THREADS) * 1000.0) : 0;
  double avg_thread_data_ms =
      (NUM_THREADS > 0) ? ((total_data_time / NUM_THREADS) * 1000.0) : 0;

  printf("\n================ BENCHMARK TIMING REPORT ================\n");
  printf("  Total Elapsed Time           : %.3f ms (%.4f sec)\n",
         total_elapsed_sec * 1000.0, total_elapsed_sec);
  printf("  Successful Connections       : %d / %d\n", total_success_connects,
         NUM_THREADS * CONNS_PER_THREAD);
  printf("  Total Bytes Transferred      : %ld bytes (%.2f KB)\n",
         total_bytes_written, total_bytes_written / 1024.0);
  printf("---------------------------------------------------------\n");
  printf("  Connection Throughput        : %.2f conn/sec\n", conn_rate);
  printf("  Data Throughput              : %.2f KB/sec\n", throughput_kb);
  printf("  Avg Thread Conn Phase Duration: %.2f ms\n", avg_thread_conn_ms);
  printf("  Avg Thread Data Phase Duration: %.2f ms\n", avg_thread_data_ms);
  printf("=========================================================\n");

  return 0;
}