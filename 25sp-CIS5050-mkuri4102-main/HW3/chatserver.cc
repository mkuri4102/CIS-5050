#include <string.h>
#include <string>
#include <time.h>
#include <map>
#include <vector>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include <sstream>
using namespace std;

const int MAX_NAME_LEN = 50;
const int MAX_BUFFER = 1000;
const int MAX_GROUPS = 10;
const int MSG_INITIAL = 0;
const int MSG_PROPOSE = 1;
const int MSG_DELIVER = 2;
const int ORDER_UNORDERED = 0;
const int ORDER_FIFO = 1;
const int ORDER_TOTAL = 2;




struct ChatClient {
    sockaddr_in address;
    int groupId;
    string name;
    ChatClient(sockaddr_in addr_) : address(addr_), groupId(0) {}
};

struct OrderedMessage {
    int sequence;
    int originNode;
    bool toDeliver;
    OrderedMessage(int s, int n) : sequence(s), originNode(n), toDeliver(false) {}
};

struct MessageComparator {
    bool operator()(const OrderedMessage& lhs, const OrderedMessage& rhs) const {
        return lhs.sequence < rhs.sequence || (lhs.sequence == rhs.sequence && lhs.originNode < rhs.originNode);
    }
};

vector<sockaddr_in> SERVER_LIST;
vector<ChatClient> ACTIVE_CLIENTS;
vector<int> FIFO_SEQUENCE;
vector<vector<int>> RECEIVED_SEQUENCE;
vector<vector<map<int, string>>> FIFO_QUEUE;
vector<int> TOTAL_PROPOSED;
vector<int> TOTAL_AGREED;
vector<map<OrderedMessage, string, MessageComparator>> TOTAL_QUEUE;
map<string, vector<OrderedMessage>> PROPOSAL_MAP;

bool VERBOSE = false;
int ORDER_TYPE = ORDER_UNORDERED; //defult
int SERVER_INDEX = 0;

sockaddr_in parse_address(char* address) {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    char* ip = strtok(address, ":");
    inet_pton(AF_INET, ip, &addr.sin_addr);

    char* port = strtok(NULL, ":");
    addr.sin_port = htons(atoi(port));

    return addr;
}

sockaddr_in load_config(const string& filename, int index) {
    ifstream config(filename);
    sockaddr_in self;

    int i = 0;
    string line;
    while (getline(config, line)) {
        char addresses[line.length() + 1];
        strcpy(addresses, line.c_str());

        char* forward = strtok(addresses, ",");
        char* bind = strtok(NULL, ",");

        SERVER_LIST.push_back(parse_address(forward));
        if (i == index - 1) {
            self = (bind == nullptr) ? parse_address(forward) : parse_address(bind);
        }
        ++i;
    }
    return self;
}

void initialize() {
    if (ORDER_TYPE == ORDER_UNORDERED) return;

    if (ORDER_TYPE == ORDER_FIFO) {
        for (int i = 0; i < MAX_GROUPS; ++i) {
            FIFO_SEQUENCE.push_back(0);
            RECEIVED_SEQUENCE.emplace_back(SERVER_LIST.size(), 0);
            FIFO_QUEUE.emplace_back(SERVER_LIST.size());
        }
    } else if (ORDER_TYPE == ORDER_TOTAL) {
        for (int i = 0; i < MAX_GROUPS; ++i) {
            TOTAL_PROPOSED.push_back(0);
            TOTAL_AGREED.push_back(0);
            TOTAL_QUEUE.emplace_back();
        }
    }
}

char* debug_timestamp() {
    char* prefix = new char[MAX_NAME_LEN];
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    strftime(prefix, MAX_NAME_LEN, "%X", timeinfo);

    char label[8];
    sprintf(label, " S%02d", SERVER_INDEX);
    strcat(prefix, label);
    return prefix;
}

