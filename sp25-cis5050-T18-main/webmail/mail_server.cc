#include <iostream>
#include <sstream>
#include <string>
#include <resolv.h>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <semaphore.h>
#include <algorithm>
#include <ctime>
#include <dirent.h>
#include <unordered_map>
#include <netdb.h>

#ifndef C_IN
    #define C_IN 1  // Internet class
#endif

#ifndef T_MX
    #define T_MX 15 // MX record type
#endif

using namespace std;

// ====================
// Configuration Macros
// ====================
#define SERVER_PORT 2500
#define MASTER_PORT 5050
#define MAX_CLIENTS 1000
#define BUFFER_SIZE 10240

// ====================
// Global Variables
// ====================
int server_socket;                        // Server socket descriptor
vector<int> client_sockets;               // List of active client sockets
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t *connection_semaphore;              // Semaphore for limiting concurrent connections
bool shutting_down = false;               // Shutdown flag

// Helper: Recv exactly n bytes
static std::string recv_all(int fd, size_t n) {
    std::string buf;
    buf.reserve(n);
    while (buf.size() < n) {
        char tmp[64 * 1024];
        size_t to_read = std::min(sizeof(tmp), n - buf.size());
        int r = recv(fd, tmp, to_read, 0);
        if (r <= 0) break;
        buf.append(tmp, r);
    }
    std::cout << "Successfully received "<< n << " bytes from client " << fd <<  std::endl;
    return buf;
}

// Helper: Send exactly n bytes
static void send_all(int fd, const std::string &s) {
    const char *buf = s.data(); size_t n = s.size();
    size_t total = 0;
    while (total < n) {
        ssize_t w = send(fd, buf + total, n - total, 0);
        if (w <= 0) {
            return;                   
        }             
        total += (size_t)w;
    }
}

// Helper Function: Send Command and Receive Response
bool sendReceiveCommand(int sock, const string &cmd, string &response) {
    if (send(sock, cmd.c_str(), cmd.length(), 0) < 0) {
        perror("Failed to send command");
        return false;
    }
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(sock, buffer, sizeof(buffer)-1, 0);
    if (bytes < 0) {
        perror("Failed to receive response");
        return false;
    }
    response = string(buffer);
    return true;
}

// Returns current timestamp as a string
string get_timestamp() {
    time_t now = time(nullptr);
    struct tm *time_info = localtime(&now);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", time_info);
    return string(buffer);
}

// Helper Function: Ask Master for Redirection and Connect to Backend Node
int getBackendSocket(const string &key) {
    // Connect to master node on a fixed port (5050)
    int master_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (master_socket < 0) {
        perror("Master node socket creation failed");
        return -1;
    }
    struct sockaddr_in master_addr;
    memset(&master_addr, 0, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = htons(MASTER_PORT); // Master node port
    master_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // local by default
    if (connect(master_socket, (struct sockaddr *)&master_addr, sizeof(master_addr)) < 0) {
        perror("Master node connection failed");
        close(master_socket);
        return -1;
    }
    char response_buffer[BUFFER_SIZE];
    int bytes = recv(master_socket, response_buffer, sizeof(response_buffer)-1, 0);
    if (bytes < 0) {
        perror("Failed to receive response from master node");
        return -1;
    }
    // Send the ASK command for redirection
    string ask_cmd = "ASK " + key + "\r\n";
    if (send(master_socket, ask_cmd.c_str(), ask_cmd.size(), 0) < 0) {
        perror("Failed to send ASK command");
        close(master_socket);
        return -1;
    }
    // Receive the response for redirection from the master node
    memset(response_buffer, 0, sizeof(response_buffer));
    recv(master_socket, response_buffer, sizeof(response_buffer)-1, 0);
    // Parse the response (expected format: "+OK REDIRECT ip:port\r\n")
    string master_response(response_buffer);
    const string prefix = "+OK REDIRECT ";
    if (master_response.compare(0, prefix.size(), prefix) != 0) {
        cerr << "Invalid response from master node: " << master_response << endl;
        return -1;
    }
    string quit = "QUIT\r\n";
    send(master_socket, quit.c_str(), quit.size(), 0);
    close(master_socket);
    string port_str = master_response.substr(prefix.size());
    port_str.erase(port_str.find_last_not_of(" \r\n") + 1);
    size_t colonPos = port_str.find(":");
    string ip_str = port_str.substr(0, colonPos);
    const char* ip = ip_str.c_str();
    string port = port_str.substr(colonPos + 1);
    int backend_port = atoi(port.c_str());
    // Connect to the backend node
    int backend_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_socket < 0) {
        perror("Backend node socket creation failed");
        return -1;
    }
    struct sockaddr_in backend_addr;
    memset(&backend_addr, 0, sizeof(backend_addr));
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(backend_port);
    backend_addr.sin_addr.s_addr = inet_addr(ip);
    if (connect(backend_socket, (struct sockaddr *)&backend_addr, sizeof(backend_addr)) < 0) {
        perror("Backend node connection failed");
        close(backend_socket);
        return -1;
    }
    char* buf_[32]; recv(backend_socket, buf_, sizeof(buf_)-1, 0); // expect "+OK Connected\r\n"
    return backend_socket;
}

