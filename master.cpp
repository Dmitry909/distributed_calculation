#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <unordered_set>
#include <map>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <chrono>
#include <vector>
#include <cassert>

using namespace std;

struct Task {
    size_t taskId;
    double l;
    double r;
};

struct TaskResult {
    size_t taskId;
    double result;
};

using TimeType = chrono::time_point<chrono::high_resolution_clock>;
struct WorkerState {
    unordered_set<size_t> tasksIds;
    TimeType lastHeartbeat;
    int tcpSock;
    struct sockaddr_in addressForTcp;
};

size_t tasksCount = 0;
double l;
double r;
using Address = pair<string, uint16_t>;
map<Address, WorkerState> workersStates;
const size_t PORT = 12345;
const size_t MAX_EVENTS = 100;
const size_t BUFFER_SIZE = 1024; // а больше не надо? что будет если не хватит на одно сообщение?
char buffer[BUFFER_SIZE];
const double healthCheckPeriod = 10; // seconds
double workersConnectionWaitingTime = healthCheckPeriod / 2;
TimeType lastHealthCheckTime;
const int EPOLL_WAIT_TIMEOUT_MS = 500;
bool taskSplitted = false;
unordered_set<size_t> completedTasks;
double totalResult = 0;
struct epoll_event ev, events[MAX_EVENTS];
int epollFd;
int udp_sock;

void CloseAllSocks() {
    close(udp_sock);
    close(epollFd);
    for (const auto& [_, state] : workersStates) {
        close(state.tcpSock);
    }
}

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

