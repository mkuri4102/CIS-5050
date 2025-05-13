TARGET = mail_server

all: $(TARGET)

$(TARGET): mail_server.cc
	g++ mail_server.cc -std=c++17 -lpthread -lresolv -g -o $(TARGET)

clean:
	rm -fv $(TARGET) *~
