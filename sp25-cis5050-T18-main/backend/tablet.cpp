// Tablet server for PennCloud KV-store
// Implements primary-based replication, fault tolerance, checkpointing, logging, and recovery
#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <chrono>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <poll.h>
#include <fcntl.h>
#include <atomic>
#include <unordered_set>

namespace fs = std::filesystem;
constexpr int MASTER_PORT = 5050;
constexpr int REPLICATION_FACTOR = 3;   // three replicas per shard
std::mutex mutex;  // automatically release when out of scope
std::unordered_map<std::string, std::unordered_map<std::string,std::string>> kvstore;   // cache for current subtablet in memory
int self_index;       // this tablet's index
int shard_i;
int num_nodes;
int num_shards;
static constexpr int num_tablets = 3;   // three smaller tablets for this node
std::string log_file, checkpoint_file;  // file name prefixes
std::vector<std::pair<std::string,int>> nodes;  // {ip, port} pairs
std::atomic<bool> running {true};
bool dead {false};  // to mimic dead
static constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;  // for hashing
static constexpr uint64_t FNV_PRIME        = 0x100000001b3ULL;   // for hashing
int current_tablet = 0;
std::unordered_map<std::string, std::unordered_set<std::string>> all_row_col;  // keep all row/col keys for this node (since they're much smaller than contents, they fit in memory)

void handle_shutdown(int) { running = false; std::cout << "[Tablet" << self_index << "] Shutdown\n"; exit(0);}

int get_tablet(const std::string &key) {  // get (sub)tablet for a row key
    uint64_t h = FNV_OFFSET_BASIS;
    for (unsigned char c : key) {
        h ^= c;
        h *= FNV_PRIME;
    }
    int tablet = static_cast<int>((h / num_shards) % num_tablets);
    std::cout << "[Tablet" << self_index << "] " << key << " in subtablet: "  << tablet <<  std::endl;
    return tablet;
}

// Trim leading/trailing " \t\r\n"
static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a==std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Send all bytes in s
static void send_all(int fd, const std::string &s) {
    const char *buf = s.data(); size_t n = s.size();
    size_t total = 0;
    while (total < n) {
        ssize_t w = send(fd, buf + total, n - total, 0);     
        total += (size_t)w;
    }
}

// Recv exactly n bytes
static std::string recv_all(int fd, size_t n) {
    std::string buf;
    constexpr size_t CHUNK = 1024*1024; // 1MB
    std::vector<char> tmp(CHUNK);
    while (buf.size() < n) {
        size_t to_read = std::min(CHUNK, n - buf.size());
        ssize_t r = recv(fd, tmp.data(), to_read, 0);
        buf.append(tmp.data(), (size_t)r);
    }
    return buf;
}

// Fetch current primary index for this shard from master
int query_primary() {
    int sock = socket(AF_INET,SOCK_STREAM,0);
    sockaddr_in m{}; m.sin_family=AF_INET; m.sin_port=htons(MASTER_PORT);
    inet_pton(AF_INET,"127.0.0.1",&m.sin_addr);
    connect(sock,(sockaddr*)&m,sizeof(m));
    char buf_[32];
    recv(sock,buf_,sizeof(buf_)-1,0); // expect "+OK Master ready\r\n"
    std::ostringstream os;
    os << "ASK_PRIMARY " << shard_i << "\r\n";
    std::string cmd = os.str(); send_all(sock,cmd);
    int n=recv(sock,buf_,sizeof(buf_)-1,0);
    buf_[n] = '\0';
    std::string resp(buf_);
    send_all(sock, "QUIT\r\n");
    close(sock);
    auto pos=resp.find_first_of(' ');
    std::string primary_i=trim(resp.substr(pos+1));
    std::cout << "[Tablet" << self_index << "] Primary for shard" << shard_i << " now is: "  << primary_i <<  std::endl;
    return stoi(primary_i);
}

