#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <set>
#include <unordered_map>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>

using namespace std;

size_t tasksCount;
double l;
double r;
std::set<string> workersAddresses;
const size_t PORT = 12345;
const size_t MAX_EVENTS = 100;
const size_t BUFFER_SIZE = 1024; // а больше не надо? что будет если не хватит на одно сообщение?
char buffer[BUFFER_SIZE];

int set_nonblocking(int sockfd) {
    int flags, s;
    flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        exit(1); // TODO exit?
    }

    flags |= O_NONBLOCK;
    s = fcntl(sockfd, F_SETFL, flags);
    if (s == -1) {
        perror("fcntl");
        exit(1); // TODO exit?
    }

    return 0;
}

int create_and_bind_udp(int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("UDP socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("UDP bind failed");
        close(sockfd);
        exit(1);
    }

    return sockfd;
}

int create_and_bind_tcp(int port) {
    int sockfd;
    struct sockaddr_in server_addr;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("TCP socket creation failed");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("TCP bind failed");
        close(sockfd);
        exit(1);
    }

    if (listen(sockfd, 10) < 0) {
        perror("Listen failed");
        close(sockfd);
        exit(1);
    }

    return sockfd;
}

int main() {
    cin >> l >> r;
    if (l > r) {
        cerr << "l must be not more than r";
        exit(1);
    }

    // TODO get workersAddresses using broadcast. Далее в epoll-е еще придется ждать таймаут, чтобы пришло определенное число воркеров.
    workersAddresses = {"1.2.3.4", "5.6.7.8", "9.10.11.12"};

    int udp_sock, tcp_sock, conn_sock;
    udp_sock = create_and_bind_udp(PORT);
    tcp_sock = create_and_bind_tcp(PORT);
    set_nonblocking(udp_sock);
    set_nonblocking(tcp_sock);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(1);
    }

    struct epoll_event ev, events[MAX_EVENTS];
    
    ev.events = EPOLLIN;
    ev.data.fd = udp_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_sock, &ev) == -1) {
        perror("epoll_ctl: udp_sock");
        exit(1);
    }

    ev.events = EPOLLIN;
    ev.data.fd = tcp_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, tcp_sock, &ev) == -1) {
        perror("epoll_ctl: tcp_sock");
        exit(1);
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    while (true) {
        int nEvents = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nEvents == -1) {
            perror("epoll_wait");
            exit(1);
        }

        for (int i = 0; i < nEvents; ++i) {
            if (events[i].data.fd == udp_sock) {
                memset(buffer, 0, BUFFER_SIZE);
                recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);

                cout << "Received heartbeat from client " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << endl;
            } else {
                
            }
        }
    }

    close(udp_sock);
    close(tcp_sock);
    close(epoll_fd);
}
