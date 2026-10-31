#include <string.h>
#include "frame.h"
#include "logger.h"

/* 内部函数, 回到找帧头状态, 半途收的数据全部作废 */
static void frame_parser_reset(frame_parser_t *parser)
{
    parser->state = FRAME_SEEK_H1;
    parser->pos = 0;
    parser->checksum = 0;
    /* len和payload故意不清: 状态机走不到读它们的路径, 清了是白忙 */
}

void frame_parser_init(frame_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }
    parser->len = 0;
    frame_parser_reset(parser);
}

int frame_parse_byte(frame_parser_t *parser, uint8_t byte, frame_t *out)
{
    if (parser == NULL || out == NULL) {
        return -1;
    }

    switch (parser->state) {

    case FRAME_SEEK_H1:
        if (byte == FRAME_HEADER1) {
            parser->state = FRAME_SEEK_H2;
        }
        /* 不是0xAA就什么都不做: 噪声字节在这里被自然丢弃, 不打日志防刷屏 */
        break;

    case FRAME_SEEK_H2:
        if (byte == FRAME_HEADER2) {
            parser->state = FRAME_SEEK_LEN;
        } else if (byte == FRAME_HEADER1) {
            /* 停在原地: 这个0xAA可能才是真帧头的第一字节 */
        } else {
            parser->state = FRAME_SEEK_H1;
        }
        break;

    case FRAME_SEEK_LEN:
        if (byte == 0 || byte > FRAME_MAX_PAYLOAD) {
            LOG_WARN("frame: bad len %d, resync", byte);
            frame_parser_reset(parser);
        } else {
            parser->len = byte;
            parser->pos = 0;
            parser->checksum = byte;      /* 累加从LEN字节自己开始 */
            parser->state = FRAME_SEEK_PAYLOAD;
        }
        break;

    case FRAME_SEEK_PAYLOAD:
        parser->payload[parser->pos] = byte;
        parser->checksum += byte;         /* uint8_t累加自然回绕, 等价于取低8位 */
        parser->pos++;
        if (parser->pos == parser->len) {
            parser->state = FRAME_SEEK_CHECK;
        }
        break;

    case FRAME_SEEK_CHECK:
        if (byte == parser->checksum) {
            out->len = parser->len;
            memcpy(out->payload, parser->payload, parser->len);
            frame_parser_reset(parser);
            return 1;                     /* 一帧完整到手 */
        }
        LOG_WARN("frame: checksum fail expect 0x%02X got 0x%02X, resync",
                 parser->checksum, byte);
        frame_parser_reset(parser);       /* 只丢这一帧, 不回退, 下一帧重新同步 */
        break;

    default:
        /* 正常永远到不了这里, 进来说明state被内存踩坏, 属于程序bug */
        LOG_ERROR("frame: invalid state %d", parser->state);
        frame_parser_reset(parser);
        return -1;
    }

    return 0;
}