bool identify_client(sockaddr_in src, int* idx) {
    for (size_t i = 0; i < ACTIVE_CLIENTS.size(); ++i) {
        if (strcmp(inet_ntoa(ACTIVE_CLIENTS[i].address.sin_addr), inet_ntoa(src.sin_addr)) == 0 &&
            ACTIVE_CLIENTS[i].address.sin_port == src.sin_port) {
            *idx = static_cast<int>(i) + 1;
            return true;
        }
    }
    return false;
}

bool identify_server(sockaddr_in src, int* idx) {
    for (size_t i = 0; i < SERVER_LIST.size(); ++i) {
        if (strcmp(inet_ntoa(SERVER_LIST[i].sin_addr), inet_ntoa(src.sin_addr)) == 0 &&
            SERVER_LIST[i].sin_port == src.sin_port) {
            *idx = static_cast<int>(i) + 1;
            return true;
        }
    }
    return false;
}

char* compose_message(char* content, ChatClient* client) {
    char* message = new char[MAX_BUFFER];
    if (client->name.empty()) {
        char temp[MAX_NAME_LEN];
        sprintf(temp, "%s:%d", inet_ntoa(client->address.sin_addr), client->address.sin_port);
        sprintf(message, "<%s> %s", temp, content);
    } else {
        sprintf(message, "<%s> %s", client->name.c_str(), content);
    }
    return message;
}

void send_to_group(int sock, char* message, int group) {
    for (size_t i = 0; i < ACTIVE_CLIENTS.size(); ++i) {
        if (ACTIVE_CLIENTS[i].groupId == group) {
            sendto(sock, message, strlen(message), 0, (struct sockaddr*)&ACTIVE_CLIENTS[i].address, sizeof(ACTIVE_CLIENTS[i].address));
            if (VERBOSE) {
                char* prefix = debug_timestamp();
                cout << prefix << " Server sends \"" << message << "\" to client " << i + 1
                     << " at chat room #" << ACTIVE_CLIENTS[i].groupId << endl;
                delete[] prefix;
            }
        }
    }
}

void broadcast_to_servers(int sock, char* message, bool include_self) {
    for (size_t i = 0; i < SERVER_LIST.size(); ++i) {
        if (!include_self && i == static_cast<size_t>(SERVER_INDEX - 1)) continue;
        sendto(sock, message, strlen(message), 0, (struct sockaddr*)&SERVER_LIST[i], sizeof(SERVER_LIST[i]));
    }
}


void process_client_message(int clientId, char* buffer, int sock) {
    ChatClient* client = &ACTIVE_CLIENTS[clientId - 1];

    if (buffer[0] == '/') {
        char* command = strtok(buffer, " ");
        char response[MAX_BUFFER];

        if (strcasecmp(command, "/join") == 0) {
            if (client->groupId == 0) {
                command = strtok(NULL, " ");
                int group = atoi(command);

                if (group <= MAX_GROUPS) {
                    client->groupId = group;
                    sprintf(response, "+OK You're now in chat room #%d", group);
                } else {
                    sprintf(response, "-ERR There are only %d chat room(s) you could join", MAX_GROUPS);
                }
            } else {
                sprintf(response, "-ERR You're already in room #%d", client->groupId);
            }
            sendto(sock, response, strlen(response), 0, (struct sockaddr*)&client->address, sizeof(client->address));

        } else if (strcasecmp(command, "/nick") == 0) {
            command = strtok(NULL, " ");
            client->name = command;
            sprintf(response, "+OK Nickname set to \"%s\"", command);
            sendto(sock, response, strlen(response), 0, (struct sockaddr*)&client->address, sizeof(client->address));

        } else if (strcasecmp(command, "/part") == 0) {
            if (client->groupId == 0) {
                sprintf(response, "-ERR You didn't join any chat room yet");
            } else {
                sprintf(response, "+OK You have left chat room #%d", client->groupId);
                client->groupId = 0;
            }
            sendto(sock, response, strlen(response), 0, (struct sockaddr*)&client->address, sizeof(client->address));

        } else if (strcasecmp(command, "/quit") == 0) {
            ACTIVE_CLIENTS.erase(ACTIVE_CLIENTS.begin() + clientId - 1);

        } else {
            sprintf(response, "-ERR Unknown command for client");
            sendto(sock, response, strlen(response), 0, (struct sockaddr*)&client->address, sizeof(client->address));
        }

    } else {
        if (client->groupId == 0) {
            char response[MAX_BUFFER];
            sprintf(response, "-ERR You didn't join any chat room yet");
            sendto(sock, response, strlen(response), 0, (struct sockaddr*)&client->address, sizeof(client->address));
        } else {
            char forward[MAX_BUFFER];
            char* formatted = compose_message(buffer, client);

            if (ORDER_TYPE == ORDER_UNORDERED) {
                send_to_group(sock, formatted, client->groupId);
                sprintf(forward, "%d|%d|%d|%d|%s", 0, SERVER_INDEX, 0, client->groupId, formatted);
                broadcast_to_servers(sock, forward, false);

            } else if (ORDER_TYPE == ORDER_FIFO) {
                send_to_group(sock, formatted, client->groupId);
                FIFO_SEQUENCE[client->groupId - 1]++;
                sprintf(forward, "%d|%d|%d|%d|%s", 0, SERVER_INDEX, FIFO_SEQUENCE[client->groupId - 1], client->groupId, formatted);
                broadcast_to_servers(sock, forward, false);

            } else if (ORDER_TYPE == ORDER_TOTAL) {
                sprintf(forward, "%d|%d|%d|%d|%s", MSG_INITIAL, SERVER_INDEX, 0, client->groupId, formatted);
                broadcast_to_servers(sock, forward, true);
            }
            delete[] formatted;
        }
    }
}

