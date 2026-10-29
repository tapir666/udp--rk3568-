#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include "../inc/rdt_core.h"

#define IDLE_TIMEOUT_S 30   //连续多少秒没有收到任何包就判定客户端掉线
#define MAX_CLIENTS 16
#define MAX_QUEUE 128

typedef struct {
    rdt_session_t sess;
    pthread_t tid;
    int active;
    pthread_mutex_t q_lock;
    pthread_cond_t q_cond;
    char q_buf[MAX_QUEUE][HEADER_SIZE + MAX_PAYLOAD];
    int q_len[MAX_QUEUE];
    int q_head;
    int q_tail;
    int q_count;
} rdt_server_client_t;

static int g_multi_sockfd = -1;
static rdt_server_client_t g_clients[MAX_CLIENTS];
static pthread_mutex_t g_clients_lock = PTHREAD_MUTEX_INITIALIZER;
static rdt_server_command_cb g_on_command = NULL;

static void *rdt_server_client_worker(void *arg);

static int rdt_server_recv_ack(rdt_session_t *s, uint32_t ackno, double rto)
{
    struct pollfd pfd = { s->sockfd, POLLIN, 0 };
    int ret = poll(&pfd, 1, rdt_rto_ms(rto));
    if (ret > 0 && (pfd.revents & POLLIN))
    {
        rdt_hdr_t h;
        s->peer_len = sizeof(s->peer);
        if (recvfrom(s->sockfd, &h, HEADER_SIZE, 0,
                     (struct sockaddr *)&s->peer, &s->peer_len) == HEADER_SIZE &&
            rdt_verify_packet(&h, HEADER_SIZE))
        {
            if ((ntohs(h.flags) & PKT_ACK) && ntohl(h.ack) == ackno)
                return 0;
        }
    }
    return -1;
}

int rdt_server_handshake(rdt_session_t *s)
{
    char pkt[HEADER_SIZE];
    double rto = 0.5;
    int retry = 0;
    uint32_t client_isn = 0;
    uint32_t server_isn = 0;

    while (1)
    {
        s->peer_len = sizeof(s->peer);
        int n = recvfrom(s->sockfd, pkt, HEADER_SIZE, 0,
                         (struct sockaddr *)&s->peer, &s->peer_len);
        if (n == HEADER_SIZE)
        {
            rdt_hdr_t *h = (rdt_hdr_t *)pkt;
            if (rdt_verify_packet(pkt, n) && (ntohs(h->flags) & PKT_SYN))
            {
                client_isn = ntohl(h->seq);
                break;
            }
        }
    }

    server_isn = rdt_make_isn();
    rdt_send_ctrl(s, server_isn, client_isn + 1, PKT_SYN | PKT_ACK);

    while (1)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            s->peer_len = sizeof(s->peer);
            if (recvfrom(s->sockfd, &h, HEADER_SIZE, 0,
                         (struct sockaddr *)&s->peer, &s->peer_len) == HEADER_SIZE &&
                rdt_verify_packet(&h, HEADER_SIZE))
            {
                uint16_t flags = ntohs(h.flags);
                if (flags & PKT_SYN)
                {
                    rdt_send_ctrl(s, server_isn, client_isn + 1,
                                  PKT_SYN | PKT_ACK);
                    continue;
                }
                if ((flags & PKT_ACK) && ntohl(h.ack) == server_isn + 1)
                    break;
            }
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
                return -1;
            rdt_send_ctrl(s, server_isn, client_isn + 1,
                          PKT_SYN | PKT_ACK);
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
        else
        {
            if (errno == EINTR) continue;
            return -1;
        }
    }

    s->snd_seq = server_isn + 1;
    s->rcv_expected = client_isn + 1;
    s->client_fin_seq = 0;
    return 0;
}

