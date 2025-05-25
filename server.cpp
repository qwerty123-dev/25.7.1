// ============================
//        SERVER CODE
// ============================

#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>
#include <mysql/mysql.h>

#define SERVER_UDP_PORT 12345

struct ClientInfo {
    sockaddr_in addr;
};

std::map<std::string, ClientInfo> clients;
std::mutex clients_mutex;

MYSQL* conn;

std::string escapeString(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '\'') output += "''";
        else output += c;
    }
    return output;
}

bool connectToDB() {
    conn = mysql_init(nullptr);
    if (!conn) {
        std::cerr << "MySQL init failed\n";
        return false;
    }
    if (!mysql_real_connect(conn, "localhost", "root", "root", nullptr, 0, nullptr, 0)) {
        std::cerr << "MySQL connection failed: " << mysql_error(conn) << "\n";
        return false;
    }
    mysql_query(conn, "CREATE DATABASE IF NOT EXISTS chatdb");
    mysql_select_db(conn, "chatdb");

    const char* createUsers = "CREATE TABLE IF NOT EXISTS users ("
                              "username VARCHAR(100) PRIMARY KEY,"
                              "password VARCHAR(100))";

    const char* createMessages = "CREATE TABLE IF NOT EXISTS messages ("
                                 "id INT AUTO_INCREMENT PRIMARY KEY,"
                                 "sender VARCHAR(100),"
                                 "receiver VARCHAR(100),"
                                 "text TEXT)";

    mysql_query(conn, createUsers);
    mysql_query(conn, createMessages);

    return true;
}

void saveMessageToDB(const std::string& sender, const std::string& receiver, const std::string& text) {
    std::string query = "INSERT INTO messages(sender, receiver, text) VALUES('" +
                        escapeString(sender) + "', '" + escapeString(receiver) + "', '" + escapeString(text) + "')";
    if (mysql_query(conn, query.c_str()) != 0) {
        std::cerr << "DB insert error: " << mysql_error(conn) << "\n";
    }
}

bool validateLogin(const std::string& user, const std::string& pass) {
    std::string query = "SELECT * FROM users WHERE username='" + escapeString(user) + "' AND password='" + escapeString(pass) + "'";
    if (mysql_query(conn, query.c_str()) != 0) {
        std::cerr << "DB select error: " << mysql_error(conn) << "\n";
        return false;
    }
    MYSQL_RES* res = mysql_store_result(conn);
    bool valid = mysql_num_rows(res) > 0;
    mysql_free_result(res);
    return valid;
}

bool registerUser(const std::string& user, const std::string& pass) {
    std::string query = "INSERT INTO users(username, password) VALUES('" + escapeString(user) + "', '" + escapeString(pass) + "')";
    if (mysql_query(conn, query.c_str()) != 0) {
        std::cerr << "DB insert error: " << mysql_error(conn) << "\n";
        return false;
    }
    return true;
}

void handlePacket(int sockfd, std::string data, sockaddr_in client_addr) {
    if (data.rfind("REGISTER:", 0) == 0) {
        auto body = data.substr(9);
        auto sep = body.find(':');
        if (sep == std::string::npos) return;
        std::string user = body.substr(0, sep);
        std::string pass = body.substr(sep + 1);

        std::string reply = registerUser(user, pass) ? "REGISTER_OK" : "REGISTER_FAIL";
        sendto(sockfd, reply.c_str(), reply.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
    }
    else if (data.rfind("LOGIN:", 0) == 0) {
        auto body = data.substr(6);
        auto sep = body.find(':');
        if (sep == std::string::npos) return;
        std::string user = body.substr(0, sep);
        std::string pass = body.substr(sep + 1);

        if (validateLogin(user, pass)) {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients[user] = ClientInfo{client_addr};
            std::string reply = "LOGIN_OK";
            sendto(sockfd, reply.c_str(), reply.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        } else {
            std::string reply = "LOGIN_FAIL";
            sendto(sockfd, reply.c_str(), reply.size(), 0, (sockaddr*)&client_addr, sizeof(client_addr));
        }
    }
    else if (data.rfind("MSG:", 0) == 0) {
        size_t pos1 = data.find(':', 4);
        if (pos1 == std::string::npos) return;
        std::string receiver = data.substr(4, pos1 - 4);
        std::string message = data.substr(pos1 + 1);

        std::string sender;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& pair : clients) {
                if (pair.second.addr.sin_addr.s_addr == client_addr.sin_addr.s_addr &&
                    pair.second.addr.sin_port == client_addr.sin_port) {
                    sender = pair.first;
                    break;
                }
            }
        }
        if (sender.empty()) return;
        saveMessageToDB(sender, receiver, message);

        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients.count(receiver)) {
            std::string out = "FROM:" + sender + ":" + message;
            sendto(sockfd, out.c_str(), out.size(), 0, (sockaddr*)&clients[receiver].addr, sizeof(sockaddr_in));
        }
    }
}

int main() {
    if (!connectToDB()) return 1;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_UDP_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    std::cout << "UDP Server started on port " << SERVER_UDP_PORT << "\n";

    while (true) {
        char buffer[1024];
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        ssize_t len = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0, (sockaddr*)&clientAddr, &clientLen);
        if (len < 0) continue;
        buffer[len] = '\0';
        std::string data(buffer);
        std::thread(handlePacket, sockfd, data, clientAddr).detach();
    }

    close(sockfd);
    mysql_close(conn);
    return 0;
}