// Generate next unique email ID as a string for a user
string generate_email_id(const string& user) {
    int backend_socket = getBackendSocket(user);
    string get_cmd = "GET " + user + " emails\r\n";  // get ID list
    string backend_response;
    sendReceiveCommand(backend_socket, get_cmd, backend_response);
    cout << "Backend response for GET emailIDs: " << backend_response << endl;
    const string ok_prefix = "+OK ";
    string mail_id;
    if (backend_response.compare(0, ok_prefix.size(), ok_prefix) != 0) { // be "-ERR Not found", we initialize it
        mail_id = "1";
    } else {
        backend_response.erase(0, 4);  // erase "+OK "           
        backend_response.erase(backend_response.find_first_of("\r\n"));  
        size_t sz = std::stoull(backend_response);
        send_all(backend_socket, "READY\r\n");
        string IDs = recv_all(backend_socket, sz);
        size_t pos = IDs.find_last_of(' ');
        std::string last_id = (pos == std::string::npos) ? IDs : IDs.substr(pos + 1);
        int next = std::stoi(last_id) + 1;
        mail_id = std::to_string(next);
    }
    string quit = "QUIT\r\n";
    send(backend_socket, quit.c_str(), quit.size(), 0);
    close(backend_socket);
    return mail_id;
}

// Send an SMTP command to outside server (e.g., gmail)
void send_smtp_command(int sock, const string &cmd) {
    string full_cmd = cmd + "\r\n";
    send(sock, full_cmd.c_str(), full_cmd.length(), 0);
    cerr << "C: " << cmd << endl;
}

// Receive SMTP response from outside server (e.g., gmail)
string recv_smtp_response(int sock) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE - 1, 0);
    cerr << "S: " << buffer;
    return string(buffer);
}

// Obtain the MX record for a domain
string get_mx_record(const string &domain) {    
    unsigned char query_buffer[NS_PACKETSZ];
    int query_len = res_query(domain.c_str(), C_IN, T_MX, query_buffer, sizeof(query_buffer));
    if (query_len < 0) {
        cerr << "[ERROR] res_query failed for domain: " << domain << endl;
        return "";
    }
    ns_msg handle;
    if (ns_initparse(query_buffer, query_len, &handle) < 0) {
        perror("ns_initparse failed");
        return "";
    }
    ns_rr rr;
    if (ns_parserr(&handle, ns_s_an, 0, &rr) < 0) {
        perror("ns_parserr failed");
        return "";
    }
    char mx_host[NS_MAXDNAME];
    if (ns_name_uncompress(ns_msg_base(handle), ns_msg_end(handle),
                           ns_rr_rdata(rr) + 2, mx_host, sizeof(mx_host)) < 0) {
        perror("ns_name_uncompress failed");
        return "";
    }
    return string(mx_host);
}