int rdt_server_recv_command(rdt_session_t *s, char *cmd, int maxlen)
{
    int idle = 0;   //空闲计时，单位秒

    while (1)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000);
        if (ret == 0)
        {
            if (++idle >= IDLE_TIMEOUT_S)
            {
                printf("session timeout, close connection\n");
                return -1;
            }
            continue;
        }
        if (ret < 0)
            continue;

        idle = 0;

        if (pfd.revents & POLLIN)
        {
            while (1)
            {
                char pkt[HEADER_SIZE + MAX_PAYLOAD];
                s->peer_len = sizeof(s->peer);
                int n = recvfrom(s->sockfd, pkt, sizeof(pkt), MSG_DONTWAIT,
                                 (struct sockaddr *)&s->peer, &s->peer_len);
                if (n <= 0)
                    break;
                if (!rdt_verify_packet(pkt, n))
                    continue;

                rdt_hdr_t *h = (rdt_hdr_t *)pkt;
                uint16_t flags = ntohs(h->flags);
                if (flags & PKT_ACK)
                    continue;

                //心跳包：客户端还活着，回ACK并重置超时
                if (flags & PKT_HEARTBEAT)
                {
                    rdt_send_ack(s, s->rcv_expected);
                    continue;
                }

                if ((flags & PKT_FIN) && n == HEADER_SIZE)
                {
                    s->client_fin_seq = ntohl(h->seq);
                    rdt_send_ack(s, s->client_fin_seq + 1);
                    return -2;
                }

                uint32_t seq = ntohl(h->seq);
                if (seq == s->rcv_expected)
                {
                    int plen = n - HEADER_SIZE;
                    if (plen > maxlen) plen = maxlen;
                    memcpy(cmd, pkt + HEADER_SIZE, plen);
                    s->rcv_expected++;
                    rdt_send_ack(s, s->rcv_expected);
                    return plen;
                }
                else if (seq > s->rcv_expected)
                {
                    if (!s->rcv_have[seq % WINDOW_SIZE])
                    {
                        memcpy(s->rcv_reorder[seq % WINDOW_SIZE],
                               pkt + HEADER_SIZE, n - HEADER_SIZE);
                        s->rcv_len[seq % WINDOW_SIZE] = n - HEADER_SIZE;
                        s->rcv_have[seq % WINDOW_SIZE] = 1;
                    }
                    rdt_send_ack(s, s->rcv_expected);
                }
                else
                {
                    rdt_send_ack(s, s->rcv_expected);
                }
            }
        }
    }
}

int rdt_server_send_message(rdt_session_t *s, const char *payload,uint16_t flags)
{
    int len = (int)strlen(payload);
    if (len > MAX_PAYLOAD)
        len = MAX_PAYLOAD;

    uint32_t seq = s->snd_seq++;
    char pkt[HEADER_SIZE + MAX_PAYLOAD];
    rdt_build_packet_len(pkt, seq, flags, payload, len);

    double rto = 0.2;
    int retry = 0;
    while (1)
    {
        sendto(s->sockfd, pkt, HEADER_SIZE + len, 0,
               (struct sockaddr *)&s->peer, sizeof(s->peer));
        if (rdt_server_recv_ack(s, seq + 1, rto) == 0)
            return 0;
        if (++retry >= MAX_RETRY)
            return -1;
        rto *= 2;
        if (rto > 5.0) rto = 5.0;
    }
}

