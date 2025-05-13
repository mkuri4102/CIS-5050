#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <openssl/md5.h>

#define DEFAULT_PORT 11000
#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS 100

int server_socket;
int verbose = 0;
char *mailbox_dir = NULL; // Mailbox directory passed as non-option argument

// Struct for passing client info to threads
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_info;

// Structure for a POP3 message
typedef struct {
    char *data;      // Full text of the message (including "From " header)
    int size;        // Size in bytes
    int deleted;     // 0 = not deleted, 1 = marked for deletion
    char uidl[33];   // MD5 digest of message data as 32-hex-digit string + '\0'
} pop3_message;

// Per-session POP3 state
typedef struct {
    int logged_in;
    char username[256];
    pop3_message *messages;
    int num_messages;
    char mbox_path[512]; // Full path to the user's mailbox file
} pop3_session;

void* handle_client(void* arg);
void cleanup_and_exit(int signum);

// Helper: trim leading and trailing whitespace
char *trim_whitespace(char *str) {
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = '\0';
    return str;
}

// Load the mailbox file for the logged-in user and split it into messages.
// Each message is assumed to start with a line beginning with "From ".
int load_mailbox(pop3_session *session) {
    FILE *fp = fopen(session->mbox_path, "r");
    if (!fp) return -1;

    // Read entire file into memory.
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    rewind(fp);
    char *filedata = (char *)malloc(filesize + 1);
    if (!filedata) { fclose(fp); return -1; }
    fread(filedata, 1, filesize, fp);
    filedata[filesize] = '\0';
    fclose(fp);

    pop3_message *msgs = NULL;
    int msg_count = 0;
    char *line;
    char *saveptr;
    char *current_msg = NULL;
    int current_size = 0;

    // Tokenize filedata by newline.
    line = strtok_r(filedata, "\n", &saveptr);
    while(line != NULL) {
        // A new message starts when a line begins with "From ".
        if (strncmp(line, "From ", 5) == 0) {
            if (current_msg != NULL) {
                // Finish the current message.
            	msgs = (pop3_message *)realloc(msgs, (msg_count + 1) * sizeof(pop3_message));
                msgs[msg_count].data = current_msg;
                msgs[msg_count].size = current_size;
                msgs[msg_count].deleted = 0;
                // Compute MD5 digest for UIDL.
                unsigned char digest[MD5_DIGEST_LENGTH];
                MD5_CTX ctx;
                MD5_Init(&ctx);
                MD5_Update(&ctx, current_msg, current_size);
                MD5_Final(digest, &ctx);
                for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
                    sprintf(&(msgs[msg_count].uidl[i*2]), "%02x", digest[i]);
                msgs[msg_count].uidl[32] = '\0';
                msg_count++;
                current_msg = NULL;
                current_size = 0;
            }
            // Start a new message with the "From " line.
            int line_len = strlen(line);
            current_msg = (char *) malloc(line_len + 2);
            if (!current_msg) { free(filedata); free(msgs); return -1; }
            strcpy(current_msg, line);
            strcat(current_msg, "\n");
            current_size = line_len + 1;
        } else {
            if (current_msg) {
                int line_len = strlen(line);
                current_msg = (char *)realloc(current_msg, current_size + line_len + 2);
                strcat(current_msg, line);
                strcat(current_msg, "\n");
                current_size += line_len + 1;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    // Add the last message if any.
    if (current_msg != NULL) {
    	msgs = (pop3_message *)realloc(msgs, (msg_count + 1) * sizeof(pop3_message));
        msgs[msg_count].data = current_msg;
        msgs[msg_count].size = current_size;
        msgs[msg_count].deleted = 0;
        unsigned char digest[MD5_DIGEST_LENGTH];
        MD5_CTX ctx;
        MD5_Init(&ctx);
        MD5_Update(&ctx, current_msg, current_size);
        MD5_Final(digest, &ctx);
        for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
            sprintf(&(msgs[msg_count].uidl[i*2]), "%02x", digest[i]);
        msgs[msg_count].uidl[32] = '\0';
        msg_count++;
    }
    free(filedata);
    session->messages = msgs;
    session->num_messages = msg_count;
    return 0;
}

// Rewrite the mailbox file, omitting messages marked as deleted.
int update_mailbox(pop3_session *session) {
    FILE *fp = fopen(session->mbox_path, "w");
    if (!fp) return -1;
    for (int i = 0; i < session->num_messages; i++) {
        if (!session->messages[i].deleted)
            fwrite(session->messages[i].data, 1, session->messages[i].size, fp);
    }
    fclose(fp);
    return 0;
}

void free_messages(pop3_session *session) {
    if (session->messages) {
        for (int i = 0; i < session->num_messages; i++)
            free(session->messages[i].data);
        free(session->messages);
        session->messages = NULL;
        session->num_messages = 0;
    }
}

void* handle_client(void* arg) {
    client_info *cinfo = (client_info*) arg;
    int client_fd = cinfo->client_fd;
    free(cinfo);

    char recv_buffer[BUFFER_SIZE] = {0};
    int bytes_read;
    pop3_session session;
    memset(&session, 0, sizeof(session));
    session.logged_in = 0;
    session.messages = NULL;
    session.num_messages = 0;

    // Send initial greeting.
    const char *greeting = "+OK POP3 ready [localhost]\r\n";
    send(client_fd, greeting, strlen(greeting), 0);
    if (verbose) printf("[%d] S: %s", client_fd, greeting);

    while ((bytes_read = recv(client_fd, recv_buffer, sizeof(recv_buffer)-1, 0)) > 0) {
        recv_buffer[bytes_read] = '\0';
        // Process each command line (assume commands end with "\r\n").
        char *line = strtok(recv_buffer, "\r\n");
        while (line != NULL) {
            // Parse command and its argument.
            char *cmd = strtok(line, " ");
            if (!cmd) { line = strtok(NULL, "\r\n"); continue; }
            for (int i = 0; cmd[i]; i++) cmd[i] = toupper(cmd[i]);
            if (strcmp(cmd, "USER") == 0) {
                char *arg = strtok(NULL, " ");
                if (!arg) {
                    send(client_fd, "-ERR Missing username\r\n", strlen("-ERR Missing username\r\n"), 0);
                } else {
                    snprintf(session.username, sizeof(session.username), "%s", arg);
                    // Build mailbox file path.
                    snprintf(session.mbox_path, sizeof(session.mbox_path), "%s/%s.mbox", mailbox_dir, session.username);
                    if (access(session.mbox_path, F_OK) != 0)
                        send(client_fd, "-ERR No such mailbox\r\n", strlen("-ERR No such mailbox\r\n"), 0);
                    else
                        send(client_fd, "+OK User accepted\r\n", strlen("+OK User accepted\r\n"), 0);
                }
            }  else if (strcmp(cmd, "PASS") == 0) {
                // Get the rest of the line as the password (allowing spaces)
                char *arg = strtok(NULL, "\r\n");
                if (!arg) {
                    send(client_fd, "-ERR Missing password\r\n", strlen("-ERR Missing password\r\n"), 0);
                } else {
                    // Check if USER has been provided first.
                    if (session.username[0] == '\0') {
                        send(client_fd, "-ERR USER required before PASS\r\n", strlen("-ERR USER required before PASS\r\n"), 0);
                    } else if (session.logged_in) {
                        // Already authenticated.
                        send(client_fd, "-ERR maildrop already locked\r\n", strlen("-ERR maildrop already locked\r\n"), 0);
                    } else {
                        // Check the provided password.
                        if (strcmp(arg, "cis505") != 0) {
                            send(client_fd, "-ERR Invalid password\r\n", strlen("-ERR Invalid password\r\n"), 0);
                        } else {
                            // Attempt to load the mailbox.
                            if (load_mailbox(&session) != 0) {
                                send(client_fd, "-ERR unable to lock maildrop\r\n", strlen("-ERR unable to lock maildrop\r\n"), 0);
                            } else {
                                session.logged_in = 1;
                                // Compute the count and total size of the messages.
                                int count = 0, total = 0;
                                for (int i = 0; i < session.num_messages; i++) {
                                    if (!session.messages[i].deleted) {
                                        // Optionally, subtract the mbox header size from each message.
                                        char *msg_data = session.messages[i].data;
                                        char *first_newline = strchr(msg_data, '\n');
                                        int actual_size = (first_newline) ? session.messages[i].size - (first_newline - msg_data + 1)
                                                                           : session.messages[i].size;
                                        count++;
                                        total += actual_size;
                                    }
                                }
                                char response[BUFFER_SIZE];
                                snprintf(response, sizeof(response),
                                         "+OK %s's maildrop has %d messages (%d octets)\r\n",
                                         session.username, count, total);
                                send(client_fd, response, strlen(response), 0);
                            }
                        }
                    }
                }
            } else if (strcmp(cmd, "STAT") == 0) {
                if (!session.logged_in) {
                    send(client_fd, "-ERR Not authenticated\r\n", strlen("-ERR Not authenticated\r\n"), 0);
                } else {
                    int count = 0, total = 0;
                    for (int i = 0; i < session.num_messages; i++) {
                        if (!session.messages[i].deleted) {
                            char *msg_data = session.messages[i].data;
                            // Find the end of the mbox header (the first newline).
                            char *newline = strchr(msg_data, '\n');
                            // If a newline is found, subtract the header from the total size.
                            int msg_size = (newline) ? session.messages[i].size - (newline - msg_data + 1)
                                                     : session.messages[i].size;
                            count++;
                            total += msg_size;
                        }
                    }
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "+OK %d %d\r\n", count, total);
                    send(client_fd, response, strlen(response), 0);
                }
            } else if (strcmp(cmd, "LIST") == 0) {
                char *arg = strtok(NULL, " ");
                if (arg) {
                    int num = atoi(arg);
                    if (num < 1 || num > session.num_messages || session.messages[num-1].deleted)
                        send(client_fd, "-ERR No such message\r\n", 23, 0);
                    else {
                        char response[BUFFER_SIZE];
                        snprintf(response, sizeof(response), "+OK %d %d\r\n", num, session.messages[num-1].size);
                        send(client_fd, response, strlen(response), 0);
                    }
                } else {
                    int count = 0, total = 0;
                    for (int i = 0; i < session.num_messages; i++) {
                        if (!session.messages[i].deleted) {
                            count++;
                            total += session.messages[i].size;
                        }
                    }
                    char header[BUFFER_SIZE];
                    snprintf(header, sizeof(header), "+OK %d messages (%d octets)\r\n", count, total);
                    send(client_fd, header, strlen(header), 0);
                    for (int i = 0; i < session.num_messages; i++) {
                        if (!session.messages[i].deleted) {
                            char linebuf[BUFFER_SIZE];
                            snprintf(linebuf, sizeof(linebuf), "%d %d\r\n", i+1, session.messages[i].size);
                            send(client_fd, linebuf, strlen(linebuf), 0);
                        }
                    }
                    send(client_fd, ".\r\n", 3, 0);
                }
            } else if (strcmp(cmd, "UIDL") == 0) {
                char *arg = strtok(NULL, " ");
                if (arg) {
                    int num = atoi(arg);
                    if (num < 1 || num > session.num_messages || session.messages[num-1].deleted)
                        send(client_fd, "-ERR No such message\r\n", 23, 0);
                    else {
                        char response[BUFFER_SIZE];
                        snprintf(response, sizeof(response), "+OK %d %s\r\n", num, session.messages[num-1].uidl);
                        send(client_fd, response, strlen(response), 0);
                    }
                } else {
                    send(client_fd, "+OK\r\n", 5, 0);
                    for (int i = 0; i < session.num_messages; i++) {
                        if (!session.messages[i].deleted) {
                            char linebuf[BUFFER_SIZE];
                            snprintf(linebuf, sizeof(linebuf), "%d %s\r\n", i+1, session.messages[i].uidl);
                            send(client_fd, linebuf, strlen(linebuf), 0);
                        }
                    }
                    send(client_fd, ".\r\n", 3, 0);
                }
            } else if (strcmp(cmd, "RETR") == 0) {
                char *arg = strtok(NULL, " ");
                if (!arg) {
                    send(client_fd, "-ERR Missing message number\r\n", strlen("-ERR Missing message number\r\n"), 0);
                } else {
                    int num = atoi(arg);
                    if (num < 1 || num > session.num_messages || session.messages[num-1].deleted) {
                        send(client_fd, "-ERR No such message\r\n", strlen("-ERR No such message\r\n"), 0);
                    } else {
                        send(client_fd, "+OK message follows\r\n", strlen("+OK message follows\r\n"), 0);

                        // Retrieve stored message.
                        char *msg_data = session.messages[num-1].data;
                        // Skip mbox header if present.
                        if (strncmp(msg_data, "From ", 5) == 0) {
                            char *header_end = strchr(msg_data, '\n');
                            if (header_end)
                                msg_data = header_end + 1;
                        }

                        // Process message data line by line.
                        char *start = msg_data;
                        while (*start != '\0') {
                            char *end = strchr(start, '\n');
                            int line_len = (end) ? (end - start) : strlen(start);
                            char line_buf[BUFFER_SIZE];
                            if (line_len >= BUFFER_SIZE)
                                line_len = BUFFER_SIZE - 1;
                            strncpy(line_buf, start, line_len);
                            line_buf[line_len] = '\0';

                            // If the line is empty or is exactly "\r", treat it as an empty line.
                            if (line_len == 0 || (line_len == 1 && line_buf[0] == '\r')) {
                                // Send the literal "''" with no CRLF.
                                send(client_fd, "''", strlen("''"), 0);
                            } else {
                                char out_line[BUFFER_SIZE];
                                if (line_buf[0] == '.')
                                    snprintf(out_line, sizeof(out_line), ".%s\r\n", line_buf);
                                else
                                    snprintf(out_line, sizeof(out_line), "%s\r\n", line_buf);
                                send(client_fd, out_line, strlen(out_line), 0);
                            }
                            if (end)
                                start = end + 1;
                            else
                                break;
                        }
                        // Terminate multi-line response.
                        send(client_fd, ".\r\n", 3, 0);
                    }
                }
            } else if (strcmp(cmd, "DELE") == 0) {
                char *arg = strtok(NULL, " ");
                if (!arg)
                    send(client_fd, "-ERR Missing message number\r\n", 31, 0);
                else {
                    int num = atoi(arg);
                    if (num < 1 || num > session.num_messages || session.messages[num-1].deleted)
                        send(client_fd, "-ERR No such message\r\n", 23, 0);
                    else {
                        session.messages[num-1].deleted = 1;
                        char response[BUFFER_SIZE];
                        snprintf(response, sizeof(response), "+OK message %d deleted\r\n", num);
                        send(client_fd, response, strlen(response), 0);
                    }
                }
            } else if (strcmp(cmd, "RSET") == 0) {
                for (int i = 0; i < session.num_messages; i++)
                    session.messages[i].deleted = 0;
                char response[BUFFER_SIZE];
                snprintf(response, sizeof(response), "+OK maildrop has %d messages\r\n", session.num_messages);
                send(client_fd, response, strlen(response), 0);
            } else if (strcmp(cmd, "NOOP") == 0) {
                send(client_fd, "+OK\r\n", 6, 0);
            } else if (strcmp(cmd, "QUIT") == 0) {
                if (session.logged_in) {
                    update_mailbox(&session);
                    free_messages(&session);
                    send(client_fd, "+OK POP3 server signing off\r\n", 31, 0);
                } else {
                    send(client_fd, "+OK POP3 server signing off\r\n", 31, 0);
                }
                close(client_fd);
                return NULL;
            } else {
                send(client_fd, "-ERR Not supported\r\n", strlen("-ERR Not supported\r\n"), 0);
            }
            line = strtok(NULL, "\r\n");
        }
    }
    if (session.logged_in)
        free_messages(&session);
    close(client_fd);
    return NULL;
}

void cleanup_and_exit(int signum) {
    printf("Shutting down server...\n");
    close(server_socket);
    exit(0);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    struct sockaddr_in server_addr;

    // Parse command-line arguments: -p <port>, -a, -v, and the mailbox directory.
    int i = 1;
    while(i < argc) {
        if(strcmp(argv[i], "-p") == 0 && i+1 < argc) {
            port = atoi(argv[i+1]);
            i += 2;
        } else if(strcmp(argv[i], "-a") == 0) {
            fprintf(stderr, "Minna Kuriakose - mkuri\n");
            exit(0);
        } else if(strcmp(argv[i], "-v") == 0) {
            verbose = 1;
            i++;
        } else {
            mailbox_dir = argv[i];
            i++;
        }
    }
    if(mailbox_dir == NULL) {
        fprintf(stderr, "Usage: %s [options] <mailbox_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, cleanup_and_exit);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if(bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if(listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    if(verbose) {
        printf("POP3 Server listening on port %d\n", port);
    }
    while(1) {
    	client_info *cinfo = (client_info *) malloc(sizeof(client_info));
        socklen_t addr_len = sizeof(cinfo->client_addr);
        cinfo->client_fd = accept(server_socket, (struct sockaddr*)&cinfo->client_addr, &addr_len);
        if(cinfo->client_fd < 0) {
            perror("Accept failed");
            free(cinfo);
            continue;
        }
        if(verbose) {
            printf("[%d] New connection\n", cinfo->client_fd);
        }
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, cinfo);
        pthread_detach(thread);
    }
    return 0;
}
