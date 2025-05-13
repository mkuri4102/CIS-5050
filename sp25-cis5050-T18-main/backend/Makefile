CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread

MASTER_BIN  = master
TABLET_BIN  = tablet

MASTER_SRCS = master.cpp
TABLET_SRCS = tablet.cpp

.PHONY: all clean

all: $(MASTER_BIN) $(TABLET_BIN)

$(MASTER_BIN): $(MASTER_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TABLET_BIN): $(TABLET_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(MASTER_BIN) $(TABLET_BIN) checkpoint_* log_*
