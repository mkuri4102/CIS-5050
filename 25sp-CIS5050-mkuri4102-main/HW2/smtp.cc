#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEFAULT_PORT 2500
#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS 100
#define MAX_RECIPIENTS 10
#define MAX_EMAIL_DATA 65536

int server_socket;
int verbose = 0;
char *mailbox_dir = NULL; // Mailbox directory (required on command line)

// Struct for passing client info to threads
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_info;

void* handle_client(void* arg);
void cleanup_and_exit(int signum);

// Helper: trim leading/trailing whitespace (used for commands only)
char* trim_whitespace(char *str) {
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0)
        return str;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    *(end+1) = '\0';
    return str;
}

// Helper: parse a recipient address (should be "<user@localhost>")
// If valid, extracts the local part (user) into username.
int parse_recipient(const char* input, char *username, size_t username_size) {
    if (input[0] != '<') return 0;
    const char *at = strchr(input, '@');
    if (!at) return 0;
    // Check that domain is "localhost"
    const char *domain_start = at + 1;
    size_t domain_len = strlen("localhost");
    if (strncmp(domain_start, "localhost", domain_len) != 0) return 0;
    const char *end = strchr(domain_start, '>');
    if (!end) return 0;
    size_t user_len = at - (input + 1);
    if (user_len >= username_size) return 0;
    strncpy(username, input + 1, user_len);
    username[user_len] = '\0';
    return 1;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    struct sockaddr_in server_addr;

    // Parse command-line arguments.
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[i+1]);
            i += 2;
        } else if (strcmp(argv[i], "-a") == 0) {
            fprintf(stderr, "Minna Kuriakose - mkuri\n");
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
            i++;
        } else {
            // Assume non-option argument is the mailbox directory.
            mailbox_dir = argv[i];
            i++;
        }
    }
    if (mailbox_dir == NULL) {
        fprintf(stderr, "Usage: %s [options] <mailbox_directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Set up SIGINT handler.
    signal(SIGINT, cleanup_and_exit);

    // Create server socket.
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_socket, MAX_CONNECTIONS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    if (verbose) {
        printf("SMTP Server listening on port %d\n", port);
    }
    // Main accept loop for concurrent connections.
    while (1) {
        client_info* cinfo = (client_info*) malloc(sizeof(client_info));
        if (!cinfo) {
            perror("Memory allocation failed");
            continue;
        }
        socklen_t addr_len = sizeof(cinfo->client_addr);
        cinfo->client_fd = accept(server_socket, (struct sockaddr*)&cinfo->client_addr, &addr_len);
        if (cinfo->client_fd < 0) {
            perror("Accept failed");
            free(cinfo);
            continue;
        }
        if (verbose) {
            printf("[%d] New connection\n", cinfo->client_fd);
        }
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, cinfo);
        pthread_detach(thread);
    }
    return 0;
}

