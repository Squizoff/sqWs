# sqWs Makefile

CC = clang
CFLAGS = -Wall -Iinclude -I/usr/include/libdrm
LDFLAGS = -ldrm -lm

SRC_DIR = src
BIN_DIR = bin

SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client

SERVER_SOURCES = $(wildcard $(SERVER_DIR)/*.c)
CLIENT_SOURCES = $(wildcard $(CLIENT_DIR)/*.c)

all: $(BIN_DIR)/sqws $(BIN_DIR)/client

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/sqws: $(SERVER_SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -O2 $(SERVER_SOURCES) -o $@ $(LDFLAGS)

$(BIN_DIR)/client: $(CLIENT_SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -O2 $(CLIENT_SOURCES) -o $@ $(LDFLAGS)

clean:
	rm -f $(BIN_DIR)/sqws $(BIN_DIR)/client