void rdt_server_wave(rdt_session_t *s)
{
    char pkt[HEADER_SIZE];
    double rto = 0.5;
    int retry = 0;
    uint32_t client_fin_seq = s->client_fin_seq;
    s->client_fin_seq = 0;

    while (client_fin_seq == 0)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            char buf[HEADER_SIZE + MAX_PAYLOAD];
            s->peer_len = sizeof(s->peer);
            int n = recvfrom(s->sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&s->peer, &s->peer_len);
            if (n == HEADER_SIZE)
            {
                rdt_hdr_t *h = (rdt_hdr_t *)buf;
                if (rdt_verify_packet(buf, n) && (ntohs(h->flags) & PKT_FIN))
                {
                    client_fin_seq = ntohl(h->seq);
                    break;
                }
            }
        }
        else if (++retry >= MAX_RETRY)
            return;
    }

    rdt_send_ack(s, client_fin_seq + 1);

    uint32_t server_fin_seq = s->snd_seq;
    rdt_send_ctrl(s, server_fin_seq, 0, PKT_FIN);

    retry = 0;
    rto = 0.5;
    while (1)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            s->peer_len = sizeof(s->peer);
            if (recvfrom(s->sockfd, &h, HEADER_SIZE, 0,
                         (struct sockaddr *)&s->peer, &s->peer_len) == HEADER_SIZE &&
                rdt_verify_packet(&h, HEADER_SIZE))
            {
                uint16_t flags = ntohs(h.flags);
                if ((flags & PKT_FIN) && ntohl(h.seq) == client_fin_seq)
                {
                    rdt_send_ack(s, client_fin_seq + 1);
                    continue;
                }
                if ((flags & PKT_ACK) && ntohl(h.ack) == server_fin_seq + 1)
                    break;
            }
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
                break;
            rdt_send_ctrl(s, server_fin_seq, 0, PKT_FIN);
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
        else
        {
            if (errno == EINTR) continue;
            break;
        }
    }
    (void)pkt;
}

static int rdt_server_same_addr(const struct sockaddr_in *a,
                                const struct sockaddr_in *b)
{
    return a->sin_addr.s_addr == b->sin_addr.s_addr &&
           a->sin_port == b->sin_port;
}

static void rdt_server_queue_init(rdt_server_client_t *cs)
{
    pthread_mutex_init(&cs->q_lock, NULL);
    pthread_cond_init(&cs->q_cond, NULL);
    cs->q_head = 0;
    cs->q_tail = 0;
    cs->q_count = 0;
}

static void rdt_server_queue_push(rdt_server_client_t *cs, const char *pkt,
                                  int len)
{
    if (len <= 0 || len > HEADER_SIZE + MAX_PAYLOAD)
        return;
    pthread_mutex_lock(&cs->q_lock);
    if (cs->q_count < MAX_QUEUE)
    {
        memcpy(cs->q_buf[cs->q_tail], pkt, len);
        cs->q_len[cs->q_tail] = len;
        cs->q_tail = (cs->q_tail + 1) % MAX_QUEUE;
        cs->q_count++;
        pthread_cond_signal(&cs->q_cond);
    }
    pthread_mutex_unlock(&cs->q_lock);
}

static int rdt_server_queue_pop(rdt_server_client_t *cs, char *buf,
                                int maxlen, int timeout_ms)
{
    pthread_mutex_lock(&cs->q_lock);
    while (cs->q_count == 0)
    {
        if (timeout_ms <= 0)
        {
            pthread_mutex_unlock(&cs->q_lock);
            return -1;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L)
        {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        int rc = pthread_cond_timedwait(&cs->q_cond, &cs->q_lock, &ts);
        if (rc == ETIMEDOUT)
        {
            pthread_mutex_unlock(&cs->q_lock);
            return -1;
        }
        if (rc != 0)
        {
            pthread_mutex_unlock(&cs->q_lock);
            return -1;
        }
    }
    int len = cs->q_len[cs->q_head];
    if (len > maxlen)
        len = maxlen;
    memcpy(buf, cs->q_buf[cs->q_head], len);
    cs->q_head = (cs->q_head + 1) % MAX_QUEUE;
    cs->q_count--;
    pthread_mutex_unlock(&cs->q_lock);
    return len;
}

static int rdt_server_queue_header(rdt_server_client_t *cs, rdt_hdr_t *h,
                                   int timeout_ms)
{
    char pkt[HEADER_SIZE + MAX_PAYLOAD];
    int n = rdt_server_queue_pop(cs, pkt, sizeof(pkt), timeout_ms);
    if (n < HEADER_SIZE || !rdt_verify_packet(pkt, n))
        return -1;
    memcpy(h, pkt, HEADER_SIZE);
    return 0;
}

static rdt_server_client_t *rdt_server_find_client_locked(
    const struct sockaddr_in *addr)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_clients[i].active &&
            rdt_server_same_addr(&g_clients[i].sess.peer, addr))
            return &g_clients[i];
    }
    return NULL;
}

