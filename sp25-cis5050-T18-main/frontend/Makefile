CC = g++
CFLAGS = -std=c++17 -Wall -Wextra -pedantic -I. -pthread

SRC_FILES = http_server.cpp \
       src/utils/http_utils.cpp \
       src/utils/kvstore_utils.cpp \
       src/utils/tablet_utils.cpp \
       src/utils/cookies_utils.cpp \
       src/utils/email_utils.cpp \
       src/webstorage/webstorage.cpp \
       src/webstorage/webstorage_file.cpp \
       src/webstorage/webstorage_handler.cpp \
       src/handlers/get_handler.cpp \
       src/handlers/post_handler.cpp \
       src/handlers/head_handler.cpp \
       src/handlers/handler_utils.cpp \
       src/routes/handle_login.cpp \
       src/routes/handle_signup.cpp \
       src/routes/handle_reset_password.cpp \
       src/routes/handle_logout.cpp \
       src/routes/handle_get_emails.cpp \
       src/routes/handle_send_email.cpp \
       src/routes/handle_forward_email.cpp \
       src/routes/handle_reply_email.cpp \
       src/routes/handle_delete_email.cpp \
       src/routes/handle_server_id.cpp \
       src/routes/handle_admin_db.cpp \
       src/routes/handle_admin_nodes.cpp \
       src/routes/handle_kill_node.cpp \
       src/routes/handle_restart_node.cpp

OBJS = $(patsubst %.cpp,build/%.o,$(SRC_FILES))
SERVER_TARGET = http_server
LB_TARGET = load_balancer

BUILD_DIRS = build \
             build/src \
             build/src/utils \
             build/src/handlers \
             build/src/routes \
             build/src/webstorage

all: $(BUILD_DIRS) $(SERVER_TARGET) $(LB_TARGET)

$(BUILD_DIRS):
	mkdir -p $@

$(SERVER_TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(LB_TARGET): load_balancer.cpp
	$(CC) $(CFLAGS) -o $@ $<

build/%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(SERVER_TARGET) $(LB_TARGET)
	rm -rf build
	rm -f src/utils.o  # Remove any stale object file

run: $(SERVER_TARGET)
	./$(SERVER_TARGET)

run_lb: all
	chmod +x start_servers.sh
	./start_servers.sh 3

.PHONY: all clean run run_lb $(BUILD_DIRS)



