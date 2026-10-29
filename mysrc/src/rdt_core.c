#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../inc/crc.h"
#include "../inc/rdt_core.h"

void rdt_session_init(rdt_session_t *s)
{
    if (s == NULL)
        return;
    memset(s, 0, sizeof(*s));
    s->sockfd = -1;
    s->peer_len = sizeof(s->peer);
    s->client_fin_seq = 0;
}

double rdt_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

uint32_t rdt_make_isn(void)
{
    return ((uint32_t)time(NULL) ^ ((uint32_t)getpid() << 16));
}

int rdt_rto_ms(double rto)
{
    int ms = (int)(rto * 1000);
    return ms < 1 ? 1 : ms;
}

void rdt_set_packet_checksum(rdt_hdr_t *h, const char *payload, int payload_len)
{
    if (h == NULL)
        return;
    h->checksum = 0;
    uint32_t crc = crc32_update(0, h, (size_t)HEADER_SIZE);
    if (payload != NULL && payload_len > 0)
        crc = crc32_update(crc, payload, (size_t)payload_len);
    h->checksum = htonl(crc);
}

int rdt_verify_packet(const void *pkt, int len)
{
    if (pkt == NULL || len < HEADER_SIZE)
        return 0;
    rdt_hdr_t h;
    memcpy(&h, pkt, HEADER_SIZE);
    uint32_t recv_cs = ntohl(h.checksum);
    h.checksum = 0;
    uint32_t crc = crc32_update(0, &h, (size_t)HEADER_SIZE);
    if (len > HEADER_SIZE)
        crc = crc32_update(crc, (const char *)pkt + HEADER_SIZE,
                           (size_t)(len - HEADER_SIZE));
    return crc == recv_cs;
}

void rdt_build_packet_len(char *pkt, uint32_t seq, uint16_t flags,
                          const char *payload, int payload_len)
{
    rdt_hdr_t h;
    h.seq = htonl(seq);
    h.ack = 0;
    h.flags = htons(flags);
    h.window = htons(RWND);
    rdt_set_packet_checksum(&h, payload, payload_len);
    memcpy(pkt, &h, HEADER_SIZE);
    if (payload != NULL && payload_len > 0)
        memcpy(pkt + HEADER_SIZE, payload, payload_len);
}

void rdt_build_packet(char *pkt, uint32_t seq, uint16_t flags,
                      const char *payload)
{
    rdt_build_packet_len(pkt, seq, flags, payload,
                         payload != NULL ? MAX_PAYLOAD : 0);
}

void rdt_build_ack_packet(char *pkt, uint32_t ackno)
{
    rdt_hdr_t h;
    h.seq = 0;
    h.ack = htonl(ackno);
    h.flags = htons(PKT_ACK);
    h.window = htons(RWND);
    rdt_set_packet_checksum(&h, NULL, 0);
    memcpy(pkt, &h, HEADER_SIZE);
}

void rdt_send_ack(rdt_session_t *s, uint32_t ackno)
{
    char pkt[HEADER_SIZE];
    rdt_build_ack_packet(pkt, ackno);
    sendto(s->sockfd, pkt, HEADER_SIZE, 0,
           (struct sockaddr *)&s->peer, sizeof(s->peer));
}

void rdt_send_ctrl(rdt_session_t *s, uint32_t seq, uint32_t ackno,
                   uint16_t flags)
{
    rdt_hdr_t h;
    h.seq = htonl(seq);
    h.ack = htonl(ackno);
    h.flags = htons(flags);
    h.window = htons(RWND);
    rdt_set_packet_checksum(&h, NULL, 0);
    sendto(s->sockfd, &h, HEADER_SIZE, 0,
           (struct sockaddr *)&s->peer, sizeof(s->peer));
}