// Helper Function: Relay an email to external recipients for a given domain
// Assume all outside email addresses MUST be valid
bool relay_email_to_outside(const string &sender, const vector<string> &recipients, const string &message) {
    // Group recipients by domain
    unordered_map<string, vector<string>> domain_map;
    for (const auto &recipient : recipients) {
        size_t at_pos = recipient.find("@");  // Invalid email format
        if (at_pos == string::npos) continue;
        string domain = recipient.substr(at_pos + 1);
        domain_map[domain].push_back(recipient);
    }

    bool all_success = true;
    for (auto &[domain, recips] : domain_map) {
        string mail_server = get_mx_record(domain);
        if (mail_server.empty()) {
            cerr << "[ERROR] No MX record found for domain: " << domain << endl;
            all_success = false;
            break;
        }
        // Resolve mail server address
        struct hostent *server = gethostbyname(mail_server.c_str());
        if (!server) {
            cerr << "[ERROR] Failed to resolve " << mail_server << endl;
            all_success = false;
            break;
        }
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(25);
        memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
        // Connect to the mail server
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            cerr << "[ERROR] Failed to connect to " << mail_server << endl;
            close(sock);
            all_success = false;
            break;
        }
        // SMTP handshake + extended features
        recv_smtp_response(sock);
        send_smtp_command(sock, "EHLO penncloud.com");
        recv_smtp_response(sock);
        // Send MAIL FROM commands (change domain to avoid anti-spam)
        string relay_sender = sender.substr(0, sender.find("@")) + "@" + domain;
        send_smtp_command(sock, "MAIL FROM:<" + relay_sender + ">");
        recv_smtp_response(sock);
        // Send RCPT TO commands
        for (const auto &rcpt : recips) {
            send_smtp_command(sock, "RCPT TO:<" + rcpt + ">");
            string response = recv_smtp_response(sock);
            if (response.find("250") == string::npos) {
                cerr << "[ERROR] Recipient rejected: " << rcpt << endl;
                all_success = false;
                break;
            }
        }
        if (!all_success) {
            close(sock); break;
        }
        // Send email content
        send_smtp_command(sock, "DATA");
        recv_smtp_response(sock);
        send_smtp_command(sock, message + "\r\n.");
        recv_smtp_response(sock);
        // Quit connection
        send_smtp_command(sock, "QUIT");
        close(sock);
    }
    return all_success;
}

// KV Store Storage Functionality for Local Recipients
void store_email_in_kv(const string &recipient, const string &sender, const string &subject, const string &content) {
    string timestamp = get_timestamp();
    string mail_id = generate_email_id(recipient);
    // Build plain text
    ostringstream plain;
    plain << "Sender: " << sender << "|";
    plain << "Subject: " << subject << "|";
    plain << "Time: " << timestamp << "|";
    plain << "Content: " << content; // content lines are also separated by "|"
    string plain_text = plain.str();
    // Construct a KV PUT command using the plain text above
    ostringstream cmd;
    cmd << "PUT " << recipient << " EMAIL" << mail_id << " " << plain_text.size() << "\r\n";
    string put_cmd = cmd.str();

    int backend_socket = getBackendSocket(recipient); // use helper function
    if (backend_socket < 0) {
        perror("Failed to connect to backend partition");
        return;
    }
    // Send the PUT command and receive response (use helper function)
    string backend_response;
    sendReceiveCommand(backend_socket, put_cmd, backend_response);
    cout << "Backend response for PUT command: " << backend_response << endl;
    send_all(backend_socket, plain_text);
    char* buf_[64];
    recv(backend_socket,buf_,sizeof(buf_)-1,0); // expect "+OK All bytes received\r\n" here
    cout << "Email stored successfully: " << plain_text << endl;
    // Then, we must also update the emailID list for this recipient
    string get_cmd = "GET " + recipient + " emails\r\n";
    if (!sendReceiveCommand(backend_socket, get_cmd, backend_response)) {
        close(backend_socket);
        return;
    }
    cout << "Backend response for GET command: " << backend_response << endl;
    const string ok_prefix = "+OK ";
    if (backend_response.compare(0, ok_prefix.size(), ok_prefix) != 0) { // be "-ERR Not found", we initialize it
        put_cmd = "PUT " + recipient + " emails " + to_string(mail_id.size()) + "\r\n";
        send_all(backend_socket, put_cmd);
        char* buf_[32]; recv(backend_socket, buf_, sizeof(buf_)-1, 0);
        send_all(backend_socket, mail_id);
        recv(backend_socket, buf_, sizeof(buf_)-1, 0);
    } else {
        backend_response.erase(0, 4);  // erase "+OK "           
        backend_response.erase(backend_response.find_first_of("\r\n"));  
        size_t sz = std::stoull(backend_response);
        send_all(backend_socket, "READY\r\n");
        string IDs = recv_all(backend_socket, sz);
        IDs += " " + mail_id;
        put_cmd = "PUT " + recipient + " emails " + to_string(IDs.size()) + "\r\n";
        send_all(backend_socket, put_cmd);
        char* buf_[32]; recv(backend_socket, buf_, sizeof(buf_)-1, 0);
        send_all(backend_socket, IDs);
        recv(backend_socket, buf_, sizeof(buf_)-1, 0);
    }
    cout << "Updating email IDs completed" << endl;
    string quit = "QUIT\r\n";
    send(backend_socket, quit.c_str(), quit.size(), 0);
    close(backend_socket);
}

