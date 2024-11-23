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
#include <algorithm>
#include <cmath>

#include "task.h"

using namespace std;

double func(double x) {
    return -3 * x * x + sin(x) * x - 14;
}

double calcOnInterval(double l, double r, double step) {
    double result = 0;
    while (l < r) {
        double realR = min(r, l + step);
        double segmentLength = realR - l;
        result += func(l + segmentLength / 2) * segmentLength;
        l += step;
    }
    return result;
}

const size_t BUFFER_SIZE = 1024;
const size_t UDP_PORT = 3456;
const size_t TCP_PORT = 3457;
string masterAddress;
int tcpSock;
int udpSock;
struct sockaddr_in tcpAddr;
struct sockaddr_in udpAddr;

void GetBroadcast(unsigned short broadcastPort) {
    int sockFd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char buffer[BUFFER_SIZE];
    ssize_t numBytesReceived;

    if ((sockFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(broadcastPort);

    if (bind(sockFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockFd);
        exit(1);
    }

    cerr << "Listening for broadcast messages on port " << broadcastPort << "..." << endl;

    while (true) {
        numBytesReceived = recvfrom(sockFd, buffer, BUFFER_SIZE - 1, 0, (struct sockaddr*)&addr, &addr_len);
        if (numBytesReceived < 0) {
            perror("recvfrom");
            continue;
        }
        buffer[numBytesReceived] = '\0';
        string message(buffer);
        char senderIp[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, senderIp, INET_ADDRSTRLEN);

        cout << senderIp << " broadcasted message: " << message << endl;
        if (message == "Broadcast to calculate the integral") {
            masterAddress = senderIp;
            break;
        }
    }

    close(sockFd);
}

void SetupTcp() {
    tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpSock < 0) {
        perror("TCP socket creation failed");
        exit(1);
    }

    tcpAddr.sin_family = AF_INET;
    tcpAddr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, masterAddress.data(), &tcpAddr.sin_addr);

    if (bind(tcpSock, (struct sockaddr*)&tcpAddr, sizeof(tcpAddr)) < 0) {
        perror("bind");
        close(tcpSock);
        exit(1);
    }

    if (listen(tcpSock, 1) < 0) {
        perror("listen");
        close(tcpSock);
        exit(1);
    }

    int flags = fcntl(tcpSock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        exit(1);
    }
    fcntl(tcpSock, F_SETFL, flags | O_NONBLOCK);
}

void SetupUdp() {
    udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSock < 0) {
        perror("socket");
        exit(1);
    }

    udpAddr.sin_family = AF_INET;
    udpAddr.sin_port = htons(UDP_PORT);
    inet_pton(AF_INET, masterAddress.data(), &udpAddr.sin_addr);
}

int main() {
    GetBroadcast(4567);

    // сначала сделать SetupTcp и начать принимать соединения, чтобы не пропустить подключение.
    SetupUdp();
    while (true) {
        // прочитать из tcp буфера, если что-то пришло то начать выполнять задачу с фьючой.

        // если прошло кратное таймауту время, отправить heartbeat по UDP.
    }
}
