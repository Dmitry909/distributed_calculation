#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <chrono>
#include <vector>

using namespace std;

struct Task {
    double l;
    double r;
};

struct WorkerState {
    unordered_set<size_t> tasksIds;
    chrono::time_point<chrono::high_resolution_clock> lastHeartbeat;
};

size_t tasksCount = 0;
double l;
double r;
unordered_map<string, WorkerState> workersStates;
const size_t PORT = 12345;
const size_t MAX_EVENTS = 100;
const size_t BUFFER_SIZE = 1024; // а больше не надо? что будет если не хватит на одно сообщение?
char buffer[BUFFER_SIZE];
const double healthCheckPeriod = 10; // seconds
double workersConnectionWaitingTime = healthCheckPeriod / 2;
chrono::time_point<chrono::high_resolution_clock> lastHealthCheckTime;
const int EPOLL_WAIT_TIMEOUT_MS = 500;

int SetNonblocking(int sockfd) {
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

int CreateAndBindUdp(int port) {
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

int CreateAndBindTcp(int port) {
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

void SetTcpConnectionWithWorkek(const string& workerAddress) {
    // TODO
}

void AssignAndSendTaskToWorker(const string& workerAddress, size_t taskId) {
    workersStates[workerAddress].tasksIds.insert(taskId);
    // TODO send
}

Task GetSubInterval(size_t i) {
    if (tasksCount == 0) {
        return {0, 0};
    }
    return {l + (r - l) / tasksCount * i, l + (r - l) / tasksCount * (i + 1)};
}

double DiffBetweenTimestamps(chrono::time_point<chrono::high_resolution_clock> a, chrono::time_point<chrono::high_resolution_clock> b) {
    return (chrono::duration<double>(b - a)).count();
}

int main() {
    cin >> l >> r;
    if (l > r) {
        cerr << "l must be not more than r";
        exit(1);
    }

    // TODO get workersStates using broadcast. Далее в epoll-е еще придется ждать таймаут, чтобы пришло определенное число воркеров.
    // workersStates = {"1.2.3.4:5555", "5.6.7.8:5555", "9.10.11.12:5555"};
    const auto timeOfBroadcast = chrono::high_resolution_clock::now();

    int udp_sock, tcp_sock, conn_sock;
    udp_sock = CreateAndBindUdp(PORT);
    tcp_sock = CreateAndBindTcp(PORT);
    SetNonblocking(udp_sock);
    SetNonblocking(tcp_sock);

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
    bool task_splitted = false;
    lastHealthCheckTime = chrono::high_resolution_clock::now();
    while (true) {
        int nEvents = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT_MS); // block not more than for EPOLL_WAIT_TIMEOUT_MS ms
        if (nEvents == -1) {
            perror("epoll_wait");
            exit(1);
        }

        const auto currentTime = chrono::high_resolution_clock::now();
        for (int i = 0; i < nEvents; ++i) {
            if (events[i].data.fd == udp_sock) {
                memset(buffer, 0, BUFFER_SIZE);
                recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_addr_len);

                string workerAddress = inet_ntoa(client_addr.sin_addr);
                workerAddress += ":";
                workerAddress += to_string(ntohs(client_addr.sin_port));

                cout << "Received heartbeat from client " << workerAddress << endl;

                workersStates[workerAddress].lastHeartbeat = currentTime;
            } else {
                
            }
        }

        if (!task_splitted && DiffBetweenTimestamps(timeOfBroadcast, currentTime) >= workersConnectionWaitingTime) {
            cout << DiffBetweenTimestamps(timeOfBroadcast, currentTime) << endl;
            if (workersStates.empty()) {
                workersConnectionWaitingTime *= 2;
            } else {
                tasksCount = workersStates.size();
                task_splitted = true;
                size_t i = 0;
                for (auto& [worker, state] : workersStates) {
                    SetTcpConnectionWithWorkek(worker);
                    AssignAndSendTaskToWorker(worker, i++);
                }
            }
        }

        if (task_splitted && DiffBetweenTimestamps(lastHealthCheckTime, currentTime) > healthCheckPeriod) {
            lastHealthCheckTime = currentTime;
            vector<string> aliveWorkersAddresses;
            vector<size_t> tasksToReassign;
            for (auto& [workerAddress, state] : workersStates) {
                if (DiffBetweenTimestamps(state.lastHeartbeat, currentTime) > healthCheckPeriod) {
                    cerr << "Dead: " << workerAddress << std::endl;
                    tasksToReassign.insert(
                        tasksToReassign.begin(),
                        make_move_iterator(state.tasksIds.begin()),
                        make_move_iterator(state.tasksIds.end())
                    );
                    state.tasksIds.clear();
                } else {
                    cerr << "Alive: " << workerAddress << std::endl;
                    aliveWorkersAddresses.push_back(workerAddress);
                }
            }
            if (aliveWorkersAddresses.empty()) {
                cerr << "No alive workers :(" << endl;
            } else {
                size_t newTasksPerWorker = tasksToReassign.size() / aliveWorkersAddresses.size();
                size_t indexOfWorker = 0;
                for (size_t i = 0; i < tasksToReassign.size(); ++i) {
                    if (i > 0 && i % newTasksPerWorker == 0) {
                        ++indexOfWorker;
                    }
                    AssignAndSendTaskToWorker(aliveWorkersAddresses[indexOfWorker], tasksToReassign[i]);
                }
            }
        }
    }

    close(udp_sock);
    close(tcp_sock);
    close(epoll_fd);
}