// Delete an email given a username and emailID (assume parameters passed in are valid)
void delete_email(const string& username, const string& mail_id) {
    string delete_cmd = "DELETE " + username + " EMAIL" + mail_id + "\r\n";
    // Get the backend socket using username as the key
    int backend_socket = getBackendSocket(username);
    if (backend_socket < 0) {
        return;
    }
    // Send the DELETE command
    string backend_response;
    if (!sendReceiveCommand(backend_socket, delete_cmd, backend_response)) {
        close(backend_socket);
        return;
    }
    cout << "Backend response for DELETE command: " << backend_response << endl;
    // Update the email ID list for the user
    string get_cmd = "GET " + username + " emails\r\n";
    if (!sendReceiveCommand(backend_socket, get_cmd, backend_response)) {
        close(backend_socket);
        return;
    }
    cout << "Backend response for GET command: " << backend_response << endl;
    backend_response.erase(0, 4);  // erase "+OK "           
    backend_response.erase(backend_response.find_first_of("\r\n"));  
    size_t sz = std::stoull(backend_response);
    send_all(backend_socket, "READY\r\n");
    string IDs = recv_all(backend_socket, sz);
    // Remove the mail_id from the list
    istringstream iss(IDs);
    ostringstream oss;
    string token;
    bool first = true;
    int count = 0;
    while (iss >> token) {
        if (token == mail_id) continue;
        if (!first) {
            oss << " ";
        }
        oss << token;
        count++;
        first = false;
    }
    if (count) {
        string updated_ids = oss.str();
        string put_cmd = "PUT " + username + " emails " + to_string(updated_ids.size()) + "\r\n";
        if (!sendReceiveCommand(backend_socket, put_cmd, backend_response)) {
            close(backend_socket);
            return;
        }
        cout << "Backend response for updating email IDs (after deletion): " << backend_response << endl;
        send_all(backend_socket, updated_ids);
        char* buf_[128];
        recv(backend_socket, buf_, sizeof(buf_)-1, 0);  // expect "+OK All bytes received"
    } else {
         string backend_response;
         delete_cmd = "DELETE " + username + " emails\r\n";
        if (!sendReceiveCommand(backend_socket, delete_cmd, backend_response)) {
            close(backend_socket);
            return;
        }
        cout << "Backend response for DELETE command (now no emails at all): " << backend_response << endl;
    }
    string quit = "QUIT\r\n";
    send(backend_socket, quit.c_str(), quit.size(), 0);
    close(backend_socket);
}

