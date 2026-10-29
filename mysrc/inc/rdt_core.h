#ifndef RDT_CORE_H
#define RDT_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "rdt_protocol.h"

#define RDT_NAME_MAX 256

typedef struct {
    int sockfd;
    struct sockaddr_in peer;
    socklen_t peer_len;
    uint32_t snd_seq;
    uint32_t rcv_expected;
    uint32_t client_fin_seq;
    long retrans_count;
    char rcv_reorder[WINDOW_SIZE][MAX_PAYLOAD];
    int rcv_have[WINDOW_SIZE];
    int rcv_len[WINDOW_SIZE];
} rdt_session_t;

typedef void (*rdt_progress_cb)(long done, long total, long retrans);

void rdt_session_init(rdt_session_t *s);

double rdt_now_ms(void);
uint32_t rdt_make_isn(void);
int rdt_rto_ms(double rto);
void rdt_set_packet_checksum(rdt_hdr_t *h, const char *payload, int payload_len);
int rdt_verify_packet(const void *pkt, int len);

void rdt_build_packet_len(char *pkt, uint32_t seq, uint16_t flags,const char *payload, int payload_len);
void rdt_build_packet(char *pkt, uint32_t seq, uint16_t flags,const char *payload);
void rdt_build_ack_packet(char *pkt, uint32_t ackno);
void rdt_send_ack(rdt_session_t *s, uint32_t ackno);
void rdt_send_ctrl(rdt_session_t *s, uint32_t seq, uint32_t ackno,uint16_t flags);

/* 服务端 */
typedef void (*rdt_server_command_cb)(const char *cmd);

int rdt_server_handshake(rdt_session_t *s);
int rdt_server_recv_command(rdt_session_t *s, char *cmd, int maxlen);
int rdt_server_send_message(rdt_session_t *s, const char *payload,uint16_t flags);
void rdt_server_wave(rdt_session_t *s);
int rdt_server_multi_start(int port, rdt_server_command_cb cb);

/* 客户端 */
int rdt_client_connect(rdt_session_t *s, const char *ip, int port);
int rdt_client_disconnect(rdt_session_t *s);
int rdt_client_send_command(rdt_session_t *s, const char *cmd);
int rdt_client_recv_message(rdt_session_t *s, char *out, int maxlen);
int rdt_client_send_heartbeat(rdt_session_t *s);
int rdt_client_send_file(rdt_session_t *s, const char *path,rdt_progress_cb cb);
int rdt_client_recv_file(rdt_session_t *s, const char *save_dir,rdt_progress_cb cb);

#endif
