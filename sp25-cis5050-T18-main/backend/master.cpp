// 127.0.0.1:5050  # Backend Coordinator/Master (assume it never fails)
// Implements partitioning, primary tracking, status tracking, fault detection, and lookup
#include <iostream>
#include <string>
#include <cstdint>
#include <sstream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <cerrno>
#include <atomic>
#include <csignal>

constexpr int MASTER_PORT = 5050;
constexpr int REPLICATION_FACTOR = 3;
static std::atomic<bool> running {true};
std::vector<std::pair<std::string, int>> node_addresses;  // {ip, port} pairs learned from config.txt
std::vector<bool> node_alive;  // status of all nodes
std::vector<int> primary_map;       // primary_map[shard_i] = primary_index (at most 1 primary at any moment)
int num_nodes;
int num_shards;
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;  // for hashing
static constexpr uint64_t FNV_PRIME        = 0x100000001b3ULL;   // for hashing
std::mutex coord_mutex;

// Since master node never fails, it only shuts down at the end of session when we hit Ctrl+C
void handle_shutdown(int) { running = false; std::cout << "[Coordinator] Shutdown\n"; exit(0);}

// Trim leading/trailing " \t\r\n"
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a==std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Load config file: ip:port (ignore comments after '#')
void load_config(const std::string &filename) {
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        auto pos = line.find('#');
        if (pos != std::string::npos) line = trim(line.substr(0, pos));
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string ip = trim(line.substr(0, colon));
        int port = std::stoi(trim(line.substr(colon + 1)));
        node_addresses.emplace_back(ip, port);
    }
    num_nodes = node_addresses.size();
    num_shards = num_nodes / REPLICATION_FACTOR;
}

// Hash row key (as evenly as possible)
static int get_shard(const std::string &key) {
    uint64_t h = FNV_OFFSET_BASIS;
    for (unsigned char c : key) {
        h ^= c;
        h *= FNV_PRIME;
    }
    return static_cast<int>(h % num_shards);
}

// Try to connect to node i, return status
bool check_node(int i) {
    auto [ip, port] = node_addresses[i];
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    bool alive = false;
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
        char buffer[32]; recv(sock, buffer, 31, 0);
        std::string check = "CHECK\r\n";
        send(sock, check.c_str(), check.size(), 0);
        char buf[10]; recv(sock, buf, 9, 0);
        if (buf[0] == '+') {
            alive = true;
        }
        std::string quit = "QUIT\r\n";
        send(sock, quit.c_str(), quit.size(), 0);
    }
    close(sock);
    return alive;
}

// Scan shard i and re‐elect primary if necessary
void refresh(int shard_i) {
    std::lock_guard<std::mutex> lk(coord_mutex);
    int base = shard_i * REPLICATION_FACTOR;
    // update aliveness for this shard’s nodes
    for (int j = 0; j < REPLICATION_FACTOR; ++j) {
        int idx = base + j;
        node_alive[idx] = check_node(idx);
    }
    int cur_prim  = primary_map[shard_i];
    // if current primary died, pick another alive replica
    if (cur_prim == -1 || !node_alive[cur_prim]) {
        int new_prim = -1;
        for (int j = 0; j < REPLICATION_FACTOR; ++j) {
            int idx = base + j;
            if (node_alive[idx]) { new_prim = idx; break; }
        }
        primary_map[shard_i] = new_prim;
    }
}