void forward_email(const string& username, const string& mail_id, const vector<string>& locals, const vector<string>& non_locals) {
    int backend_socket = getBackendSocket(username);
    if (backend_socket < 0) {
        return;
    }
    string get_cmd = "GET " + username + " EMAIL" + mail_id + "\r\n";
    string backend_response;
    if (!sendReceiveCommand(backend_socket, get_cmd, backend_response)) {
        close(backend_socket);
        return;
    }
    backend_response.erase(0, 4);  // erase "+OK "           
    backend_response.erase(backend_response.find_first_of("\r\n"));  
    size_t sz = std::stoull(backend_response);
    send_all(backend_socket, "READY\r\n");
    string email = recv_all(backend_socket, sz);
    vector<string> tokens;
    stringstream ss(email);
    string token;
    while (getline(ss, token, '|')) { // parse
        tokens.push_back(token);
    }
    // Assume tokens[0] is sender, tokens[1] is subject, tokens[2] is time, and tokens[3..N] are content
    string forwarder = username + "@penncloud";
    string original_sender, subject, original_time;
    string contentWithDelim, outgoing_content; 
    // contentWithDelim is for local receivers, outgoing_content is for outside receivers
    const string senderLabel = "Sender: ";
    const string subjectLabel = "Subject: ";
    const string timeLabel = "Time: ";
    const string contentLabel = "Content: ";
    original_sender = tokens[0].substr(senderLabel.size());
    subject = "FWD: " + tokens[1].substr(subjectLabel.size());
    original_time = tokens[2].substr(timeLabel.size());
    contentWithDelim += "###The email below is forwarded. It was previously sent by " + original_sender + " on " + original_time + ".|";
    outgoing_content += "###The email below is forwarded. It was previously sent by " + original_sender + " on " + original_time + ".\r\n";
    string firstContentLine = tokens[3].substr(contentLabel.size());
    contentWithDelim += firstContentLine;
    outgoing_content += firstContentLine;
    // if the original content has more than one line
    for (size_t i = 4; i < tokens.size(); i++) {
        contentWithDelim += "|" + tokens[i];
        outgoing_content += "\r\n" + tokens[i];
    }
    // deal with locals
    for (const auto &local : locals) {
        store_email_in_kv(local, forwarder, subject, contentWithDelim); // store email in kv and also update email ID list
    }
    // deal with non_locals
    ostringstream full_message;
    full_message << "Subject: " << subject << "\r\n" << outgoing_content;
    int countTry = 0;
    while (!relay_email_to_outside(forwarder, non_locals, full_message.str()) && countTry <= 10) {
        cerr << "[INFO] Relay failed, retrying..." << endl;
        countTry++;
    }
    string quit = "QUIT\r\n";
    send(backend_socket, quit.c_str(), quit.size(), 0);
    close(backend_socket);
}

// Assume we only reply to one person (who previously sent the email), so it is either local or nonlocal
// Also notice that the latter four parameters are passed by reference so we can update them
void reply_helper(const string& username, const string& mail_id, vector<string>& local_recipients, vector<string>& external_recipients, string& email_subject, string& email_content) {
    local_recipients.clear();
    external_recipients.clear();
    email_subject.clear();
    email_content.clear();
    int backend_socket = getBackendSocket(username);
    if (backend_socket < 0) {
        return;
    }
    string get_cmd = "GET " + username + " EMAIL" + mail_id + "\r\n";
    string backend_response;
    if (!sendReceiveCommand(backend_socket, get_cmd, backend_response)) {
        close(backend_socket);
        return;
    }
    backend_response.erase(0, 4);  // erase "+OK "           
    backend_response.erase(backend_response.find_first_of("\r\n"));  
    size_t sz = std::stoull(backend_response);
    send_all(backend_socket, "READY\r\n");
    string original_email = recv_all(backend_socket, sz);
    vector<string> tokens;
    stringstream ss(original_email);
    string token;
    while (getline(ss, token, '|')) { // parse
        tokens.push_back(token);
    }
    // Assume tokens[0] is sender, tokens[1] is subject, tokens[2] is time, and tokens[3..N] are content
    string original_sender, original_time;
    // contentWithDelim is for local receivers, outgoing_content is for outside receivers
    const string senderLabel = "Sender: ";
    const string subjectLabel = "Subject: ";
    const string timeLabel = "Time: ";
    const string contentLabel = "Content: ";
    original_sender = tokens[0].substr(senderLabel.size());
    if (original_sender.find("@penncloud") != string::npos) {
        local_recipients.push_back(original_sender.substr(0, original_sender.find('@')));
    } else {
        external_recipients.push_back(original_sender);
    }
    email_subject = "RE: " + tokens[1].substr(subjectLabel.size());
    original_time = tokens[2].substr(timeLabel.size());
    // Add prefix to email content (orginal content)
    email_content += "###The email below was previously sent by " + original_sender + " on " + original_time + ".|";
    string firstContentLine = tokens[3].substr(contentLabel.size());
    email_content += firstContentLine;
    // if the original content has more than one line
    for (size_t i = 4; i < tokens.size(); i++) {
        email_content += "|" + tokens[i];
    }
    email_content += "|###The email below is the response by " + username + ".|";
    string quit = "QUIT\r\n";
    send(backend_socket, quit.c_str(), quit.size(), 0);
    close(backend_socket);
}