static rdt_server_client_t *rdt_server_create_client_locked(
    const struct sockaddr_in *addr)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_clients[i].active)
            continue;
        memset(&g_clients[i], 0, sizeof(g_clients[i]));
        rdt_session_init(&g_clients[i].sess);
        g_clients[i].sess.sockfd = g_multi_sockfd;
        g_clients[i].sess.peer = *addr;
        g_clients[i].sess.peer_len = sizeof(*addr);
        rdt_server_queue_init(&g_clients[i]);
        g_clients[i].active = 1;
        if (pthread_create(&g_clients[i].tid, NULL,
                           rdt_server_client_worker,
                           &g_clients[i]) != 0)
        {
            g_clients[i].active = 0;
            return NULL;
        }
        pthread_detach(g_clients[i].tid);
        return &g_clients[i];
    }
    return NULL;
}

static void rdt_server_cleanup_client(rdt_server_client_t *cs)
{
    pthread_mutex_lock(&g_clients_lock);
    cs->active = 0;
    pthread_mutex_unlock(&g_clients_lock);
}

static int rdt_server_multi_handshake(rdt_server_client_t *cs)
{
    rdt_session_t *s = &cs->sess;
    char pkt[HEADER_SIZE + MAX_PAYLOAD];
    int idle = 0;
    uint32_t client_isn = 0;

    while (1)
    {
        int n = rdt_server_queue_pop(cs, pkt, sizeof(pkt), 1000);
        if (n < 0)
        {
            if (++idle >= IDLE_TIMEOUT_S)
                return -1;
            continue;
        }
        if (n < HEADER_SIZE || !rdt_verify_packet(pkt, n))
            continue;
        rdt_hdr_t *h = (rdt_hdr_t *)pkt;
        if (ntohs(h->flags) & PKT_SYN)
        {
            client_isn = ntohl(h->seq);
            break;
        }
    }

    uint32_t server_isn = rdt_make_isn();
    rdt_send_ctrl(s, server_isn, client_isn + 1, PKT_SYN | PKT_ACK);
    printf("remote server send SYN+ACK, server_isn=%u\n", server_isn);

    double rto = 0.5;
    int retry = 0;
    while (1)
    {
        rdt_hdr_t h;
        if (rdt_server_queue_header(cs, &h, rdt_rto_ms(rto)) == 0)
        {
            uint16_t flags = ntohs(h.flags);
            if (flags & PKT_SYN)
            {
                rdt_send_ctrl(s, server_isn, client_isn + 1,
                              PKT_SYN | PKT_ACK);
                continue;
            }
            if ((flags & PKT_ACK) && ntohl(h.ack) == server_isn + 1)
                break;
            continue;
        }
        if (++retry >= MAX_RETRY)
        {
            printf("remote handshake timeout\n");
            return -1;
        }
        rdt_send_ctrl(s, server_isn, client_isn + 1, PKT_SYN | PKT_ACK);
        rto *= 2;
        if (rto > 5.0) rto = 5.0;
    }

    s->snd_seq = server_isn + 1;
    s->rcv_expected = client_isn + 1;
    s->client_fin_seq = 0;
    printf("remote handshake ok\n");
    return 0;
}

