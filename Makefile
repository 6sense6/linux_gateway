CC      = gcc
CFLAGS  = -Wall -g -Iinclude -pthread -MMD -MP   # ← 改1：自动生成头文件依赖.d
BUILD   = build
GATEWAY_OBJS = $(BUILD)/main.o $(BUILD)/ringbuffer.o
TEST_OBJS    = $(BUILD)/test_ringbuffer.o $(BUILD)/ringbuffer.o
all: $(BUILD)/gateway $(BUILD)/test_ringbuffer    # ← 改2：产物也进build/
$(BUILD)/gateway: $(GATEWAY_OBJS)
	$(CC) $(CFLAGS) $^ -o $@
$(BUILD)/test_ringbuffer: $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/test_ringbuffer.o: test/test_ringbuffer.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD):
	mkdir -p $@
-include $(wildcard $(BUILD)/*.d)                  # ← 改3：把.d依赖收进来
.PHONY: all clean
clean:
	rm -rf $(BUILD)                                # ← clean只需删目录
