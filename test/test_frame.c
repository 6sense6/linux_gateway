#include <stdio.h>
#include <stdint.h>
#include "frame.h"
#include "logger.h"

static int g_pass = 0;
static int g_fail = 0;

/* 判定并计数, 测试结论必须printf出来, 不靠肉眼数 */
static void check(const char *name, int cond)
{
    if (cond) {
        g_pass++;
        printf("[PASS] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s\n", name);
    }
}

/* 按协议构造一帧温湿度, 返回帧字节数, 恒为8 */
static int build_frame(int16_t temp, int16_t humi, uint8_t *out)
{
    uint8_t checksum = 0;
    int i;

    out[0] = FRAME_HEADER1;
    out[1] = FRAME_HEADER2;
    out[2] = 4;                        /* LEN: 温湿度固定4字节 */
    out[3] = (uint8_t)(temp >> 8);     /* 温度高字节, 大端先行 */
    out[4] = (uint8_t)(temp & 0xFF);
    out[5] = (uint8_t)(humi >> 8);
    out[6] = (uint8_t)(humi & 0xFF);

    for (i = 2; i <= 6; i++) {         /* LEN到payload逐字节累加, 与接收侧同一定义 */
        checksum += out[i];
    }
    out[7] = checksum;
    return 8;
}

/* 把n个字节逐个喂给解析器, 返回出帧数, 最后一帧存入last, last可为NULL */
static int feed(frame_parser_t *p, const uint8_t *data, int n, frame_t *last)
{
    frame_t f;
    int frames = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (frame_parse_byte(p, data[i], &f) == 1) {
            frames++;
            if (last != NULL) {
                *last = f;
            }
        }
    }
    return frames;
}

/* 从frame_t按大端取出温湿度原始值 */
static int16_t get_temp(const frame_t *f)
{
    return (int16_t)((f->payload[0] << 8) | f->payload[1]);
}

static int16_t get_humi(const frame_t *f)
{
    return (int16_t)((f->payload[2] << 8) | f->payload[3]);
}

/* 用例A: 正常帧, 一次喂8字节, 出1帧, 数值正确 */
static void test_case_a(void)
{
    frame_parser_t p;
    frame_t f = {0};
    uint8_t raw[8];
    int frames;

    frame_parser_init(&p);
    build_frame(257, 653, raw);          /* 25.7度, 65.3% */
    frames = feed(&p, raw, 8, &f);

    check("A frames==1", frames == 1);
    check("A len==4", f.len == 4);
    check("A temp==257", get_temp(&f) == 257);
    check("A humi==653", get_humi(&f) == 653);
}

/* 用例B: 半包. 先喂前3字节断言0帧, 再喂后5字节, 共出1帧数值正确 */
static void test_case_b(void)
{
    frame_parser_t p;
    frame_t f = {0};
    uint8_t raw[8];
    int frames;

    frame_parser_init(&p);
    build_frame(257, 653, raw);          /* 25.7度, 65.3% */
    frames = feed(&p, raw, 3, &f);
    check("B frames==0", frames == 0);
    frames = feed(&p, raw + 3, 5, &f);
    check("B frames==1", frames == 1);
    check("B len==4", f.len == 4);
    check("B temp==257", get_temp(&f) == 257);
    check("B humi==653", get_humi(&f) == 653);
}

/* 用例C: 粘包. 两帧共16字节一次喂, 出2帧, 最后收到的是第二帧的数值 */
static void test_case_c(void)
{
    frame_parser_t p;
    frame_t f = {0};
    uint8_t raw[16];
    int frames;

    frame_parser_init(&p);
    build_frame(257, 653, raw);          /* 帧一: 25.7度, 65.3% */
    build_frame(260, 700, raw + 8);      /* 帧二: 26.0度, 70.0% */
    frames = feed(&p, raw, 16, &f);

    check("C frames==2", frames == 2);
    check("C last len==4", f.len == 4);
    check("C last temp==260", get_temp(&f) == 260);
    check("C last humi==700", get_humi(&f) == 700);
}

/* 用例D: 坏校验. 好帧末字节被翻转, 出0帧, 紧接的好帧能出1帧 */
static void test_case_d(void)
{
    frame_parser_t p;
    frame_t f = {0};
    uint8_t bad[8];
    uint8_t good[8];
    int frames;

    frame_parser_init(&p);
    build_frame(257, 653, bad);
    bad[7] ^= 0x01;                      /* 翻转校验字节, 制造坏帧 */
    build_frame(260, 700, good);         /* 紧接的好帧 */

    frames = feed(&p, bad, 8, &f);
    check("D bad frames==0", frames == 0);

    frames = feed(&p, good, 8, &f);
    check("D good frames==1", frames == 1);
    check("D temp==260", get_temp(&f) == 260);
    check("D humi==700", get_humi(&f) == 700);
    /* 运行时会打印一条 checksum fail 的 WARN, 属预期, 不是测试失败 */
}