// Write the current kvstore (for current subtablet) to the checkpoint file (if log file is non-empty) and clear the on-disk log
void checkpoint(int tablet) {
    std::ifstream logf(log_file + std::to_string(tablet), std::ios::binary | std::ios::ate);
    auto s = logf.tellg();
    logf.close();
    if (s > 0) {  // if on–disk log for subtablet is empty, nothing’s changed → skip
        uint32_t prev_version = 0;
        {
            std::ifstream cp_in(checkpoint_file + std::to_string(tablet), std::ios::binary | std::ios::ate);
            if (cp_in) {
                auto len = cp_in.tellg();
                if (len >= static_cast<std::streamoff>(sizeof(prev_version))) {
                    cp_in.seekg(0, std::ios::beg);
                    cp_in.read(reinterpret_cast<char*>(&prev_version), sizeof(prev_version));
                }
            }
        }
        uint32_t new_version = prev_version + 1;  // get new version number
        std::ofstream cp(checkpoint_file + std::to_string(tablet), std::ios::binary | std::ios::trunc);
        cp.write(reinterpret_cast<char*>(&new_version), sizeof(new_version));
        for (auto& [row, cols] : kvstore) {
            for (auto& [col, val] : cols) {
                uint32_t rl = row.size(), cl = col.size(), vl = val.size();
                cp.write(reinterpret_cast<char*>(&rl), sizeof(rl));
                cp.write(row.data(), rl);
                cp.write(reinterpret_cast<char*>(&cl), sizeof(cl));
                cp.write(col.data(), cl);
                cp.write(reinterpret_cast<char*>(&vl), sizeof(vl));
                cp.write(val.data(), vl);
            }
        }
        cp.close();
        std::ofstream lf(log_file + std::to_string(tablet), std::ios::binary | std::ios::trunc);  // clear the log
        lf.close();
        std::cout << "[Tablet" << self_index << "] Finished checkpoint v" << new_version << " for subtablet" << current_tablet << "\n";
    } 
}

// Load the checkpoint file of a subtablet back into kvstore in memory
void load_back(int tablet) {
    kvstore.clear();   // must clear old data
    current_tablet = tablet;  // must also update current tablet
    std::ifstream cp(checkpoint_file + std::to_string(tablet), std::ios::binary | std::ios::ate);
    if (cp.tellg() == 0) {  // empty checkpoint → nothing to load back
        cp.close();
        std::cout << "[Tablet" << self_index << "] No need to load back checkpoint (empty) for subtablet" << tablet << std::endl;
        return;
    }
    // skip the 4-byte version header
    cp.seekg(sizeof(uint32_t), std::ios::beg);
    // if there's nothing else in the file, we're done
    if (cp.peek() == EOF) {
        cp.close();
        std::cout << "[Tablet" << self_index << "] No need to load back checkpoint (empty) for subtablet" << tablet << std::endl;
        return;
    }
    // now read all the triples
    while (cp.peek() != EOF) {
        uint32_t rl, cl, vl;
        cp.read(reinterpret_cast<char*>(&rl), sizeof(rl));
        std::string row(rl, '\0');
        cp.read(&row[0], rl);
        cp.read(reinterpret_cast<char*>(&cl), sizeof(cl));
        std::string col(cl, '\0');
        cp.read(&col[0], cl);
        cp.read(reinterpret_cast<char*>(&vl), sizeof(vl));
        std::string val(vl, '\0');
        cp.read(&val[0], vl);
        kvstore[row][col] = val;
    }
    cp.close();
}