bool SetTcpConnectionWithWorker(const Address& workerAddress) {
    auto& workerState = workersStates[workerAddress];
    workerState.tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    if (workerState.tcpSock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    workerState.addressForTcp.sin_family = AF_INET;
    workerState.addressForTcp.sin_addr.s_addr = inet_addr(workerAddress.first.data());
    workerState.addressForTcp.sin_port = htons(workerAddress.second);

    if (connect(workerState.tcpSock, (struct sockaddr*)&workerState.addressForTcp, sizeof(workerState.addressForTcp)) < 0) {
        workerState.lastHeartbeat = chrono::time_point<chrono::high_resolution_clock>();
        // close(workerState.tcpSock);
        return false;
    }

    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = workerState.tcpSock;
    if (epoll_ctl(epollFd, EPOLL_CTL_MOD, workerState.tcpSock, &ev) == -1) {
        perror("epoll_ctl: mod");
        close(workerState.tcpSock);
        exit(1);
    }

    return true;
}

Task GetSubInterval(size_t i) {
    if (tasksCount == 0) {
        return {0, 0};
    }
    return {i, l + (r - l) / tasksCount * i, l + (r - l) / tasksCount * (i + 1)};
}

void AssignAndSendTaskToWorker(const Address& workerAddress, size_t taskId) {
    auto& workerState = workersStates[workerAddress];
    workerState.tasksIds.insert(taskId);
    
    auto task = GetSubInterval(taskId);
    cerr << "\tCalling send..." << endl;
    if (send(workerState.tcpSock, &task, sizeof(task), 0) < 0) {
        perror("Send failed");
        // exit(1);
    }
}

double DiffBetweenTimestamps(const TimeType& a, const TimeType& b) {
    assert(a <= b);
    return (chrono::duration<double>(b - a)).count();
}

void TryToCreateSubtasks(const TimeType& timeOfBroadcast, const TimeType& currentTime) {
    cout << "TryToCreateSubtasks: diff: " << DiffBetweenTimestamps(timeOfBroadcast, currentTime) << endl;
    cout << "\tworkersStates.size(): " << workersStates.size() << endl;
    if (workersStates.empty()) {
        workersConnectionWaitingTime += 10;
        cerr << "0 workers available, increased timeout to " << workersConnectionWaitingTime << " seconds" << endl;
    } else {
        tasksCount = workersStates.size();
        taskSplitted = true;
        size_t i = 0;
        for (auto& [worker, state] : workersStates) {
            if (SetTcpConnectionWithWorker(worker)) {
                cerr << "\tSetTcpConnectionWithWorker finished" << endl;
                AssignAndSendTaskToWorker(worker, i++);
                cerr << "\tAssignAndSendTaskToWorker finished" << endl;
            }
        }
    }
}

void DoHealthCheck() {
    auto currentTime = chrono::high_resolution_clock::now();
    lastHealthCheckTime = currentTime;
    vector<Address> aliveWorkersAddresses;
    vector<size_t> tasksToReassign;
    for (auto& [workerAddress, state] : workersStates) {
        if (DiffBetweenTimestamps(state.lastHeartbeat, currentTime) > healthCheckPeriod) {
            cerr << "Dead: " << workerAddress.first << ":" << workerAddress.second << std::endl;
            tasksToReassign.insert(
                tasksToReassign.begin(),
                make_move_iterator(state.tasksIds.begin()),
                make_move_iterator(state.tasksIds.end())
            );
            state.tasksIds.clear();
        } else {
            cerr << "Alive: " << workerAddress.first << ":" << workerAddress.second << std::endl;
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

void CompleteTask(TaskResult result) {
    if (completedTasks.insert(result.taskId).second) {
        totalResult += result.result;
        if (completedTasks.size() == tasksCount) {
            cout << "Result: " << totalResult << endl;
            CloseAllSocks();
            exit(0);
        }
    }
}

int main() {
    cin >> l >> r;
    if (l > r) {
        cerr << "l must be not more than r" << endl;
        exit(1);
    }

    // TODO get workersStates using broadcast. Далее в epoll-е еще придется ждать таймаут, чтобы пришло определенное число воркеров.
    // workersStates = {"1.2.3.4:5555", "5.6.7.8:5555", "9.10.11.12:5555"};
    const auto timeOfBroadcast = chrono::high_resolution_clock::now();

    udp_sock = CreateAndBindUdp(PORT);
    SetNonblocking(udp_sock);

    epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("epoll_create1");
        exit(1);
    }
    
    ev.events = EPOLLIN;
    ev.data.fd = udp_sock;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, udp_sock, &ev) == -1) {
        perror("epoll_ctl: udp_sock");
        exit(1);
    }

    struct sockaddr_in clientAddr;
    socklen_t client_addr_len = sizeof(clientAddr);
    lastHealthCheckTime = chrono::high_resolution_clock::now();
    while (true) {
        int nEvents = epoll_wait(epollFd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT_MS); // block not more than for EPOLL_WAIT_TIMEOUT_MS ms
        if (nEvents == -1) {
            perror("epoll_wait");
            exit(1);
        }

        const auto currentTime = chrono::high_resolution_clock::now();
        for (int i = 0; i < nEvents; ++i) {
            if (events[i].data.fd == udp_sock) {
                memset(buffer, 0, BUFFER_SIZE);
                recvfrom(udp_sock, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&clientAddr, &client_addr_len);

                Address workerAddress = {inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port)};
                cout << "Received heartbeat from client " << workerAddress.first << ":" << workerAddress.second << endl;
                if (!workersStates.contains(workerAddress)) {
                    bool success = SetTcpConnectionWithWorker(workerAddress);
                    cerr << "tried to set tcp connection with worker, result: " << success << endl;
                }
                workersStates[workerAddress].lastHeartbeat = currentTime;
            } else {
                int sockFd = events[i].data.fd;
                TaskResult taskResult;
                if (getpeername(sockFd, (struct sockaddr*)&clientAddr, &client_addr_len) == -1) {
                    perror("getpeername");
                }
                Address workerAddress = {inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port)};
                int len = read(sockFd, &taskResult, sizeof(taskResult));
                if (len > 0) {
                    CompleteTask(taskResult);
                    workersStates[workerAddress].tasksIds.clear();
                }
                // else {
                //     if (epoll_ctl(epollFd, EPOLL_CTL_DEL, sockFd, NULL) == -1) {
                //         perror("epoll_ctl: DEL");
                //     }
                //     close(sockFd);
                //     cout << "Closed connection with client" << endl;
                // }
            }
        }

        if (!taskSplitted && DiffBetweenTimestamps(timeOfBroadcast, currentTime) >= workersConnectionWaitingTime) {
            TryToCreateSubtasks(timeOfBroadcast, currentTime);
            cerr << "TryToCreateSubtasks finished" << endl;
        }

        if (taskSplitted && DiffBetweenTimestamps(lastHealthCheckTime, currentTime) > healthCheckPeriod) {
            DoHealthCheck();
        }
    }

    CloseAllSocks();
}