/* 用例E: 假帧头. payload前三个字节就是AA 55 04, 解析器不被带偏 */
static void test_case_e(void)
{
    frame_parser_t p;
    frame_t f = {0};
    /* 手工构造帧: AA 55 04 | AA 55 04 01 | CHECKSUM
       CHECKSUM = (04 + AA + 55 + 04 + 01) & 0xFF
                = (0x04+0xAA+0x55+0x04+0x01) & 0xFF
                = 0x108 & 0xFF
                = 0x08
       注意 payload 前3字节 = AA 55 04, 看起来像帧头, 但收体状态下只当数据存 */
    uint8_t raw[8] = { 0xAA, 0x55, 0x04, 0xAA, 0x55, 0x04, 0x01, 0x08 };
    int frames;

    frame_parser_init(&p);
    frames = feed(&p, raw, 8, &f);

    check("E frames==1", frames == 1);
    check("E len==4", f.len == 4);
    check("E payload[0]==0xAA", f.payload[0] == 0xAA);
    check("E payload[1]==0x55", f.payload[1] == 0x55);
    check("E payload[2]==0x04", f.payload[2] == 0x04);
    /* 本用例不应出现任何 WARN, 出现即说明 SEEK_PAYLOAD 把 AA 55 当帧头了 */
}

/* 用例F: 噪声前置. 20个固定垃圾字节后跟一帧好帧, 照常出1帧 */
static void test_case_f(void)
{
    frame_parser_t p;
    frame_t f = {0};
    /* 20个固定垃圾字节, 全部避开 0xAA, 保证可复现 */
    uint8_t raw[28] = {
        0x00, 0x11, 0x22, 0x33, 0x44,
        0x66, 0x77, 0x88, 0x99, 0xBB,
        0xCC, 0xDD, 0xEE, 0xFF, 0x12,
        0x34, 0x56, 0x78, 0x9A, 0xBC
    };
    int frames;

    frame_parser_init(&p);
    build_frame(257, 653, raw + 20);     /* 后8字节是好帧 */
    frames = feed(&p, raw, 28, &f);

    check("F frames==1", frames == 1);
    check("F len==4", f.len == 4);
    check("F temp==257", get_temp(&f) == 257);
    check("F humi==653", get_humi(&f) == 653);
}

/* 用例G: 双AA帧头. 字节流12 AA后接一帧正常帧, 仍出1帧 */
static void test_case_g(void)
{
    frame_parser_t p;
    frame_t f = {0};
    uint8_t raw[10];
    int frames;

    frame_parser_init(&p);
    raw[0] = 0x12;                       /* 普通噪声, SEEK_H1 丢弃 */
    raw[1] = 0xAA;                       /* 候选帧头, 进 SEEK_H2 */
    build_frame(257, 653, raw + 2);      /* 紧接真帧 AA 55 ..., SEEK_H2 遇 AA 停留逻辑生效 */

    frames = feed(&p, raw, 10, &f);

    check("G frames==1", frames == 1);
    check("G len==4", f.len == 4);
    check("G temp==257", get_temp(&f) == 257);
    check("G humi==653", get_humi(&f) == 653);
}

/* 用例H: 非法LEN. LEN为0xFF大于64, 判假同步丢弃, 后续好帧照常出 */
static void test_case_h(void)
{
    frame_parser_t p;
    frame_t f = {0};
    uint8_t raw[11] = { 0xAA, 0x55, 0xFF };  /* 假帧头接非法LEN */
    int frames;

    frame_parser_init(&p);
    build_frame(257, 653, raw + 3);       /* 真帧紧跟在坏片段后面 */
    frames = feed(&p, raw, 11, &f);

    check("H frames==1", frames == 1);
    check("H temp==257", get_temp(&f) == 257);
    check("H humi==653", get_humi(&f) == 653);
    /* 预期打印一条 bad len 的 WARN */
}

int main(void)
{
    logger_init(NULL, LOG_LVL_DEBUG);

    test_case_a();
    test_case_b();
    test_case_c();
    test_case_d();
    test_case_e();
    test_case_f();
    test_case_g();
    test_case_h();

    logger_close();
    printf("\ntotal: pass %d, fail %d\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}