static int rdt_server_multi_recv_command(rdt_server_client_t *cs,char *cmd, int maxlen)
{
    rdt_session_t *s = &cs->sess;
    int idle = 0;

    while (1)
    {
        char pkt[HEADER_SIZE + MAX_PAYLOAD];
        int n = rdt_server_queue_pop(cs, pkt, sizeof(pkt), 1000);
        if (n < 0)
        {
            if (++idle >= IDLE_TIMEOUT_S)
            {
                printf("remote session timeout, close connection\n");
                return -1;
            }
            continue;
        }
        idle = 0;
        if (n < HEADER_SIZE || !rdt_verify_packet(pkt, n))
            continue;

        rdt_hdr_t *h = (rdt_hdr_t *)pkt;
        uint16_t flags = ntohs(h->flags);
        if (flags & PKT_ACK)
            continue;

        if (flags & PKT_HEARTBEAT)
        {
            rdt_send_ack(s, s->rcv_expected);
            continue;
        }

        if ((flags & PKT_FIN) && n == HEADER_SIZE)
        {
            s->client_fin_seq = ntohl(h->seq);
            rdt_send_ack(s, s->client_fin_seq + 1);
            return -2;
        }

        uint32_t seq = ntohl(h->seq);
        if (seq == s->rcv_expected)
        {
            int plen = n - HEADER_SIZE;
            if (plen > maxlen) plen = maxlen;
            memcpy(cmd, pkt + HEADER_SIZE, plen);
            s->rcv_expected++;
            rdt_send_ack(s, s->rcv_expected);
            return plen;
        }
        else if (seq > s->rcv_expected)
        {
            if (!s->rcv_have[seq % WINDOW_SIZE])
            {
                memcpy(s->rcv_reorder[seq % WINDOW_SIZE],
                       pkt + HEADER_SIZE, n - HEADER_SIZE);
                s->rcv_len[seq % WINDOW_SIZE] = n - HEADER_SIZE;
                s->rcv_have[seq % WINDOW_SIZE] = 1;
            }
            rdt_send_ack(s, s->rcv_expected);
        }
        else
        {
            rdt_send_ack(s, s->rcv_expected);
        }
    }
}

static int rdt_server_multi_send_message(rdt_server_client_t *cs,const char *payload,uint16_t flags)
{
    rdt_session_t *s = &cs->sess;
    int len = (int)strlen(payload);
    if (len > MAX_PAYLOAD)
        len = MAX_PAYLOAD;

    uint32_t seq = s->snd_seq++;
    char pkt[HEADER_SIZE + MAX_PAYLOAD];
    rdt_build_packet_len(pkt, seq, flags, payload, len);

    double rto = 0.2;
    int retry = 0;
    while (1)
    {
        sendto(g_multi_sockfd, pkt, HEADER_SIZE + len, 0,
               (struct sockaddr *)&s->peer, sizeof(s->peer));
        rdt_hdr_t h;
        if (rdt_server_queue_header(cs, &h, rdt_rto_ms(rto)) == 0 &&
            (ntohs(h.flags) & PKT_ACK) && ntohl(h.ack) == seq + 1)
            return 0;
        if (++retry >= MAX_RETRY)
        {
            printf("remote response ack timeout\n");
            return -1;
        }
        rto *= 2;
        if (rto > 5.0) rto = 5.0;
    }
}

static void rdt_server_multi_wave(rdt_server_client_t *cs)
{
    rdt_session_t *s = &cs->sess;
    double rto = 0.5;
    int retry = 0;
    uint32_t client_fin_seq = s->client_fin_seq;
    s->client_fin_seq = 0;

    while (client_fin_seq == 0)
    {
        char pkt[HEADER_SIZE + MAX_PAYLOAD];
        int n = rdt_server_queue_pop(cs, pkt, sizeof(pkt), 1000);
        if (n < 0)
        {
            if (++retry >= MAX_RETRY)
            {
                printf("remote wait client fin timeout\n");
                return;
            }
            continue;
        }
        if (n == HEADER_SIZE && rdt_verify_packet(pkt, n))
        {
            rdt_hdr_t *h = (rdt_hdr_t *)pkt;
            if (ntohs(h->flags) & PKT_FIN)
            {
                client_fin_seq = ntohl(h->seq);
                break;
            }
        }
    }

    rdt_send_ack(s, client_fin_seq + 1);
    printf("remote server ack client FIN\n");

    uint32_t server_fin_seq = s->snd_seq;
    rdt_send_ctrl(s, server_fin_seq, 0, PKT_FIN);
    printf("remote server send FIN, seq=%u\n", server_fin_seq);

    retry = 0;
    rto = 0.5;
    while (1)
    {
        rdt_hdr_t h;
        if (rdt_server_queue_header(cs, &h, rdt_rto_ms(rto)) == 0)
        {
            uint16_t flags = ntohs(h.flags);
            if ((flags & PKT_FIN) && ntohl(h.seq) == client_fin_seq)
            {
                rdt_send_ack(s, client_fin_seq + 1);
                continue;
            }
            if ((flags & PKT_ACK) && ntohl(h.ack) == server_fin_seq + 1)
                break;
            continue;
        }
        if (++retry >= MAX_RETRY)
        {
            printf("remote fin ack timeout\n");
            break;
        }
        rdt_send_ctrl(s, server_fin_seq, 0, PKT_FIN);
        rto *= 2;
        if (rto > 5.0) rto = 5.0;
    }
    printf("remote wave ok\n");
}

