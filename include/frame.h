#ifndef FRAME_H
#define FRAME_H

#include <stdint.h>

/* 私有协议帧格式, 一帧 = 帧头2字节 + 长度1字节 + 数据 + 校验1字节      */
/* | 0xAA | 0x55 | LEN(1..64) | PAYLOAD(LEN字节) | CHECKSUM(1字节) |  */
/* CHECKSUM = (LEN字节 + 所有PAYLOAD字节) 累加后取低8位                */
/* 温湿度payload约定4字节, 大端: 温度int16单位0.1度, 湿度int16单位0.1% */

#define FRAME_HEADER1     0xAA
#define FRAME_HEADER2     0x55
#define FRAME_MAX_PAYLOAD 64

/* 状态机的五个状态, 解析按此顺序推进 */
typedef enum {
    FRAME_SEEK_H1 = 0,    /* 找帧头第一字节0xAA */
    FRAME_SEEK_H2,        /* 找帧头第二字节0x55 */
    FRAME_SEEK_LEN,       /* 等待长度字节 */
    FRAME_SEEK_PAYLOAD,   /* 收集有效数据 */
    FRAME_SEEK_CHECK      /* 等待校验字节 */
} frame_state_t;

/* 解析成功产出一帧, payload按字节原样存放, 字段解释由上层负责 */
typedef struct {
    uint8_t len;                          /* 本帧payload字节数 */
    uint8_t payload[FRAME_MAX_PAYLOAD];   /* payload内容 */
} frame_t;

/* 解析器状态, 由调用者创建并持有, 跨read调用存活, 半包靠它续命 */
typedef struct {
    frame_state_t state;                  /* 当前所处状态 */
    uint8_t len;                          /* 正在收的帧的LEN */
    uint8_t pos;                          /* payload已收字节数 */
    uint8_t checksum;                     /* LEN加payload的滚动累加和 */
    uint8_t payload[FRAME_MAX_PAYLOAD];   /* 收到一半的payload暂存区 */
} frame_parser_t;

/* 上岗前调用一次 */
void frame_parser_init(frame_parser_t *parser);

/* 每收到一个字节喂一次 */
/* 返回 1 = out里得到一帧, 0 = 继续收或坏帧已丢, -1 = 参数错 */
int frame_parse_byte(frame_parser_t *parser, uint8_t byte, frame_t *out);

#endif /* FRAME_H */