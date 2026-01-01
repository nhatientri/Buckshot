# Tên chương trình thực thi (executable)
TARGET_CLIENT = buckshot_client
TARGET_MOCK = mock_server  # Tên mới cho Mock Server

# Thư mục chứa mã nguồn và đối tượng
SRCDIR = src
OBJDIR = obj

# Các file mã nguồn
SRC_CLIENT = \
$(SRCDIR)/client_main.c \
$(SRCDIR)/protocol.c \
$(SRCDIR)/history_manager.c \
$(SRCDIR)/gui.c
SRC_MOCK = $(SRCDIR)/MockServer.c

# Tất cả các file nguồn để biên dịch
SRC = $(SRC_CLIENT) $(SRC_MOCK)

# Tên các file object
OBJ_CLIENT = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC_CLIENT))
OBJ_MOCK = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRC_MOCK))

# Cờ biên dịch và liên kết (Đã bao gồm GTK+ và JSON-C)
CFLAGS = -Wall -std=c99 -pthread $(shell pkg-config --cflags json-c gtk+-3.0)
LDFLAGS = -pthread $(shell pkg-config --libs json-c gtk+-3.0)

.PHONY: all clean run client mock

# Mục tiêu chính: Build cả hai chương trình
all: $(OBJDIR) $(TARGET_CLIENT) $(TARGET_MOCK)

# Quy tắc tạo thư mục obj (order-only prerequisite)
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Quy tắc chung để tạo file object (.o) từ file nguồn (.c)
# Sẽ tạo thư mục obj nếu nó chưa tồn tại (nhờ | $(OBJDIR))
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Quy tắc build Client: Liên kết tất cả các file object của client
$(TARGET_CLIENT): $(OBJ_CLIENT)
	$(CC) $^ -o $@ $(LDFLAGS)

# Quy tắc build Mock Server: Liên kết file object của mock server
$(TARGET_MOCK): $(OBJ_MOCK)
	$(CC) $^ -o $@ $(LDFLAGS)

# Quy tắc chạy Client
client: $(TARGET_CLIENT)
	./$(TARGET_CLIENT)

# Quy tắc chạy Mock Server
mock: $(TARGET_MOCK)
	./$(TARGET_MOCK)

# Quy tắc dọn dẹp (Xóa cả hai target)
clean:
	rm -rf $(OBJDIR) $(TARGET_CLIENT) $(TARGET_MOCK) game_history.csv