void handle_total_order(int sock, char* message, int group, int sender, int seq, int node, int type) {
    char msg[MAX_BUFFER];
    string content(message);
    int g = group - 1;

    if (type == MSG_INITIAL) {
        TOTAL_PROPOSED[g] = max(TOTAL_PROPOSED[g], TOTAL_AGREED[g]) + 1;
        OrderedMessage m(TOTAL_PROPOSED[g], SERVER_INDEX);
        TOTAL_QUEUE[g][m] = content;

        sprintf(msg, "%d|%d|%d|%d|%s", MSG_PROPOSE, SERVER_INDEX, TOTAL_PROPOSED[g], group, message);
        sendto(sock, msg, strlen(msg), 0, (struct sockaddr*)&SERVER_LIST[sender - 1], sizeof(SERVER_LIST[sender - 1]));

    } else if (type == MSG_PROPOSE) {
        if (PROPOSAL_MAP.find(content) == PROPOSAL_MAP.end()) {
            PROPOSAL_MAP[content] = {};
        }
        PROPOSAL_MAP[content].emplace_back(seq, node);

        if (PROPOSAL_MAP[content].size() == SERVER_LIST.size()) {
            int max_seq = 0, max_node = 0;
            for (const auto& m : PROPOSAL_MAP[content]) {
                if (m.sequence > max_seq || (m.sequence == max_seq && m.originNode > max_node)) {
                    max_seq = m.sequence;
                    max_node = m.originNode;
                }
            }

            sprintf(msg, "%d|%d|%d|%d|%s", MSG_DELIVER, max_node, max_seq, group, message);
            broadcast_to_servers(sock, msg, true);
            PROPOSAL_MAP.erase(content);
        }

    } else if (type == MSG_DELIVER) {
        OrderedMessage m(seq, node);
        m.toDeliver = true;

        for (auto it = TOTAL_QUEUE[g].begin(); it != TOTAL_QUEUE[g].end(); ++it) {
            if (it->second == content) {
                TOTAL_QUEUE[g].erase(it);
                TOTAL_QUEUE[g][m] = content;
                break;
            }
        }

        TOTAL_AGREED[g] = max(TOTAL_AGREED[g], seq);

        while (!TOTAL_QUEUE[g].empty() && TOTAL_QUEUE[g].begin()->first.toDeliver) {
            strncpy(msg, TOTAL_QUEUE[g].begin()->second.c_str(), MAX_BUFFER);
            send_to_group(sock, msg, group);
            TOTAL_QUEUE[g].erase(TOTAL_QUEUE[g].begin());
        }
    }
}