// Append one log entry to disk and update counts (num of entries); must be for current tablet
void append_log(const std::string &entry) {
    // open for update or create
    std::fstream lf(log_file + std::to_string(current_tablet), std::ios::binary | std::ios::in | std::ios::out);
    uint32_t entry_count = 0;
    lf.seekg(0, std::ios::end);
    std::streamoff sz = lf.tellg();
    if (sz < static_cast<std::streamoff>(sizeof(entry_count))) {
        // empty log file: count = 1
        lf.close();
        std::ofstream of(log_file + std::to_string(current_tablet), std::ios::binary | std::ios::trunc);
        entry_count = 1;
        of.write(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
        uint32_t L = (uint32_t)entry.size();
        of.write(reinterpret_cast<char*>(&L), sizeof(L));
        of.write(entry.data(), L);
        return;
    }
    // have existing entries: bump count
    lf.seekg(0);
    lf.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
    entry_count++;
    lf.seekp(0);
    lf.write(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
    // append new entry
    lf.seekp(0, std::ios::end);
    uint32_t L = (uint32_t)entry.size();
    lf.write(reinterpret_cast<char*>(&L), sizeof(L));
    lf.write(entry.data(), L);
}

// Replay all PUT and successful CPUT and DELETE from the on-disk log into kvstore for a subtablet
void replay_log(int tablet) {
    std::ifstream lf(log_file + std::to_string(tablet), std::ios::binary);
    if (!lf) return;
    uint32_t entry_count = 0;
    lf.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
    if (!lf) return;
    for (uint32_t i = 0; i < entry_count; ++i) {
        // Read length prefix
        uint32_t L;
        if (!lf.read(reinterpret_cast<char*>(&L), sizeof(L))) break;
        // Read exactly L bytes for entry
        std::string entry(L, '\0');
        lf.read(&entry[0], L);
        // Find the three spaces that delimit cmd, row, col
        size_t p1 = entry.find(' ');
        size_t p2 = entry.find(' ', p1 + 1);
        size_t p3 = entry.find(' ', p2 + 1);
        std::string cmd = entry.substr(0, p1);
        std::string row = entry.substr(p1 + 1, p2 - p1 - 1);
        std::string col = entry.substr(p2 + 1, p3 - p2 - 1);
        if (cmd == "PUT") {
            std::string payload = entry.substr(p3 + 1, L - p3 - 1);
            kvstore[row][col] = payload;
            std::cout << "[Tablet" << self_index << "] Replayed PUT/CPUT " << row << " " << col << " with " << payload.size() << " bytes" <<  std::endl;
        } else if (cmd == "DELETE") {
            kvstore[row].erase(col);
            std::cout << "[Tablet" << self_index << "] Replayed DELETE " << row << " " << col << std::endl;
        }
    }
    lf.close();
}

// Return list of the sockets of all alive replicas in this shard
std::vector<int> get_alive_replicas() {
    std::vector<int> fds;
    int base = shard_i * REPLICATION_FACTOR;
    for (int i = 0; i < REPLICATION_FACTOR; ++i) {
        int idx = base + i;
        if (idx == self_index) continue;
        const auto& [ip, port] = nodes[idx];
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
        if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            continue;
        }
        char buf[32];
        recv(sock, buf , sizeof(buf) - 1, 0); // always "+OK Connected\r\n"
        send_all(sock, "CHECK\r\n");
        char buf_[10];
        recv(sock, buf_, sizeof(buf_) - 1, 0);
        if (buf_[0] == '+') {
            fds.push_back(sock);
            std::cout << "[Tablet" << self_index << "] Preparing to replicate to tablet" << idx <<  std::endl;
        } else {
            send_all(sock, "QUIT\r\n");
            close(sock);
        }
    }
    return fds;
}

// Return the latest checkpoint version for a given sub‐tablet (can be 0 if empty) based on the first 4 bytes
int version_of_checkpoint(int tablet) {  // do not need lock since it's only used during recovery
    std::string path = checkpoint_file + std::to_string(tablet);
    std::ifstream cp(path, std::ios::binary | std::ios::ate);
    if (!cp) return 0;
    auto sz = cp.tellg();
    if (sz < static_cast<std::streamoff>(sizeof(uint32_t))) return 0;
    cp.seekg(0, std::ios::beg);
    uint32_t v = 0;
    cp.read(reinterpret_cast<char*>(&v), sizeof(v));
    cp.close();
    return static_cast<int>(v);
}

// Return the log count of a tablet of this node (can be 0 if empty) based on the first 4 bytes
int log_count(int tablet) {
    std::string fname = log_file + std::to_string(tablet);
    std::ifstream lf(fname, std::ios::binary | std::ios::ate);                  
    auto sz = lf.tellg();
    if (sz < static_cast<std::streamoff>(sizeof(uint32_t)))
        return 0;                             
    lf.seekg(0, std::ios::beg);
    uint32_t entry_count = 0;
    lf.read(reinterpret_cast<char*>(&entry_count), sizeof(entry_count));
    lf.close();
    return static_cast<int>(entry_count);
}

// Stay synced with primary's chk + log
void restore_tablet_with_prim(int tablet, int sock) {
    send_all(sock, "CHECKPOINT_VERSION " + std::to_string(tablet) + "\r\n");
    char buffer[128];
    int n = recv(sock, buffer, sizeof(buffer)-1, 0);
    buffer[n] = '\0';  
    int prim_version = (int)strtoull(buffer, nullptr, 10);
    int my_version = version_of_checkpoint(tablet);
    if (prim_version > my_version) {
        send_all(sock, "WANT\r\n");
        n = recv(sock, buffer, sizeof(buffer)-1, 0);
        buffer[n] = '\0';
        int byte_count = (int)strtoull(buffer, nullptr, 10);
        send_all(sock, "READY\r\n");
        std::ofstream cp(checkpoint_file + std::to_string(tablet), std::ios::binary | std::ios::trunc);
        constexpr size_t CHUNK = 1024 * 1024;
        std::vector<char> tmp(CHUNK);
        int remaining = byte_count;
        while (remaining > 0) {
            int to_read = std::min((int)CHUNK, remaining);
            ssize_t r = recv(sock, tmp.data(), to_read, 0);
            cp.write(tmp.data(), r);
            remaining -= r;
        }
        cp.close();
        std::cout << "[Tablet" << self_index << "] Restored checkpoint v" << prim_version << " for subtablet" << tablet << " (" << byte_count << " bytes)\n";
        send_all(sock, "LOG_NUM " + std::to_string(tablet) + "\r\n");
        n = recv(sock, buffer, sizeof(buffer)-1, 0);
        buffer[n] = '\0';  
        int prim_log_count = (int)strtoull(buffer, nullptr, 10);
        if (prim_log_count == 0) {
            std::ofstream lf(log_file + std::to_string(tablet), std::ios::binary | std::ios::trunc);  // clear the log
            lf.close();
            send_all(sock, "NO_NEED\r\n");
            recv(sock, buffer, sizeof(buffer)-1, 0);
            std::cout << "[Tablet" << self_index << "] No need to change log file for subtablet" << tablet <<" \n";
            return;
        }
        send_all(sock, std::to_string(prim_log_count) + "\r\n");
        std::ofstream log(log_file + std::to_string(tablet), std::ios::binary | std::ios::trunc);
        uint32_t cnt = static_cast<uint32_t>(prim_log_count);
        log.write(reinterpret_cast<const char*>(&cnt), sizeof(cnt));
        n = recv(sock, buffer, sizeof(buffer)-1, 0);
        buffer[n] = '\0';
        int bytes_count = (int)strtoull(buffer, nullptr, 10);
        send_all(sock, "READY\r\n");
        constexpr size_t chunk = 1024 * 1024;
        std::vector<char> temp(chunk);
        int remain = bytes_count;
        while (remain > 0) {
            int toread = std::min((int)chunk, remain);
            ssize_t r_ = recv(sock, temp.data(), toread, 0);
            log.write(temp.data(), r_);
            remain -= (int)r_;
        }
        log.close();
        std::cout << "[Tablet" << self_index << "] Restored log file for subtablet" << tablet << " (" << bytes_count << " bytes)\n";
    } else {  // must be equal (prim_version can never be smaller than mine)
        send_all(sock, "NO_NEED\r\n");
        char ack[32];
        recv(sock, ack, sizeof(ack)-1, 0);
        std::cout << "[Tablet" << self_index << "] No need to change chk file for subtablet" << tablet << "\n";
        send_all(sock, "LOG_NUM " + std::to_string(tablet) + "\r\n");
        n = recv(sock, buffer, sizeof(buffer)-1, 0);
        std::cout << n << "\n";
        buffer[n] = '\0';
        int prim_log_count = (int)strtoull(buffer, nullptr, 10);
        int my_log_count = log_count(tablet);
        if (prim_log_count == my_log_count) {
            send_all(sock, "NO_NEED\r\n");
            std::cout << "[Tablet" << self_index << "] No need to change log file for subtablet" << tablet <<" \n";
            recv(sock, ack, sizeof(ack)-1, 0);
        } else {   // then it must be "prim_log_count > my_log_count" here
            int miss_log_count = prim_log_count - my_log_count;
            send_all(sock, std::to_string(miss_log_count) + "\r\n");
            std::fstream log(log_file + std::to_string(tablet), std::ios::binary | std::ios::in | std::ios::out);
            uint32_t cnt = static_cast<uint32_t>(prim_log_count);
            log.seekp(0, std::ios::beg);
            log.write(reinterpret_cast<const char*>(&cnt), sizeof(cnt));
            log.seekp(0, std::ios::end);  // append to end the missing ones
            n = recv(sock, buffer, sizeof(buffer)-1, 0);
            buffer[n] = '\0';
            int bytes_count = (int)strtoull(buffer, nullptr, 10);
            send_all(sock, "READY\r\n");
            constexpr size_t chunk = 1024 * 1024;
            std::vector<char> temp(chunk);
            int remain = bytes_count;
            while (remain > 0) {
                int toread = std::min((int)chunk, remain);
                ssize_t r_ = recv(sock, temp.data(), toread, 0);
                log.write(temp.data(), r_);
                remain -= (int)r_;
            }
            log.close();
            std::cout << "[Tablet" << self_index << "] Restored log file for subtablet" << tablet << " (" << miss_log_count << " missing entries)\n";
        }
    }
}

// Recover
void recover() {
    std::cout << "[Tablet" << self_index << "] Recovering..." <<  std::endl;
    all_row_col.clear();
    int primary = query_primary();   // get primary index
    // SCENARIO 1: I am the primary (but I just recovered, meaning others in this shard all died)
    if (primary == -1 || primary == self_index) {
        std::cout << "[Tablet" << self_index << "] Recover: Now I am the only one alive for this shard\n";
        // rebuild kvstore & schema for each subtablet
        for (int t = num_tablets - 1; t >= 0; --t) {
            // restore this sub-tablet’s data
            load_back(t);
            replay_log(t);
            for (auto& [row, cols] : kvstore) {
                for (auto& [col, val] : cols) {
                    all_row_col[row].insert(col);
                }
            }
            if (t != 0) {
                checkpoint(t);
            }
        }
        current_tablet = 0;  // (set this by default since I am the primary)
        return;
    }
    // SCENARIO 2: someone else is primary
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(nodes[primary].second);
    inet_pton(AF_INET, nodes[primary].first.c_str(), &addr.sin_addr);
    std::string result;
    connect(sock, (sockaddr*)&addr, sizeof(addr));
    char buf[64];
    recv(sock, buf, sizeof(buf)-1, 0);  // "+OK Connected\r\n"
    send_all(sock, "CUR_TAB\r\n");
    char buffer[128];
    int n = recv(sock, buffer, sizeof(buffer)-1, 0);
    buffer[n] = '\0';
    // ask for current tablet in memory of primary to stay synced
    const int cur_tablet = (int)strtoull(buffer, nullptr, 10);
    for (int i = 0; i < num_tablets; ++i) {
        if (i == cur_tablet) continue;
        std::cout << "[Tablet" << self_index << "] Recover: restore subtablet" << i << "\n";
        restore_tablet_with_prim(i, sock);  // restore based on many cases/scenarios optimally, see this helper function above
        load_back(i);
        replay_log(i);
        for (auto& [row, cols] : kvstore) {
            for (auto& [col, val] : cols) {
                all_row_col[row].insert(col);
            }
        }
    }
    // now load_back + replay that current tablet
    std::cout << "[Tablet" << self_index << "] Recover: restore subtablet" << cur_tablet << "\n";
    restore_tablet_with_prim(cur_tablet, sock);
    load_back(cur_tablet);
    replay_log(cur_tablet);
    for (auto& [row, cols] : kvstore) {
        for (auto& [col, val] : cols) {
            all_row_col[row].insert(col);
        }
    }
    current_tablet = cur_tablet;
    send_all(sock, "QUIT\r\n");
    close(sock);
    std::cout << "[Tablet" << self_index << "] Recover: Synced chk+log with primary's" <<  std::endl;
}

void propogate_load(int tab) {
    for (const int& rfd : get_alive_replicas()) {
        send_all(rfd, "LOAD " + std::to_string(tab) + "\r\n");
        char buf[32];
        recv(rfd, buf, sizeof(buf)-1, 0);  // "+OK\r\n"
        send_all(rfd, "QUIT\r\n");
        close(rfd);
        std::cout << "[Tablet" << self_index << "] propogated LOAD to another replica" << std::endl;
    }
    checkpoint(current_tablet);
    load_back(tab);
    std::cout << "[Tablet" << self_index << "] current subtab is " << current_tablet << std::endl;
}

void handle_client(int cfd) {
    char buffer[4096];
    while (running) {
        ssize_t n = recv(cfd, buffer, sizeof(buffer)-1, 0);
        buffer[n] = '\0';
        // Strip trailing "\r\n" (it must exist for normal commands)
        std::string command(buffer);
        if (command.size() >= 2 && command.substr(command.size()-2) == "\r\n") {
            command.resize(command.size() - 2);
        }
        std::istringstream line(command);
        std::string cmd; 
        line >> cmd;
        if (cmd == "GET") {
            std::string row, col;
            line >> row >> col;
            std::lock_guard<std::mutex> m_(mutex);
            std::cout << "[Tablet" << self_index << "] client" << cfd << ": GET " << row << " " << col <<  std::endl;
            if (!all_row_col.count(row) || !all_row_col[row].count(col)) {
                std::cout << "[Tablet" << self_index << "] client" << cfd << ": " << row << " " << col << " not found" << std::endl;
                send_all(cfd, "-ERR Not found\r\n");
            } else {
                int tab = get_tablet(row);
                if (tab != current_tablet) {
                    propogate_load(tab);
                }
                const auto &val = kvstore[row][col];
                // send size, recv READY, then send data
                std::string hdr = "+OK " + std::to_string(val.size()) + "\r\n";
                send_all(cfd, hdr);
                std::cout << "[Tablet" << self_index << "] client" << cfd << " should be ready for " << std::to_string(val.size()) << " bytes" << std::endl;
                char buf[64];
                recv(cfd, buf, sizeof(buf)-1, 0); // expect "READY\r\n"
                send_all(cfd, val);
                std::cout << "[Tablet" << self_index << "] client" << cfd << " should have received " << std::to_string(val.size()) << " bytes" << std::endl;
            }
        } else if (cmd == "PUT") {
            std::string row, col;
            size_t N;
            line >> row >> col >> N;
            std::lock_guard<std::mutex> m_(mutex);
            int tab = get_tablet(row);
            int prim = query_primary();
            if (prim == self_index && current_tablet != tab) {
                propogate_load(tab);
            }
            send_all(cfd, "+OK\r\n"); // acknowledge before receiving payload
            std::cout << "[Tablet" << self_index << "] Expect " << N << " bytes coming for PUT " << row << " " << col << " from client" << cfd << std::endl;
            // receive exactly N bytes of data
            std::string payload = recv_all(cfd, N);
            append_log("PUT " + row + " " + col + " " + payload);  // log
            kvstore[row][col] = payload;
            send_all(cfd, "+OK All bytes received\r\n");
            all_row_col[row].insert(col);
            std::cout << "[Tablet" << self_index << "] client" << cfd << ": successful PUT " << row << " " << col << " with " << N << " bytes" << std::endl;
            // replicate
            if (prim == self_index) {
                for (const int& rfd : get_alive_replicas()) {
                    send_all(rfd, "PUT " + row + " " + col + " " + std::to_string(N) + "\r\n");
                    char buf_[64];
                    recv(rfd, buf_, sizeof(buf_)-1, 0);  // "+OK\r\n"
                    send_all(rfd, payload);
                    recv(rfd, buf_, sizeof(buf_)-1, 0);  // "+OK All bytes received\r\n"
                    send_all(rfd, "QUIT\r\n");
                    close(rfd);
                    std::cout << "[Tablet" << self_index << "] propogated PUT to another replica" << std::endl;
                }
            }
        } else if (cmd == "CPUT") {
            std::string row, col, oldv, newv;
            line >> row >> col >> oldv >> newv;
            std::lock_guard<std::mutex> m_(mutex);
            int tab = get_tablet(row);
            int prim = query_primary();
            if (prim == self_index && current_tablet != tab) {
                propogate_load(tab);
            }
            if (kvstore.count(row) && kvstore[row].count(col) && kvstore[row][col] == oldv) {
                append_log("PUT " + row + " " + col + " " + newv); // reduce successful CPUT to PUT in LOG
                kvstore[row][col] = newv;
                send_all(cfd, "+OK CPUT Success\r\n");
                std::cout << "[Tablet" << self_index << "] CPUT success for " << row << " " << col << " with new value " << newv << std::endl;
                // replicate
                int prim = query_primary();
                if (prim == self_index) {
                    for (const int& rfd : get_alive_replicas()) {
                        send_all(rfd, "CPUT " + row + " " + col + " " +  oldv + " " + newv + "\r\n");
                        char buf_[64];
                        recv(rfd, buf_, sizeof(buf_)-1, 0);  // "+OK CPUT Success\r\n"
                        send_all(rfd, "QUIT\r\n");
                        close(rfd);
                        std::cout << "[Tablet" << self_index << "] propogated CPUT to another replica" << std::endl;
                    }
                }
            } else {
                send_all(cfd, "-ERR CPUT Failure\r\n");
                std::cout << "[Tablet" << self_index << "] CPUT failure for " << row << " " << col << " with new value " << newv << std::endl;
            }
        } else if (cmd == "DELETE") {
            std::string row, col;
            line >> row >> col;
            std::lock_guard<std::mutex> m_(mutex);
            if (!all_row_col.count(row) || !all_row_col[row].count(col)) {
                send_all(cfd, "-ERR Not found\r\n");
                std::cout << "[Tablet" << self_index << "] DELETE failure for " << row << " "  << col << std::endl;
            } else {
                append_log("DELETE " + row + " " + col); // log
                int tab = get_tablet(row);
                int prim = query_primary();
                if (prim == self_index && current_tablet != tab) {
                    propogate_load(tab);
                }
                kvstore[row].erase(col);
                all_row_col[row].erase(col);
                send_all(cfd, "+OK Deleted\r\n");
                std::cout << "[Tablet" << self_index << "] DELETE success for " << row << " "  << col << std::endl;
                // replicate
                if (prim == self_index) {
                    for (int rfd : get_alive_replicas()) {
                        // replicate the same DELETE to each live replica (skip self & dead ones)
                        send_all(rfd, "DELETE " + row + " " + col + "\r\n");
                        char buf_[64];
                        recv(rfd, buf_, sizeof(buf_)-1, 0);  // expect "+OK Deleted\r\n"
                        send_all(rfd, "QUIT\r\n");
                        close(rfd);
                        std::cout << "[Tablet" << self_index << "] propogated DELETE to another replica" << std::endl;
                    }
                }
            }
        } else if (cmd == "GET_ROWS") {
            std::lock_guard<std::mutex> m_(mutex);
            std::ostringstream os;
            os << "+OK";
            for (auto &p : all_row_col) os << " " << p.first;
            os << "\r\n";
            send_all(cfd, os.str());
            std::cout << "[Tablet" << self_index << "] client" << cfd << " should get all rows in this tablet" << std::endl;
        } else if (cmd == "GET_COLS") {
            std::string row;
            line >> row;
            std::lock_guard<std::mutex> m_(mutex);
            if (!all_row_col.count(row)) {
                send_all(cfd, "-ERR Not found\r\n");
                std::cout << "[Tablet" << self_index << "] client" << cfd << " wants all cols of " << row << " but not found" << std::endl;
            } else {
                std::ostringstream os;
                os << "+OK";
                for (const auto &c : all_row_col[row]) os << " " << c;
                os << "\r\n";
                send_all(cfd, os.str());
                std::cout << "[Tablet" << self_index << "] client" << cfd << " should have received all cols of " << row << std::endl;
            }
        } else if (cmd == "CHECKPOINT_VERSION") {
            std::lock_guard<std::mutex> m_(mutex);
            int subtablet;
            line >> subtablet;
            int version_number = version_of_checkpoint(subtablet);
            send_all(cfd, std::to_string(version_number) + "\r\n");
            char buf_[64];
            recv(cfd, buf_, sizeof(buf_) - 1, 0);
            if (buf_[0] == 'W') {  // "WANT\r\n"
                std::ifstream cp(checkpoint_file + std::to_string(subtablet), std::ios::binary);
                cp.seekg(0, std::ios::end);
                size_t sz = cp.tellg();
                cp.seekg(0);
                send_all(cfd, std::to_string(sz) + "\r\n");
                recv(cfd, buf_, sizeof(buf_)-1, 0);  // "READY\r\n"
                std::vector<char> chunk_buffer(std::min(sz, static_cast<size_t>(1024*1024)));
                while (sz > 0) {
                    size_t chunk = std::min(chunk_buffer.size(), sz);
                    cp.read(chunk_buffer.data(), chunk);
                    size_t sent = 0;
                    while (sent < chunk) {
                        ssize_t n = send(cfd, chunk_buffer.data() + sent, chunk - sent, 0);
                        sent += (size_t)n;
                    }
                    sz -= chunk;
                }
                cp.close();
                std::cout << "[Tablet" << self_index << "] client" << cfd << " should have received my checkpoint file" << std::endl;
            } else {
                std::cout << "[Tablet" << self_index << "] client" << cfd << " quit wanting chk file" << std::endl;
                send_all(cfd, "ACK\r\n");
            }
        } else if (cmd == "LOG_NUM") {
            std::lock_guard<std::mutex> m_(mutex);
            int subtablet;
            line >> subtablet;
            int log_counter = log_count(subtablet);
            send_all(cfd, std::to_string(log_counter) + "\r\n");
            char buff[64];
            int n_ = recv(cfd, buff, sizeof(buff) - 1, 0);
            if (buff[0] != 'N') {  // ignore "NO_NEED\r\n"
                std::ifstream lf(log_file + std::to_string(subtablet), std::ios::binary);
                buff[n_] = '\0';
                uint32_t lastN = (uint32_t)strtoull(buff, nullptr, 10); // only want to send last N entries
                uint32_t totalCount;
                lf.read(reinterpret_cast<char*>(&totalCount), sizeof(totalCount));
                // Figure out how many to skip
                uint32_t skip = (lastN < totalCount ? totalCount - lastN : 0);
                for (uint32_t i = 0; i < skip; ++i) {
                    uint32_t L;
                    lf.read(reinterpret_cast<char*>(&L), sizeof(L));
                    lf.seekg(L, std::ios::cur);
                }
                std::streampos startPos = lf.tellg();
                size_t bytesToSend = 0;
                {
                    // Temporary cursor to measure
                    std::streampos cur = startPos;
                    lf.seekg(cur);
                    for (uint32_t i = 0; i < lastN && lf; ++i) {
                        uint32_t L;
                        lf.read(reinterpret_cast<char*>(&L), sizeof(L));
                        bytesToSend += sizeof(L) + L;
                        lf.seekg(L, std::ios::cur);
                    }
                }
                // Tell client how many bytes they’ll get
                send_all(cfd, std::to_string(bytesToSend) + "\r\n");
                char readyBuf[64];
                recv(cfd, readyBuf, sizeof(readyBuf)-1, 0);  // “READY\r\n”
                // Seek back to where the last-N entries begin
                lf.clear();                        // clear eof flags
                lf.seekg(startPos, std::ios::beg);
                // Stream them out in chunks
                std::vector<char> tmp(1024*1024);
                size_t remaining = bytesToSend;
                while (remaining > 0 && lf) {
                    size_t toRead = std::min(tmp.size(), remaining);
                    lf.read(tmp.data(), toRead);
                    size_t actuallyRead = lf.gcount();
                    size_t sent = 0;
                    while (sent < actuallyRead) {
                        ssize_t w = send(cfd, tmp.data()+sent, actuallyRead - sent, 0);
                        sent += w;
                    }
                    remaining -= actuallyRead;
                }
                lf.close();
                std::cout << "[Tablet" << self_index << "] client" << cfd << " should have received my log file" << std::endl;
            } else {
                std::cout << "[Tablet" << self_index << "] client" << cfd << " quit wanting log file" << std::endl;
                send_all(cfd, "ACK\r\n");
            }
        } else if (cmd == "KILL") {  // this can only come from Admin Console
            dead = true;
            std::cout << "[Tablet" << self_index << "] client" << cfd << " killed me" << std::endl;
        } else if (cmd == "RESTART") {  // this can only come from Admin Console
            recover();
            std::cout << "[Tablet" << self_index << "] client" << cfd << " restarted me" << std::endl;
            dead = false;
        } else if (cmd == "QUIT") {
            std::cout << "[Tablet" << self_index << "] client" << cfd << " quit" << std::endl;
            close(cfd);
            return;
        } else if (cmd == "CHECK") {
            if (dead) {
                send_all(cfd, "-ERR\r\n");   // to mimic fake dead (no requests will be accepted)
            } else {
                send_all(cfd, "+OK\r\n");
            }
        } else if (cmd == "LOAD") {  // must be sent from primary
            int tab;
            line >> tab;
            std::lock_guard<std::mutex> m_(mutex);
            checkpoint(current_tablet);
            load_back(tab);
            send_all(cfd, "+OK\r\n");
        } else if (cmd == "CUR_TAB") {
            std::lock_guard<std::mutex> m_(mutex);
            send_all(cfd, std::to_string(current_tablet) + "\r\n");
        }
    }
    close(cfd);
}

int main(int argc, char* argv[]) {
    if(argc<3){ std::cerr<<"Usage: ./tablet <config> <self_index>\n"; return 1; }
    std::string cfg=argv[1]; self_index=std::stoi(argv[2]);
    std::ifstream f(cfg); std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find('#');
        if (pos != std::string::npos) line = trim(line.substr(0, pos));
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string ip = trim(line.substr(0, colon));
        int port = std::stoi(trim(line.substr(colon + 1)));
        nodes.emplace_back(ip, port);
    }
    num_nodes = nodes.size(); num_shards = num_nodes / REPLICATION_FACTOR;
    shard_i = self_index / REPLICATION_FACTOR;
    checkpoint_file = "checkpoint_node" + std::to_string(self_index) + "_";  // prefix
    log_file = "log_node" + std::to_string(self_index) + "_";  // prefix
    // if the files exist, it means it's not the first time this node starts
    if (fs::exists(checkpoint_file + std::to_string(0)) && fs::exists(log_file + std::to_string(0))) {
        recover();  
    } else {  // create them if it's the first time to start
        std::cout << "[Tablet" << self_index << "] First time started" <<  std::endl;
        for (int i = 0; i < num_tablets; ++i) {
            std::ofstream cp(checkpoint_file + std::to_string(i), std::ios::binary);
            std::ofstream lf(log_file + std::to_string(i), std::ios::binary);
        }
    }
    // handle shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    // Create listening socket on our assigned port
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(nodes[self_index].second);
    inet_pton(AF_INET, nodes[self_index].first.c_str(), &addr.sin_addr);
    ::bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 100);
    std::cout << "[Tablet"<< self_index << "] Listening on port " << nodes[self_index].second << std::endl;
    // Accept loop: spawn one thread per client
    while (running) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int fd = accept(listen_fd, (sockaddr*)&cli, &len);
        if (fd < 0) continue;
        std::string resp = "+OK Connected\r\n";
        send_all(fd, resp);
        std::cout << "[Tablet" << self_index << "] client" << fd << " connected" <<  std::endl;
        std::thread(handle_client, fd).detach();
    }
    return 0;
}
