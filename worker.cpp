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
string masterAddress;

void GetBroadcastMessage(unsigned short broadcastPort) {
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

int main() {
    GetBroadcastMessage(4567);
}
