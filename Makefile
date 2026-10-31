CC      = gcc
CFLAGS  = -Wall -g -Iinclude -pthread -MMD -MP
BUILD   = build

# gateway 主程序：main + 它用到的所有模块
GATEWAY_OBJS = $(BUILD)/main.o $(BUILD)/ringbuffer.o $(BUILD)/logger.o \
               $(BUILD)/serial.o $(BUILD)/frame.o

# 每个测试程序 = 测试自己的 .o + 它依赖的模块 .o
TEST_RINGBUFFER_OBJS = $(BUILD)/test_ringbuffer.o $(BUILD)/ringbuffer.o
TEST_LOGGER_OBJS     = $(BUILD)/test_logger.o     $(BUILD)/logger.o
TEST_SERIAL_OBJS     = $(BUILD)/test_serial.o     $(BUILD)/serial.o  $(BUILD)/logger.o
TEST_FRAME_OBJS      = $(BUILD)/test_frame.o      $(BUILD)/frame.o   $(BUILD)/logger.o

ALL_TESTS = $(BUILD)/test_ringbuffer $(BUILD)/test_logger \
            $(BUILD)/test_serial $(BUILD)/test_frame

all: $(BUILD)/gateway $(ALL_TESTS)

# 一键回归: 三个不需外部设备的测试; test_serial 要 socat + 设备参数, 手动跑
# 注: test_ringbuffer 内部 sleep 10 秒, 属预期
test: $(BUILD)/test_ringbuffer $(BUILD)/test_logger $(BUILD)/test_frame
	$(BUILD)/test_ringbuffer && \
	$(BUILD)/test_logger && \
	$(BUILD)/test_frame
	@echo "ALL TESTS PASSED"

$(BUILD)/gateway: $(GATEWAY_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_ringbuffer: $(TEST_RINGBUFFER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_logger: $(TEST_LOGGER_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_serial: $(TEST_SERIAL_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/test_frame: $(TEST_FRAME_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# src/ 下的 .c → build/ 下的同名 .o（模块）
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# test/ 下的 .c → build/ 下的同名 .o（测试）
$(BUILD)/%.o: test/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $@

-include $(wildcard $(BUILD)/*.d)

.PHONY: all clean test
clean:
	rm -rf $(BUILD)