static void *rdt_server_client_worker(void *arg)
{
    rdt_server_client_t *cs = (rdt_server_client_t *)arg;
    rdt_session_t *s = &cs->sess;

    if (rdt_server_multi_handshake(cs) < 0)
    {
        rdt_server_cleanup_client(cs);
        return NULL;
    }
    printf("remote client connected: %s:%u\n",
           inet_ntoa(s->peer.sin_addr),
           (unsigned)ntohs(s->peer.sin_port));

    int timed_out = 0;
    char cmd[256];
    while (1)
    {
        int n = rdt_server_multi_recv_command(cs, cmd,
                                              (int)sizeof(cmd) - 1);
        if (n == -2)
            break;
        if (n < 0)
        {
            timed_out = 1;
            break;
        }
        cmd[n] = '\0';
        printf("remote cmd: %s\n", cmd);

        if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0)
        {
            rdt_server_multi_send_message(cs, "ok\n", PKT_CMD_RESP);
            break;
        }

        if (g_on_command != NULL)
            g_on_command(cmd);
        rdt_server_multi_send_message(cs, "ok\n", PKT_CMD_RESP);
    }

    if (!timed_out)
        rdt_server_multi_wave(cs);
    else
        printf("remote session timeout, skip wave\n");

    rdt_server_cleanup_client(cs);
    printf("remote client disconnected\n");
    return NULL;
}

static void *rdt_server_dispatcher_thread(void *arg)
{
    (void)arg;
    while (1)
    {
        char pkt[HEADER_SIZE + MAX_PAYLOAD];
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        int n = recvfrom(g_multi_sockfd, pkt, sizeof(pkt), 0,
                         (struct sockaddr *)&peer, &peer_len);
        if (n < HEADER_SIZE)
            continue;
        if (!rdt_verify_packet(pkt, n))
            continue;

        rdt_hdr_t *h = (rdt_hdr_t *)pkt;
        uint16_t flags = ntohs(h->flags);

        pthread_mutex_lock(&g_clients_lock);
        rdt_server_client_t *cs = rdt_server_find_client_locked(&peer);
        if (cs == NULL && (flags & PKT_SYN))
            cs = rdt_server_create_client_locked(&peer);
        if (cs == NULL && (flags & PKT_SYN))
            printf("remote client table full, drop connection\n");
        if (cs != NULL)
            rdt_server_queue_push(cs, pkt, n);
        pthread_mutex_unlock(&g_clients_lock);
    }
    return NULL;
}

int rdt_server_multi_start(int port, rdt_server_command_cb cb)
{
    if (g_multi_sockfd >= 0)
        return -1;

    g_on_command = cb;
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("remote multi socket fail");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("remote multi bind fail");
        close(sockfd);
        return -1;
    }

    g_multi_sockfd = sockfd;
    pthread_t tid;
    if (pthread_create(&tid, NULL, rdt_server_dispatcher_thread, NULL) != 0)
    {
        perror("remote multi thread create fail");
        close(sockfd);
        g_multi_sockfd = -1;
        return -1;
    }
    pthread_detach(tid);
    printf("remote multi server on port %d\n", port);
    return 0;
}
