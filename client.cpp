#include <iostream>
#include <string>
#include <thread>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>

#define SERVER_IP "192.168.100.114"
#define SERVER_UDP_PORT 9000

int udpSock;
sockaddr_in serverAddr;

void listenServer() {
    char buffer[1024];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);

    while (true) {
        ssize_t len = recvfrom(udpSock, buffer, sizeof(buffer)-1, 0, (sockaddr*)&from, &fromLen);
        if (len > 0) {
            buffer[len] = '\0';
            std::cout << "\n[Server]: " << buffer << "\n> ";
            std::cout.flush();
        }
    }
}

int main() {
    udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSock < 0) {
        perror("socket");
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_UDP_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) <= 0) {
        std::cerr << "Invalid server IP address\n";
        return 1;
    }

    std::thread listener(listenServer);
    listener.detach();

    std::cout << "Commands:\n"
              << "register <username> <password> - Register new user\n"
              << "login <username> <password>    - Login\n"
              << "send <user> <message>          - Send message to user\n"
              << "exit                          - Quit\n";

    while (true) {
        std::string line;
        std::cout << "> ";
        std::getline(std::cin, line);

        if (line.rfind("register ", 0) == 0) {
            // register username password
            size_t pos1 = line.find(' ', 9);
            if (pos1 == std::string::npos) {
                std::cout << "Usage: register <username> <password>\n";
                continue;
            }
            std::string username = line.substr(9, pos1 - 9);
            std::string password = line.substr(pos1 + 1);
            std::string msg = "REGISTER:" + username + ":" + password;
            sendto(udpSock, msg.c_str(), msg.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
        }
        else if (line.rfind("login ", 0) == 0) {
            // login username password
            size_t pos1 = line.find(' ', 6);
            if (pos1 == std::string::npos) {
                std::cout << "Usage: login <username> <password>\n";
                continue;
            }
            std::string username = line.substr(6, pos1 - 6);
            std::string password = line.substr(pos1 + 1);
            std::string msg = "LOGIN:" + username + ":" + password;
            sendto(udpSock, msg.c_str(), msg.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
        }
        else if (line.rfind("send ", 0) == 0) {
            size_t pos1 = line.find(' ', 5);
            if (pos1 == std::string::npos) {
                std::cout << "Usage: send <user> <message>\n";
                continue;
            }
            std::string receiver = line.substr(5, pos1 - 5);
            std::string message = line.substr(pos1 + 1);
            std::string msg = "MSG:" + receiver + ":" + message;
            sendto(udpSock, msg.c_str(), msg.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
        }
        else if (line == "exit") {
            break;
        }
        else {
            std::cout << "Unknown command\n";
        }
    }

    close(udpSock);
    return 0;
}