// Send response back to the client (could be frontend or outside client)
void send_response(int client_fd, const string &code, const string &message) {
    string response = code + " " + message + "\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
    cerr << "[" << client_fd << "] S: " << response;
}

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    delete (int *)arg;
    cerr << "[" << client_fd << "] New connection" << endl;
    send_response(client_fd, "220", "mail_server.penncloud SMTP ready");

    char recv_buffer[BUFFER_SIZE];
    string buffer;
    string sender_email;
    string email_subject;
    string email_content;
    vector<string> local_recipients;
    vector<string> external_recipients;
    bool in_data_mode = false;

    while (!shutting_down) {
        memset(recv_buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_fd, recv_buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0 || shutting_down) break;
        buffer.append(recv_buffer, bytes_received);
        size_t pos;
        while ((pos = buffer.find("\r\n")) != string::npos) {
            string command = buffer.substr(0, pos);
            buffer.erase(0, pos + 2);
            cerr << "[" << client_fd << "] C: " << command << endl;
            // If in DATA mode, process as message content
            if (in_data_mode) {
                if (command == ".") { // can be either sending or replying
                    // End DATA mode—process email:
                    // For each local recipient, store in KV as plain text with "|" delimiters
                    for (const auto &rcpt : local_recipients) {
                        store_email_in_kv(rcpt, sender_email, email_subject, email_content);
                    }
                    // For external recipients, use relay_email.
                    if (!external_recipients.empty()) {
                        // Convert '|' separators in email_content to "\r\n"
                        string outgoing_content = email_content;
                        size_t pos = 0;
                        while ((pos = outgoing_content.find("|", pos)) != string::npos) {
                            outgoing_content.replace(pos, 1, "\r\n");
                            pos += 2; // Skip over the replaced "\r\n"
                        }
                        ostringstream full_message;
                        full_message << "Subject: " << email_subject << "\r\n" << outgoing_content;
                        int countTry = 0;
                        while (!relay_email_to_outside(sender_email, external_recipients, full_message.str()) && countTry <= 10) {
                            cerr << "[INFO] Relay failed, retrying..." << endl;
                            countTry++;
                        }
                    }
                    send_response(client_fd, "250", "OK: Message sent");
                    // Reset state
                    in_data_mode = false;
                    sender_email.clear();
                    email_subject.clear();
                    email_content.clear();
                    local_recipients.clear();
                    external_recipients.clear();
                } else {
                    // If line begins with "Subject:" and subject not yet captured, extract it and do not add it to email_content
                    if (command.rfind("Subject:", 0) == 0 && email_subject.empty()) {
                        email_subject = command.substr(8);
                        if (!email_subject.empty() && email_subject.front() == ' ') email_subject.erase(0, 1);
                    } else if (command.rfind("Content-Type:", 0) == 0
                        || command.rfind("Content-Transfer-Encoding:", 0) == 0
                        || email_subject.empty()) {}  // ignore Thunderbird redundant messages
                    else {
                        email_content += command + "|";
                    }
                }
            } else {
                // Not in DATA mode: parse SMTP commands
                istringstream iss(command);
                string cmd;
                iss >> cmd;
                transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
                // Note that we do not handle every error of "wrong order" or "wrong syntax" too specifically as in HW2, 
                // since the frontend nodes and Thunderbird will always send commands properly
                if (cmd == "HELO") {
                    string domain;
                    iss >> domain;
                    if (domain.empty())
                        send_response(client_fd, "501", "HELO requires domain");
                    else {
                        send_response(client_fd, "250", "Hello " + domain);
                    }
                }
                else if (cmd == "DELE") {
                    // Expect: "DELE username emailID"
                    string username, mail_id;
                    iss >> username; iss >> mail_id;
                    delete_email(username, mail_id);
                    send_response(client_fd, "250", "Email deleted for " + username + " with emailID " + mail_id);
                }
                else if (cmd == "FORW") {
                    // Expect: "FORW username emailID receiverEmail1 receiverEmail2 receiverEmail3"
                    string username, mail_id;
                    iss >> username; iss >> mail_id;
                    vector<string> locals, non_locals; 
                    // note that locals store only usernames, but non_locals store the whole email addresses
                    string address;
                    while (iss >> address) {
                        if (address.find("@penncloud") != string::npos) {
                            locals.push_back(address.substr(0, address.find('@')));
                        } else {
                            non_locals.push_back(address);
                        }
                    }
                    forward_email(username, mail_id, locals, non_locals);
                    send_response(client_fd, "250", "Email forwarding succeeded");
                }
                else if (cmd == "REPL") {
                    // Expect: "REPL username emailID", then on a new line, "DATA\r\n", and then content and then on a new line ".\r\n"
                    string username, mail_id;
                    iss >> username; iss >> mail_id;
                    sender_email = username + "@penncloud";
                    reply_helper(username, mail_id, local_recipients, external_recipients, email_subject, email_content);
                    send_response(client_fd, "250", "REPL command received");
                }
                else if (cmd == "MAIL") {
                    // Expect: MAIL FROM:<sender_email>
                    size_t pos_from = command.find("FROM:");
                    if (pos_from == string::npos) {
                        send_response(client_fd, "501", "Syntax error in MAIL command");
                    } else {
                        sender_email = command.substr(pos_from + 5);
                        // Remove any angle brackets and spaces.
                        sender_email.erase(remove(sender_email.begin(), sender_email.end(), '<'), sender_email.end());
                        sender_email.erase(remove(sender_email.begin(), sender_email.end(), '>'), sender_email.end());
                        sender_email.erase(remove(sender_email.begin(), sender_email.end(), ' '), sender_email.end());
                        if (sender_email.empty())
                            send_response(client_fd, "501", "Invalid sender email");
                        else {
                            send_response(client_fd, "250", "Sender OK");
                        }
                    }
                }
                else if (cmd == "RCPT") {
                    // Expect: RCPT TO:<recipient_email>
                    size_t pos_to = command.find("TO:");
                    if (pos_to == string::npos) {
                        send_response(client_fd, "501", "Syntax error in RCPT command");
                    } else {
                        string recip = command.substr(pos_to + 3);
                        recip.erase(remove(recip.begin(), recip.end(), '<'), recip.end());
                        recip.erase(remove(recip.begin(), recip.end(), '>'), recip.end());
                        recip.erase(remove(recip.begin(), recip.end(), ' '), recip.end());
                        // Split recipient into local part and domain.
                        size_t at_pos = recip.find("@");
                        if (at_pos == string::npos) {
                            send_response(client_fd, "501", "Invalid recipient address");
                        } else {
                            string local_part = recip.substr(0, at_pos);
                            string domain = recip.substr(at_pos);
                            if (domain == "@penncloud") {
                                // Local recipient: store only the local part
                                local_recipients.push_back(local_part);
                                send_response(client_fd, "250", "Recipient OK (local)");
                            } else {
                                external_recipients.push_back(recip);
                                send_response(client_fd, "250", "Recipient OK (external)");
                            }
                        }
                    }
                }
                else if (cmd == "DATA") {
                    send_response(client_fd, "354", "End data with <CRLF>.<CRLF>");
                    in_data_mode = true;
                }
                else if (cmd == "RSET") {
                    sender_email.clear();
                    email_subject.clear();
                    email_content.clear();
                    local_recipients.clear();
                    external_recipients.clear();
                    in_data_mode = false;
                    send_response(client_fd, "250", "OK");
                }
                else if (cmd == "NOOP") {
                    send_response(client_fd, "250", "OK");
                }
                else if (cmd == "QUIT") {
                    send_response(client_fd, "221", "Bye");
                    cerr << "[" << client_fd << "] Connection closed" << endl;
                    shutdown(client_fd, SHUT_RDWR);
                    close(client_fd);
                    pthread_mutex_lock(&client_mutex);
                    client_sockets.erase(remove(client_sockets.begin(), client_sockets.end(), client_fd), client_sockets.end());
                    pthread_mutex_unlock(&client_mutex);
                    sem_post(connection_semaphore);
                    pthread_exit(nullptr);
                    return nullptr;
                }
                else {
                    send_response(client_fd, "500", "Command unrecognized");
                }
            }
        }
    }
    return nullptr;
}

