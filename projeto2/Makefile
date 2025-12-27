# Compiler variables
CC = gcc
CFLAGS = -g -Wall -Wextra -std=c17 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lncurses


# Directory variables
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

SERVER_SRC_DIR = $(SRC_DIR)/server
CLIENT_SRC_DIR = $(SRC_DIR)/client

SERVER_OBJ_DIR = $(OBJ_DIR)/server
CLIENT_OBJ_DIR = $(OBJ_DIR)/client


# executables
SERVER_TARGET = PacmanIST
CLIENT_TARGET = client

#Client objects
OBJS_CLIENT = client_main.o debug.o api.o display.o

#Server objects
OBJS_SERVER = board.o parser.o server.o

# Dependencies
display.o = display.h
board.o = board.h
parser.o = parser.h
api.o = api.h protocol.h

# Object files path
vpath %.c $(SRC_DIR) $(SERVER_SRC_DIR) $(CLIENT_SRC_DIR)
vpath %.o $(SERVER_OBJ_DIR) $(CLIENT_OBJ_DIR)


# Make targets
all: server client
server: $(BIN_DIR)/$(SERVER_TARGET)
client: $(BIN_DIR)/$(CLIENT_TARGET)

$(BIN_DIR)/$(SERVER_TARGET): $(addprefix $(SERVER_OBJ_DIR)/,$(OBJS_SERVER)) | folders
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/$(CLIENT_TARGET): $(addprefix $(CLIENT_OBJ_DIR)/,$(OBJS_CLIENT)) | folders
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)


# dont include LDFLAGS in the end, to allow compilation on macos
$(SERVER_OBJ_DIR)/%.o: %.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@

$(CLIENT_OBJ_DIR)/%.o: %.c | folders
	$(CC) -I $(INCLUDE_DIR) $(CFLAGS) -c $< -o $@


# Create folders
folders:
	mkdir -p $(SERVER_OBJ_DIR)
	mkdir -p $(CLIENT_OBJ_DIR)
	mkdir -p $(BIN_DIR)

# Clean object files and executable
clean:
	rm -f $(SERVER_OBJ_DIR)/*.o
	rm -f $(CLIENT_OBJ_DIR)/*.o
	rm -f $(BIN_DIR)/$(SERVER_TARGET)
	rm -f $(BIN_DIR)/$(CLIENT_TARGET)

# indentify targets that do not create files
.PHONY: all clean server client folders
