#include "include/utils.h"
#include <string>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>

extern int kv_socket;
extern pthread_mutex_t kv_mutex;

#define KV_SERVER_PORT 5050
#define KV_SERVER_IP "127.0.0.1"

namespace Utils {

bool connect_to_kvstore() {
    pthread_mutex_lock(&kv_mutex);
    
    if (kv_socket >= 0) {
        pthread_mutex_unlock(&kv_mutex);
        return true;
    }
    
    kv_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (kv_socket < 0) {
        fprintf(stderr, "[connect_to_kvstore] KVStore socket creation failed\n");
        pthread_mutex_unlock(&kv_mutex);
        return false;
    }
    
    struct sockaddr_in kv_addr;
    memset(&kv_addr, 0, sizeof(kv_addr));
    kv_addr.sin_family = AF_INET;
    kv_addr.sin_port = htons(KV_SERVER_PORT);
    
    if (inet_pton(AF_INET, KV_SERVER_IP, &kv_addr.sin_addr) <= 0) {
        fprintf(stderr, "[connect_to_kvstore] Invalid KVStore server address\n");
        close(kv_socket);
        kv_socket = -1;
        pthread_mutex_unlock(&kv_mutex);
        return false;
    }
    
    if (connect(kv_socket, (struct sockaddr*)&kv_addr, sizeof(kv_addr)) < 0) {
        fprintf(stderr, "[connect_to_kvstore] KVStore connection failed\n");
        close(kv_socket);
        kv_socket = -1;
        pthread_mutex_unlock(&kv_mutex);
        return false;
    }
    
    char greeting[1024];
    memset(greeting, 0, sizeof(greeting));
    int bytes = recv(kv_socket, greeting, sizeof(greeting) - 1, 0);
    if (bytes > 0) {
        greeting[bytes] = '\0';
        fprintf(stderr, "[connect_to_kvstore] Master greeting: %s", greeting);
        
        if (strstr(greeting, "+OK Master ready") == NULL) {
            fprintf(stderr, "[connect_to_kvstore] Unexpected greeting from KVStore server: %s\n", greeting);
            close(kv_socket);
            kv_socket = -1;
            pthread_mutex_unlock(&kv_mutex);
            return false;
        }
    } else {
        fprintf(stderr, "[connect_to_kvstore] Failed to receive greeting from KVStore\n");
        close(kv_socket);
        kv_socket = -1;
        pthread_mutex_unlock(&kv_mutex);
        return false;
    }
    
    fprintf(stderr, "[connect_to_kvstore] Connected to KVStore server at %s:%d\n", KV_SERVER_IP, KV_SERVER_PORT);
    pthread_mutex_unlock(&kv_mutex);
    return true;
}

std::string kvstore_command(const std::string& command) {
    pthread_mutex_lock(&kv_mutex);
    
    if (kv_socket < 0) {
        pthread_mutex_unlock(&kv_mutex);
        if (!connect_to_kvstore()) {
            return "error: Could not connect to KVStore";
        }
        pthread_mutex_lock(&kv_mutex);
    }
    
    fprintf(stderr, "[kvstore_command] Sending to Master: %s", command.c_str());
    if (send(kv_socket, command.c_str(), command.length(), 0) < 0) {
        fprintf(stderr, "[kvstore_command] Failed to send command to Master\n");
        close(kv_socket);
        kv_socket = -1;
        pthread_mutex_unlock(&kv_mutex);
        return "error: Failed to communicate with Master";
    }
    
    char response[1024];
    memset(response, 0, sizeof(response));
    int bytes = recv(kv_socket, response, sizeof(response) - 1, 0);
    
    if (bytes <= 0) {
        fprintf(stderr, "[kvstore_command] Failed to receive response from Master\n");
        close(kv_socket);
        kv_socket = -1;
        pthread_mutex_unlock(&kv_mutex);
        return "error: No response from Master";
    }
    
    response[bytes] = '\0';
    fprintf(stderr, "[kvstore_command] Received from Master: %s", response);
    
    pthread_mutex_unlock(&kv_mutex);
    return std::string(response);
}

std::string get_tablet_for_username(const std::string& username) {
    std::string ask_command = "ASK " + username + "\r\n";

    std::string coordinator_response = kvstore_command(ask_command);
    
    // Check for ALL DEAD error condition
    if (coordinator_response.find("-ERR ALL DEAD") == 0) {
        fprintf(stderr, "[get_tablet_for_username] All nodes are dead for this shard. Service is down.\n");
        return "SERVICE_DOWN";
    }
    
    if (coordinator_response.find("+OK REDIRECT") != 0) {
        return "";
    }
    
    std::string tablet_address = coordinator_response.substr(13);
    if (tablet_address.length() >= 2 && tablet_address.substr(tablet_address.length() - 2) == "\r\n") {
        tablet_address = tablet_address.substr(0, tablet_address.length() - 2);
    }
    
    fprintf(stderr, "[get_tablet_for_username] Tablet server address: [%s]\n", tablet_address.c_str());
    return tablet_address;
}

} // namespace Utils