// Signal Handler for Graceful Shutdown
void handle_shutdown(int signal) {
    shutting_down = true;
    cerr << "\n[Server] Received SIGINT. Shutting down mail_server..." << endl;
    pthread_mutex_lock(&client_mutex);
    for (int sock : client_sockets) {
        send_response(sock, "421", "Server shutting down");
        shutdown(sock, SHUT_RDWR);
        close(sock);
    }
    client_sockets.clear();
    pthread_mutex_unlock(&client_mutex);
    close(server_socket);
    sem_close(connection_semaphore);
    sem_unlink("/mailserver_semaphore");
    cerr << "[Server] Gracefully shut down." << endl;
    exit(0);
}

// ====================
// Main Function
// ====================
int main(int argc, char *argv[]) {
    // Set up signal handler for graceful shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    // Set up server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        cerr << "[ERROR] Unable to create server socket." << endl;
        return 1;
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (::bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        cerr << "[ERROR] Failed to bind to port " << SERVER_PORT << endl;
        return 1;
    }
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        cerr << "[ERROR] Failed to listen on port " << SERVER_PORT << endl;
        return 1;
    }
    connection_semaphore = sem_open("/mailserver_semaphore", O_CREAT, 0644, MAX_CLIENTS);
    if (connection_semaphore == SEM_FAILED) {
        cerr << "[ERROR] Failed to initialize semaphore" << endl;
        return 1;
    }
    cout << "Mail Server is running on port " << SERVER_PORT << "..." << endl;

    while (!shutting_down) {
        sem_wait(connection_semaphore);
        int *client_fd = new int;
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        *client_fd = accept(server_socket, (sockaddr *)&client_addr, &addr_len);
        if (*client_fd < 0) {
            cerr << "[ERROR] Failed to accept connection." << endl;
            delete client_fd;
            continue;
        }
        pthread_mutex_lock(&client_mutex);
        client_sockets.push_back(*client_fd);
        pthread_mutex_unlock(&client_mutex);

        pthread_t client_thread;
        if (pthread_create(&client_thread, nullptr, handle_client, client_fd) != 0) {
            cerr << "[ERROR] Failed to create thread for new client." << endl;
            pthread_mutex_lock(&client_mutex);
            client_sockets.erase(remove(client_sockets.begin(), client_sockets.end(), *client_fd), client_sockets.end());
            close(*client_fd);
            delete client_fd;
            pthread_mutex_unlock(&client_mutex);
            continue;
        }
        pthread_detach(client_thread);
    }
    close(server_socket);
    return 0;
}