// Handle client connection
void handle_client(int client_fd) {
    char buffer[1024];
    std::string line;
    while (running) {
        ssize_t n = recv(client_fd, buffer, 1023, 0);
        buffer[n] = '\0';
        std::istringstream iss(buffer);
        while (std::getline(iss, line)) {
            line = trim(line);
            if (line.empty()) continue;
            std::istringstream cmd(line);
            std::string op;
            cmd >> op;
            if (op == "ASK") {  // lookup for a row key
                std::string row;
                cmd >> row;
                std::cout << "[Master] client" << client_fd << " lookup for: " << row << std::endl;
                int shard = get_shard(row);
                refresh(shard);
                int prim;
                std::lock_guard<std::mutex> lk(coord_mutex);
                prim = primary_map[shard];
                if (prim == -1) {  // all dead for that group
                    std::string resp = "-ERR ALL DEAD\r\n";
                    send(client_fd, resp.c_str(), resp.size(), 0);
                    std::cout << "[Master] tablets of "<< row << " (shard" << shard << ") all dead" << std::endl;
                } else {
                    auto [ip, port] = node_addresses[prim];
                    std::ostringstream os;
                    os << "+OK REDIRECT " << ip << ":" << port << "\r\n";
                    send(client_fd, os.str().c_str(), os.str().size(), 0);
                    std::cout << "[Master] "<< row << " should go to " << ip << ":" << port << std::endl;
                }
            }
            else if (op == "LIST_NODES") {  // know the primary and status of all nodes
                std::ostringstream os;
                std::cout << "[Master] client" << client_fd << " wants to list status of all nodes" << std::endl;
                os << "+OK ";
                for (int i = 0; i < num_shards; ++i) refresh(i);
                std::lock_guard<std::mutex> lk(coord_mutex);
                for (int s = 0; s < num_shards; ++s) {
                    // Primary
                    os << "Shard" << s << ": Primary: " << primary_map[s];
                    // Alive list
                    os << " Alive:";
                    bool any_alive = false;
                    int base = s * REPLICATION_FACTOR;
                    for (int j = 0; j < REPLICATION_FACTOR; ++j) {
                        int idx = base + j;
                        if (node_alive[idx]) {
                            os << " " << idx;
                            any_alive = true;
                        }
                    }
                    if (!any_alive) {
                        os << " -1";
                    }
                    // Dead list
                    os << " Dead:";
                    bool any_dead = false;
                    for (int j = 0; j < REPLICATION_FACTOR; ++j) {
                        int idx = base + j;
                        if (!node_alive[idx]) {
                            os << " " << idx;
                            any_dead = true;
                        }
                    }
                    if (!any_dead) {
                        os << " -1";
                    }
                    if (s + 1 < num_shards) os << " ";
                }
                os << "\r\n";
                auto resp = os.str();
                send(client_fd, resp.c_str(), resp.size(), 0);
                std::cout << "[Master] "<< resp << std::endl;
            }
            else if (op == "ASK_PRIMARY") {
                std::string shard_i;
                cmd >> shard_i;
                int shard = std::stoi(shard_i);
                std::cout << "[Master] client" << client_fd << " asks for primary of shard" << shard_i << std::endl;
                int prim;
                refresh(shard);
                std::lock_guard<std::mutex> lk(coord_mutex);
                prim = primary_map[shard];
                if (prim == -1) {  // all dead for that group
                    std::ostringstream os;
                    std::string resp = "+OK -1\r\n";
                    send(client_fd, resp.c_str(), resp.size(), 0);
                    std::cout << "[Master] Shard" << shard_i << " all dead" <<  std::endl;
                } else {
                    std::ostringstream os;
                    os << "+OK "<< std::to_string(prim) << "\r\n";
                    send(client_fd, os.str().c_str(), os.str().size(), 0);
                    std::cout << "[Master] Primary for shard" << shard_i << " now is: " << std::to_string(prim) <<  std::endl;
                }
            } 
            else if (op == "QUIT") {
                close(client_fd);
                std::cout << "[Master] client" << client_fd << " quit" << std::endl;
                return;
            }
        }
    }
    close(client_fd);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr<<"Usage: ./master <config_file>\n";
        return 1;
    }
    std::string cfg = argv[1];
    load_config(cfg);
    primary_map.assign(num_shards, -1);
    node_alive.assign(num_nodes, true); // assume all nodes are alive at the start
    // init primaries (by default)
    for (int s = 0; s < num_shards; ++s) primary_map[s] = s * REPLICATION_FACTOR;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    // run coordinator
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MASTER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (::bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    if (listen(sockfd, 100) < 0) {
        perror("listen failed");
        exit(1);
    } 
    std::cout << "[Master] Listening on port " << MASTER_PORT << std::endl;
    while (running) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int fd = accept(sockfd, (sockaddr*)&cli, &len);
        if (fd < 0) continue;
        std::cout << "[Master] client" << fd << " connected" << std::endl;              
        std::string resp = "+OK Master ready\r\n";
        send(fd, resp.c_str(), resp.size(), 0);
        std::thread(handle_client, fd).detach();
    }
    return 0;
}
