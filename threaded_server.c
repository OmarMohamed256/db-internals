#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT "3490"
#define BACKLOG 20

typedef struct {
  int conn_sock;
  int slot_id;
} thread_arg_t;

int threads[BACKLOG]; // 0 = free, 1 = occupied
sem_t slot_sem;
pthread_mutex_t pool_mutex = PTHREAD_MUTEX_INITIALIZER;

void* thread_printer(void* arg) {
  thread_arg_t* tinfo = (thread_arg_t*)arg;
  int conn_sock = tinfo->conn_sock;
  int slot_id = tinfo->slot_id;
  free(tinfo);

  // Automatically reclaim kernel thread resources on exit
  pthread_detach(pthread_self());

  char buf[4096];
  while (1) {
    ssize_t rs = read(conn_sock, buf, sizeof(buf) - 1);
    if (rs > 0) {
      buf[rs] = '\0';
      printf("%s", buf);
    } else if (rs == 0) {
      break;
    } else {
      perror("read");
      break;
    }
  }

  close(conn_sock);

  // O(1) Direct slot release
  pthread_mutex_lock(&pool_mutex);
  threads[slot_id] = 0;
  pthread_mutex_unlock(&pool_mutex);

  // Signal main thread that ANY slot is now free
  sem_post(&slot_sem);

  return NULL;
}

int main(void) {
  struct addrinfo hints, *servinfo, *p;
  int rv, listen_sockfd, conn_sock;
  struct sockaddr_storage their_addr;
  socklen_t sin_size;

  sem_init(&slot_sem, 0, BACKLOG);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((listen_sockfd =
             socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }

    int optval = 1;
    setsockopt(listen_sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(listen_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(listen_sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  freeaddrinfo(servinfo);

  if (p == NULL) {
    fprintf(stderr, "server: failed to bind\n");
    exit(1);
  }

  if (listen(listen_sockfd, 1024) == -1) {
    perror("listen");
    exit(1);
  }

  printf("server: waiting for connections...\n");

  for (;;) {
    // 1. Wait for a thread slot BEFORE accepting from kernel queue
    sem_wait(&slot_sem);

    sin_size = sizeof their_addr;
    conn_sock = accept(listen_sockfd, (struct sockaddr*)&their_addr, &sin_size);
    if (conn_sock == -1) {
      perror("accept");
      sem_post(&slot_sem);
      continue;
    }

    // Find the first free slot
    pthread_mutex_lock(&pool_mutex);
    int free_slot = -1;
    for (int i = 0; i < BACKLOG; i++) {
      if (threads[i] == 0) {
        threads[i] = 1;
        free_slot = i;
        break;
      }
    }
    pthread_mutex_unlock(&pool_mutex);

    thread_arg_t* tinfo = malloc(sizeof(thread_arg_t));
    tinfo->conn_sock = conn_sock;
    tinfo->slot_id = free_slot;

    pthread_t tid;
    if (pthread_create(&tid, NULL, thread_printer, tinfo) != 0) {
      perror("pthread_create");
      free(tinfo);
      sem_post(&slot_sem);
    }
  }

  sem_destroy(&slot_sem);
  return 0;
}