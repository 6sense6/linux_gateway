#include <stdio.h>
#include "logger.h"

int main(void)
{
    if (logger_init("build/run.log", LOG_LVL_DEBUG) != 0) {
        return 1;
    }

    LOG_DEBUG("这条应该被过滤，不出现");
    LOG_INFO("gateway starting");
    LOG_WARN("ringbuffer used %d/%d", 3, 128);
    LOG_ERROR("open %s failed", "/dev/ttyUSB0");

    logger_close();
    return 0;
}