void handle_fifo_order(int sock, char* message, int group, int sender, int seq) {
    int g = group - 1;
    int s = sender - 1;

    FIFO_QUEUE[g][s][seq] = message;

    int next_seq = RECEIVED_SEQUENCE[g][s] + 1;
    while (FIFO_QUEUE[g][s].count(next_seq)) {
        const string& msg_str = FIFO_QUEUE[g][s][next_seq];
        char msg[MAX_BUFFER];
        strncpy(msg, msg_str.c_str(), MAX_BUFFER);
        send_to_group(sock, msg, group);

        FIFO_QUEUE[g][s].erase(next_seq);
        next_seq = ++RECEIVED_SEQUENCE[g][s] + 1;
    }
}

int start_server(sockaddr_in self) {
    int sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        fprintf(stderr, "error - cannot open socket\n");
        exit(2);
    }

    const int REUSE = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &REUSE, sizeof(REUSE));

    self.sin_addr.s_addr = htons(INADDR_ANY);
    bind(sock, (struct sockaddr*)&self, sizeof(self));

    sockaddr_in src;
    socklen_t src_size = sizeof(src);

    while (true) {
        char buff[MAX_BUFFER];
        int read_len = recvfrom(sock, buff, sizeof(buff), 0, (struct sockaddr*)&src, &src_size);
        buff[read_len] = 0;

        int index = 0;
        if (identify_server(src, &index)) {
            if (VERBOSE) {
                char* prefix = debug_timestamp();
                cout << prefix << " Server " << index << " forward \"" << buff << "\"" << endl;
                delete[] prefix;
            }

            int type = atoi(strtok(buff, "|"));
            int node = atoi(strtok(NULL, "|"));
            int seq = atoi(strtok(NULL, "|"));
            int group = atoi(strtok(NULL, "|"));
            char* message = strtok(NULL, "|");

            if (ORDER_TYPE == ORDER_UNORDERED) {
                send_to_group(sock, message, group);
            } else if (ORDER_TYPE == ORDER_FIFO) {
                handle_fifo_order(sock, message, group, index, seq);
            } else if (ORDER_TYPE == ORDER_TOTAL) {
                handle_total_order(sock, message, group, index, seq, node, type);
            }

        } else {
            if (!identify_client(src, &index)) {
                ChatClient new_client(src);
                ACTIVE_CLIENTS.push_back(new_client);
                index = ACTIVE_CLIENTS.size();
            }

            if (VERBOSE) {
                char* prefix = debug_timestamp();
                cout << prefix << " Client " << index << " posts \"" << buff << "\"" << endl;
                delete[] prefix;
            }
            process_client_message(index, buff, sock);
        }
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "*** Author: Minna Kuriakose (mkuri)");
        exit(1);
    }

    int opt;
    while ((opt = getopt(argc, argv, "o:v")) != -1) {
        switch (opt) {
            case 'o':
                if (strcasecmp(optarg, "unordered") == 0) ORDER_TYPE = ORDER_UNORDERED;
                else if (strcasecmp(optarg, "fifo") == 0) ORDER_TYPE = ORDER_FIFO;
                else if (strcasecmp(optarg, "total") == 0) ORDER_TYPE = ORDER_TOTAL;
                else {
                    fprintf(stderr, "Supported Order: unordered, fifo, total");
                    exit(1);
                }
                break;
            case 'v':
                VERBOSE = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-o order] [-v] <config file> <index>", argv[0]);
                exit(1);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s [-o order] [-v] <config file> <index>", argv[0]);
        exit(1);
    }
    string config_path = argv[optind++];

    if (optind >= argc) {
        fprintf(stderr, "Usage: %s [-o order] [-v] <config file> <index>", argv[0]);
        exit(1);
    }
    SERVER_INDEX = atoi(argv[optind]);

    sockaddr_in self_addr = load_config(config_path, SERVER_INDEX);
    initialize();
    start_server(self_addr);
}

