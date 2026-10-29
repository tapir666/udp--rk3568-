#ifndef RDT_PROTOCOL_H
#define RDT_PROTOCOL_H

#include <stdint.h>

//1.定义协议头中的包类型标志
#define PKT_DATA 0x01
#define PKT_ACK  0x02
#define PKT_FIN  0x04
#define PKT_SYN  0x08
#define PKT_FILE_INFO 0x10
#define PKT_CMD  0x20
#define PKT_CMD_RESP 0x40
#define PKT_HEARTBEAT 0x80

//2.定义协议相关常量
#define MAX_PAYLOAD 1024                       //单个数据包最大负载长度
#define WINDOW_SIZE 64                         //发送/乱序缓冲区大小
#define RWND 32                                //接收窗口大小
#define SEND_WINDOW 16                         //发送窗口大小
#define MAX_RETRY 10                           //握手/挥手控制包最大重传次数

//3.定义协议头结构体
typedef struct {
    uint32_t seq;      //数据包序号
    uint32_t ack;      //累积确认号
    uint16_t flags;    //包类型标志（DATA/ACK/SYN/FIN/CMD）
    uint16_t window;   //接收窗口通告
    uint32_t checksum; //整包 CRC32，网络字节序
} rdt_hdr_t;

#define HEADER_SIZE ((int)sizeof(rdt_hdr_t))

#endif