void* handle_client(void* arg) {
    client_info* cinfo = (client_info*) arg;
    int client_fd = cinfo->client_fd;
    free(cinfo);

    char recv_buffer[BUFFER_SIZE] = {0};
    int in_data_mode = 0; // Flag for DATA mode

    // Transaction state:
    char sender[256] = {0};
    char recipients[MAX_RECIPIENTS][256];
    int recipient_count = 0;

    // Buffer for email data.
    char email_data[MAX_EMAIL_DATA];
    int email_data_len = 0;
    email_data[0] = '\0';

    // Send the initial SMTP greeting.
    const char *greeting = "220 localhost Service ready\r\n";
    send(client_fd, greeting, strlen(greeting), 0);
    if (verbose) {
        printf("[%d] S: %s", client_fd, greeting);
    }

    while (1) {
        char buffer[BUFFER_SIZE] = {0};
        int bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) {
            if (verbose) {
                printf("[%d] Connection closed by client\n", client_fd);
            }
            break;
        }
        buffer[bytes_read] = '\0';
        strncat(recv_buffer, buffer, sizeof(recv_buffer) - strlen(recv_buffer) - 1);

        char* line_end;
        // Process each complete line (terminated with "\r\n")
        while ((line_end = strstr(recv_buffer, "\r\n")) != NULL) {
            *line_end = '\0';
            char *line;
            if (in_data_mode) {
                // Use the raw line to preserve any spaces.
                line = recv_buffer;
            } else {
                line = trim_whitespace(recv_buffer);
            }

            if (in_data_mode) {
                // DATA mode: a line with a single dot ends the input.
                if (strcmp(line, ".") == 0) {
                    time_t now = time(NULL);
                    struct tm *tm_info = localtime(&now);
                    char date_str[128];
                    strftime(date_str, sizeof(date_str), "%a %b %d %H:%M:%S %Y", tm_info);
                    for (int i = 0; i < recipient_count; i++) {
                        char mbox_path[512];
                        snprintf(mbox_path, sizeof(mbox_path), "%s/%s.mbox", mailbox_dir, recipients[i]);
                        FILE *fp = fopen(mbox_path, "a");
                        if (fp) {
                            fprintf(fp, "From <%s> %s\n", sender, date_str);
                            fwrite(email_data, 1, email_data_len, fp);
                            if (email_data_len == 0 || email_data[email_data_len - 1] != '\n')
                                fprintf(fp, "\n");
                            fclose(fp);
                        } else {
                            if (verbose) {
                                printf("[%d] Error opening mailbox file: %s\n", client_fd, mbox_path);
                            }
                        }
                    }
                    const char *data_end = "250 OK\r\n";
                    send(client_fd, data_end, strlen(data_end), 0);
                    if (verbose) {
                        printf("[%d] S: %s", client_fd, data_end);
                    }
                    // Reset transaction state.
                    sender[0] = '\0';
                    recipient_count = 0;
                    email_data_len = 0;
                    email_data[0] = '\0';
                    in_data_mode = 0;
                } else {
                    // Append the raw line (with spaces) to email_data.
                    int line_len = strlen(line);
                    if (email_data_len + line_len + 1 < MAX_EMAIL_DATA) {
                        memcpy(email_data + email_data_len, line, line_len);
                        email_data_len += line_len;
                        email_data[email_data_len++] = '\n';
                        email_data[email_data_len] = '\0';
                    } else {
                        const char *err = "552 Requested mail action aborted: exceeded storage allocation\r\n";
                        send(client_fd, err, strlen(err), 0);
                        if (verbose) {
                            printf("[%d] S: %s", client_fd, err);
                        }
                        in_data_mode = 0;
                        sender[0] = '\0';
                        recipient_count = 0;
                        email_data_len = 0;
                        email_data[0] = '\0';
                    }
                }
            } else {
                // Process SMTP commands.
                if (strncasecmp(line, "HELO ", 5) == 0) {
                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), "250 localhost\r\n");
                    send(client_fd, response, strlen(response), 0);
                    if (verbose)
                        printf("[%d] S: %s", client_fd, response);
                } else if (strncasecmp(line, "MAIL FROM:", 10) == 0) {
                    char *arg = trim_whitespace(line + 10);
                    strncpy(sender, arg, sizeof(sender) - 1);
                    sender[sizeof(sender) - 1] = '\0';
                    const char *resp = "250 OK\r\n";
                    send(client_fd, resp, strlen(resp), 0);
                    if (verbose)
                        printf("[%d] S: %s", client_fd, resp);
                } else if (strncasecmp(line, "RCPT TO:", 8) == 0) {
                    char *arg = trim_whitespace(line + 8);
                    char username[256];
                    if (!parse_recipient(arg, username, sizeof(username))) {
                        const char *err = "550 No such user\r\n";
                        send(client_fd, err, strlen(err), 0);
                        if (verbose)
                            printf("[%d] S: %s", client_fd, err);
                    } else {
                        char mbox_path[512];
                        snprintf(mbox_path, sizeof(mbox_path), "%s/%s.mbox", mailbox_dir, username);
                        if (access(mbox_path, F_OK) != 0) {
                            const char *err = "550 No such user\r\n";
                            send(client_fd, err, strlen(err), 0);
                            if (verbose)
                                printf("[%d] S: %s", client_fd, err);
                        } else {
                            if (recipient_count < MAX_RECIPIENTS) {
                                strncpy(recipients[recipient_count], username, sizeof(recipients[recipient_count]) - 1);
                                recipients[recipient_count][sizeof(recipients[recipient_count]) - 1] = '\0';
                                recipient_count++;
                                const char *resp = "250 OK\r\n";
                                send(client_fd, resp, strlen(resp), 0);
                                if (verbose)
                                    printf("[%d] S: %s", client_fd, resp);
                            } else {
                                const char *err = "452 Too many recipients\r\n";
                                send(client_fd, err, strlen(err), 0);
                                if (verbose)
                                    printf("[%d] S: %s", client_fd, err);
                            }
                        }
                    }
                } else if (strcasecmp(line, "DATA") == 0) {
                    if (strlen(sender) == 0 || recipient_count == 0) {
                        const char *err = "503 Bad sequence of commands\r\n";
                        send(client_fd, err, strlen(err), 0);
                        if (verbose)
                            printf("[%d] S: %s", client_fd, err);
                    } else {
                        const char *data_prompt = "354 End data with <CR><LF>.<CR><LF>\r\n";
                        send(client_fd, data_prompt, strlen(data_prompt), 0);
                        if (verbose)
                            printf("[%d] S: %s", client_fd, data_prompt);
                        in_data_mode = 1;
                        email_data_len = 0;
                        email_data[0] = '\0';
                    }
                } else if (strcasecmp(line, "RSET") == 0) {
                    sender[0] = '\0';
                    recipient_count = 0;
                    email_data_len = 0;
                    email_data[0] = '\0';
                    const char *resp = "250 OK\r\n";
                    send(client_fd, resp, strlen(resp), 0);
                    if (verbose)
                        printf("[%d] S: %s", client_fd, resp);
                } else if (strcasecmp(line, "NOOP") == 0) {
                    const char *resp = "250 OK\r\n";
                    send(client_fd, resp, strlen(resp), 0);
                    if (verbose)
                        printf("[%d] S: %s", client_fd, resp);
                } else if (strcasecmp(line, "QUIT") == 0) {
                    const char *quit_resp = "221 localhost Service closing transmission channel\r\n";
                    send(client_fd, quit_resp, strlen(quit_resp), 0);
                    if (verbose)
                        printf("[%d] S: %s", client_fd, quit_resp);
                    close(client_fd);
                    return NULL;
                } else {
                    const char *err_resp = "500 Syntax error, command unrecognized\r\n";
                    send(client_fd, err_resp, strlen(err_resp), 0);
                    if (verbose)
                        printf("[%d] S: %s", client_fd, err_resp);
                }
            }
            // Shift any remaining data forward.
            memmove(recv_buffer, line_end + 2, strlen(line_end + 2) + 1);
        }
    }
    close(client_fd);
    return NULL;
}

void cleanup_and_exit(int signum) {
    printf("Shutting down server...\n");
    close(server_socket);
    exit(0);
}
