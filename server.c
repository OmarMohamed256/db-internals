// An epoll event-loop TCP server in C.
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_EVENTS 10
#define PORT "3490"
#define BACKLOG 20

int main() {
  struct addrinfo hints, *servinfo, *p;
  int n, rv, epollfd, listen_sockfd, nfds, conn_sock;
  struct epoll_event ev, events[MAX_EVENTS];
  char buf[4096];
  struct sockaddr_storage their_addr;
  socklen_t sin_size;

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

  if (listen(listen_sockfd, BACKLOG) == -1) {
    perror("listen");
    exit(1);
  }

  printf("server: waiting for connections...\n");

  epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  ev.events = EPOLLIN;
  ev.data.fd = listen_sockfd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sockfd, &ev) == -1) {
    perror("epoll_ctl: listen_sockfd");
    exit(EXIT_FAILURE);
  }

  for (;;) {
    nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
    sin_size = sizeof their_addr;
    if (nfds == -1) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }
    for (n = 0; n < nfds; ++n) {
      if (events[n].data.fd == listen_sockfd) {
        conn_sock =
            accept(listen_sockfd, (struct sockaddr*)&their_addr, &sin_size);
        if (conn_sock == -1) {
          perror("accept");
          exit(EXIT_FAILURE);
        }

        int flags = fcntl(conn_sock, F_GETFL, 0);
        fcntl(conn_sock, F_SETFL, flags | O_NONBLOCK);

        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = conn_sock;
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
          perror("epoll_ctl: conn_sock");
          exit(EXIT_FAILURE);
        }
      } else {
        int client_fd = events[n].data.fd;

        while (1) {
          // Reserve 1 byte for null terminator by passing sizeof(buf) - 1
          ssize_t rs = read(client_fd, buf, sizeof(buf) - 1);

          if (rs > 0) {
            buf[rs] = '\0';  // Null-terminate string safely
            printf("%s", buf);
          } else if (rs < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // Edge-triggered read buffer completely drained
              break;
            }
            // Connection error
            epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
            break;
          } else {
            // EOF: Client cleanly closed connection
            epoll_ctl(epollfd, EPOLL_CTL_DEL, client_fd, NULL);
            close(client_fd);
            break;
          }
        }
      }
    }
  }
}