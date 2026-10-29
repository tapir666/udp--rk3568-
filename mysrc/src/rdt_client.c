#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include "../inc/rdt_core.h"

static int rdt_client_handshake(rdt_session_t *s, uint32_t *client_isn,
                                uint32_t *server_isn)
{
    char pkt[HEADER_SIZE];
    double rto = 0.5;
    int retry = 0;

    *client_isn = rdt_make_isn();
    rdt_build_packet(pkt, *client_isn, PKT_SYN, NULL);
    send(s->sockfd, pkt, HEADER_SIZE, 0);

    while (1)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            if (recv(s->sockfd, &h, HEADER_SIZE, 0) == HEADER_SIZE)
            {
                if (rdt_verify_packet(&h, HEADER_SIZE))
                {
                    uint16_t flags = ntohs(h.flags);
                    if ((flags & PKT_SYN) && (flags & PKT_ACK) &&
                        ntohl(h.ack) == *client_isn + 1)
                    {
                        *server_isn = ntohl(h.seq);
                        break;
                    }
                }
            }
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
                return -1;
            send(s->sockfd, pkt, HEADER_SIZE, 0);
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
        else
        {
            if (errno == EINTR) continue;
            char tmp[HEADER_SIZE + MAX_PAYLOAD];
            recv(s->sockfd, tmp, sizeof(tmp), MSG_DONTWAIT);
            if (++retry >= MAX_RETRY)
                return -1;
            send(s->sockfd, pkt, HEADER_SIZE, 0);
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
    }

    rdt_build_ack_packet(pkt, *server_isn + 1);
    send(s->sockfd, pkt, HEADER_SIZE, 0);
    return 0;
}

int rdt_client_connect(rdt_session_t *s, const char *ip, int port)
{
    rdt_session_init(s);
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        return -1;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(sockfd);
        return -1;
    }

    s->sockfd = sockfd;
    s->peer = addr;
    s->peer_len = sizeof(addr);

    uint32_t client_isn = 0, server_isn = 0;
    if (rdt_client_handshake(s, &client_isn, &server_isn) < 0)
    {
        close(sockfd);
        s->sockfd = -1;
        return -1;
    }

    s->snd_seq = client_isn + 1;
    s->rcv_expected = server_isn + 1;
    return 0;
}

int rdt_client_send_command(rdt_session_t *s, const char *cmd)
{
    if (s->sockfd < 0)
        return -1;

    int len = (int)strlen(cmd);
    if (len > MAX_PAYLOAD)
        len = MAX_PAYLOAD;

    uint32_t seq = s->snd_seq++;
    char pkt[HEADER_SIZE + MAX_PAYLOAD];
    rdt_build_packet_len(pkt, seq, PKT_CMD, cmd, len);

    double rto = 0.2;
    int retry = 0;
    while (1)
    {
        send(s->sockfd, pkt, HEADER_SIZE + len, 0);
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            if (recv(s->sockfd, &h, HEADER_SIZE, 0) == HEADER_SIZE &&
                rdt_verify_packet(&h, HEADER_SIZE) &&
                (ntohs(h.flags) & PKT_ACK) && ntohl(h.ack) == seq + 1)
                return 0;
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
                return -1;
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
        else
        {
            if (errno == EINTR) continue;
            char tmp[HEADER_SIZE + MAX_PAYLOAD];
            recv(s->sockfd, tmp, sizeof(tmp), MSG_DONTWAIT);
            if (++retry >= MAX_RETRY)
                return -1;
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
    }
}

int rdt_client_recv_message(rdt_session_t *s, char *out, int maxlen)
{
    int wait_retry = 0;
    while (1)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            while (1)
            {
                char pkt[HEADER_SIZE + MAX_PAYLOAD];
                int n = recv(s->sockfd, pkt, sizeof(pkt), MSG_DONTWAIT);
                if (n <= 0)
                    break;
                if (!rdt_verify_packet(pkt, n))
                    continue;

                rdt_hdr_t *h = (rdt_hdr_t *)pkt;
                uint16_t flags = ntohs(h->flags);
                if (flags & PKT_ACK)
                    continue;

                uint32_t seq = ntohl(h->seq);
                if (seq == s->rcv_expected)
                {
                    int plen = n - HEADER_SIZE;
                    if (plen > maxlen) plen = maxlen;
                    if (plen > 0)
                        memcpy(out, pkt + HEADER_SIZE, plen);
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
        else if (ret == 0)
        {
            if (++wait_retry >= 10)
                return 0;
        }
        else
        {
            if (errno == EINTR) continue;
            return 0;
        }
    }
}

int rdt_client_send_heartbeat(rdt_session_t *s)
{
    if (s == NULL || s->sockfd < 0)
        return -1;

    rdt_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.flags = htons(PKT_HEARTBEAT);
    h.window = htons(RWND);
    rdt_set_packet_checksum(&h, NULL, 0);
    send(s->sockfd, &h, HEADER_SIZE, 0);
    return 0;
}

static void rdt_client_wave(rdt_session_t *s, uint32_t fin_seq)
{
    char pkt[HEADER_SIZE];
    double rto = 0.5;
    int retry = 0;
    uint32_t server_fin_seq = 0;

    rdt_build_packet(pkt, fin_seq, PKT_FIN, NULL);
    send(s->sockfd, pkt, HEADER_SIZE, 0);

    while (1)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            if (recv(s->sockfd, &h, HEADER_SIZE, 0) == HEADER_SIZE)
            {
                if (rdt_verify_packet(&h, HEADER_SIZE))
                {
                    uint16_t flags = ntohs(h.flags);
                    if (flags & PKT_FIN)
                    {
                        server_fin_seq = ntohl(h.seq);
                        rdt_build_ack_packet(pkt, server_fin_seq + 1);
                        send(s->sockfd, pkt, HEADER_SIZE, 0);
                        break;
                    }
                    if ((flags & PKT_ACK) && ntohl(h.ack) == fin_seq + 1)
                        break;
                }
            }
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
                break;
            send(s->sockfd, pkt, HEADER_SIZE, 0);
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
        else
        {
            if (errno == EINTR) continue;
            break;
        }
    }

    retry = 0;
    rto = 0.5;
    while (server_fin_seq == 0)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000);
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            if (recv(s->sockfd, &h, HEADER_SIZE, 0) == HEADER_SIZE &&
                rdt_verify_packet(&h, HEADER_SIZE) &&
                (ntohs(h.flags) & PKT_FIN))
            {
                server_fin_seq = ntohl(h.seq);
                rdt_build_ack_packet(pkt, server_fin_seq + 1);
                send(s->sockfd, pkt, HEADER_SIZE, 0);
                break;
            }
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
                break;
        }
        else
        {
            if (errno == EINTR) continue;
            break;
        }
    }

    int tw_retry = 0;
    while (tw_retry < 5)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 200);
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            if (recv(s->sockfd, &h, HEADER_SIZE, 0) == HEADER_SIZE &&
                rdt_verify_packet(&h, HEADER_SIZE) &&
                (ntohs(h.flags) & PKT_FIN) && ntohl(h.seq) == server_fin_seq)
            {
                rdt_build_ack_packet(pkt, server_fin_seq + 1);
                send(s->sockfd, pkt, HEADER_SIZE, 0);
            }
        }
        tw_retry++;
    }
}

int rdt_client_disconnect(rdt_session_t *s)
{
    if (s->sockfd < 0)
        return -1;
    rdt_client_send_command(s, "quit");
    rdt_client_wave(s, s->snd_seq);
    close(s->sockfd);
    s->sockfd = -1;
    return 0;
}

int rdt_client_send_file(rdt_session_t *s, const char *path,
                         rdt_progress_cb cb)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL)
        return -1;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *slash = strrchr(path, '/');
    const char *name = slash != NULL ? slash + 1 : path;
    char info[512];
    snprintf(info, sizeof(info), "%s\n%ld", name, file_size);

    uint32_t info_seq = s->snd_seq++;
    char pkt[HEADER_SIZE + MAX_PAYLOAD];
    rdt_build_packet_len(pkt, info_seq, PKT_FILE_INFO, info,
                         (int)strlen(info));

    double rto = 0.2;
    int retry = 0;
    while (1)
    {
        send(s->sockfd, pkt, HEADER_SIZE + (int)strlen(info), 0);
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            rdt_hdr_t h;
            if (recv(s->sockfd, &h, HEADER_SIZE, 0) == HEADER_SIZE &&
                rdt_verify_packet(&h, HEADER_SIZE) &&
                (ntohs(h.flags) & PKT_ACK) && ntohl(h.ack) == info_seq + 1)
                break;
        }
        else if (ret == 0)
        {
            if (++retry >= MAX_RETRY)
            {
                fclose(fp);
                return -1;
            }
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
        else
        {
            if (errno == EINTR) continue;
            char tmp[HEADER_SIZE + MAX_PAYLOAD];
            recv(s->sockfd, tmp, sizeof(tmp), MSG_DONTWAIT);
            if (++retry >= MAX_RETRY)
            {
                fclose(fp);
                return -1;
            }
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
        }
    }

    struct {
        char data[MAX_PAYLOAD];
        double ts;
        int valid;
        int len;
    } win[WINDOW_SIZE] = {{0}};

    uint32_t base_seq = s->snd_seq;
    uint32_t nextseq = s->snd_seq;
    long sent_total = 0;
    double srtt = 0, rttvar = 0.05;
    uint32_t last_ackno = 0;
    int dup_ack_count = 0;
    char payload[MAX_PAYLOAD];
    s->retrans_count = 0;

    while (base_seq < nextseq || sent_total < file_size)
    {
        while ((int)(nextseq - base_seq) < SEND_WINDOW && sent_total < file_size)
        {
            int n = fread(payload, 1, MAX_PAYLOAD, fp);
            if (n <= 0)
                break;
            rdt_build_packet_len(pkt, nextseq, PKT_DATA, payload, n);
            send(s->sockfd, pkt, HEADER_SIZE + n, 0);
            memcpy(win[nextseq % WINDOW_SIZE].data, payload, n);
            win[nextseq % WINDOW_SIZE].len = n;
            win[nextseq % WINDOW_SIZE].ts = rdt_now_ms();
            win[nextseq % WINDOW_SIZE].valid = 1;
            nextseq++;
            sent_total += n;
            if (cb != NULL)
                cb(sent_total, file_size, s->retrans_count);
        }

        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, rdt_rto_ms(rto));
        if (ret > 0 && (pfd.revents & POLLIN))
        {
            while (1)
            {
                rdt_hdr_t h;
                int n = recv(s->sockfd, &h, HEADER_SIZE, MSG_DONTWAIT);
                if (n <= 0)
                    break;
                if (!rdt_verify_packet(&h, HEADER_SIZE))
                    continue;
                uint16_t flags = ntohs(h.flags);
                if (!(flags & PKT_ACK))
                    continue;
                uint32_t ackno = ntohl(h.ack);
                if (ackno > base_seq)
                {
                    double rtt = rdt_now_ms() - win[base_seq % WINDOW_SIZE].ts;
                    if (srtt == 0)
                        srtt = rtt;
                    else
                        srtt = 0.875 * srtt + 0.125 * rtt;
                    double diff = rtt - srtt;
                    if (diff < 0) diff = -diff;
                    rttvar = 0.75 * rttvar + 0.25 * diff;
                    rto = srtt / 1000.0 + 4 * rttvar / 1000.0;
                    if (rto < 0.02) rto = 0.02;
                    if (rto > 5.0) rto = 5.0;
                    base_seq = ackno;
                    last_ackno = ackno;
                    dup_ack_count = 0;
                }
                else if (ackno == last_ackno && ackno < nextseq)
                {
                    if (++dup_ack_count >= 3)
                    {
                        win[base_seq % WINDOW_SIZE].ts = rdt_now_ms();
                        rdt_build_packet_len(pkt, base_seq, PKT_DATA,
                                             win[base_seq % WINDOW_SIZE].data,
                                             win[base_seq % WINDOW_SIZE].len);
                        send(s->sockfd, pkt,
                             HEADER_SIZE + win[base_seq % WINDOW_SIZE].len, 0);
                        dup_ack_count = 0;
                        s->retrans_count++;
                    }
                }
            }
        }
        else if (ret == 0)
        {
            win[base_seq % WINDOW_SIZE].ts = rdt_now_ms();
            rdt_build_packet_len(pkt, base_seq, PKT_DATA,
                                 win[base_seq % WINDOW_SIZE].data,
                                 win[base_seq % WINDOW_SIZE].len);
            send(s->sockfd, pkt,
                 HEADER_SIZE + win[base_seq % WINDOW_SIZE].len, 0);
            rto *= 2;
            if (rto > 5.0) rto = 5.0;
            s->retrans_count++;
        }
        else
        {
            if (errno == EINTR) continue;
            break;
        }
    }

    s->snd_seq = nextseq;
    fclose(fp);
    if (cb != NULL)
        cb(file_size, file_size, s->retrans_count);
    return 0;
}

int rdt_client_recv_file(rdt_session_t *s, const char *save_dir,
                         rdt_progress_cb cb)
{
    char file_name[RDT_NAME_MAX] = {0};
    long file_size = -1;
    long received = 0;
    FILE *out_file = NULL;
    int info_wait = 0;

    while (file_size < 0)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000);
        if (ret == 0)
        {
            if (++info_wait >= 15)
                return -1;
            continue;
        }
        if (ret < 0)
            continue;
        info_wait = 0;

        while (1)
        {
            char pkt[HEADER_SIZE + MAX_PAYLOAD];
            int n = recv(s->sockfd, pkt, sizeof(pkt), MSG_DONTWAIT);
            if (n <= 0)
                break;
            info_wait = 0;
            if (!rdt_verify_packet(pkt, n))
                continue;

            rdt_hdr_t *h = (rdt_hdr_t *)pkt;
            uint16_t flags = ntohs(h->flags);
            if (flags & PKT_ACK)
                continue;

            if ((flags & PKT_CMD_RESP) && n > HEADER_SIZE)
            {
                s->rcv_expected = ntohl(h->seq) + 1;
                rdt_send_ack(s, s->rcv_expected);
                return -1;
            }

            if ((flags & PKT_FILE_INFO) && n > HEADER_SIZE)
            {
                char info[512] = {0};
                int ilen = n - HEADER_SIZE;
                if (ilen >= (int)sizeof(info))
                    ilen = (int)sizeof(info) - 1;
                memcpy(info, pkt + HEADER_SIZE, ilen);
                char *nl = strchr(info, '\n');
                if (nl != NULL)
                {
                    *nl = '\0';
                    snprintf(file_name, sizeof(file_name), "%s", info);
                    file_size = atol(nl + 1);
                    char *base = strrchr(file_name, '/');
                    if (base != NULL)
                        base++;
                    else
                        base = file_name;
                    char save_path[512];
                    snprintf(save_path, sizeof(save_path), "%s/%s",
                             save_dir, base);
                    out_file = fopen(save_path, "wb");
                    if (out_file == NULL)
                    {
                        s->rcv_expected = ntohl(h->seq) + 1;
                        rdt_send_ack(s, s->rcv_expected);
                        return -1;
                    }
                    if (cb != NULL)
                        cb(0, file_size, 0);
                }
                s->rcv_expected = ntohl(h->seq) + 1;
                rdt_send_ack(s, s->rcv_expected);
                continue;
            }

            uint32_t seq = ntohl(h->seq);
            if (seq == s->rcv_expected)
            {
                int plen = n - HEADER_SIZE;
                if (out_file != NULL && plen > 0)
                    fwrite(pkt + HEADER_SIZE, 1, plen, out_file);
                received += plen;
                s->rcv_expected++;
                while (s->rcv_have[s->rcv_expected % WINDOW_SIZE])
                {
                    if (out_file != NULL)
                        fwrite(s->rcv_reorder[s->rcv_expected % WINDOW_SIZE], 1,
                               s->rcv_len[s->rcv_expected % WINDOW_SIZE],
                               out_file);
                    received += s->rcv_len[s->rcv_expected % WINDOW_SIZE];
                    s->rcv_have[s->rcv_expected % WINDOW_SIZE] = 0;
                    s->rcv_len[s->rcv_expected % WINDOW_SIZE] = 0;
                    s->rcv_expected++;
                }
                rdt_send_ack(s, s->rcv_expected);
                if (cb != NULL)
                    cb(received, file_size, 0);
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

    int data_idle = 0;
    while (received < file_size)
    {
        struct pollfd pfd = { s->sockfd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 1000);
        if (ret == 0)
        {
            if (++data_idle >= 30)
            {
                if (out_file != NULL)
                    fclose(out_file);
                return -1;
            }
            continue;
        }
        if (ret < 0)
            continue;
        data_idle = 0;

        while (1)
        {
            char pkt[HEADER_SIZE + MAX_PAYLOAD];
            int n = recv(s->sockfd, pkt, sizeof(pkt), MSG_DONTWAIT);
            if (n <= 0)
                break;
            data_idle = 0;
            if (!rdt_verify_packet(pkt, n))
                continue;

            rdt_hdr_t *h = (rdt_hdr_t *)pkt;
            uint16_t flags = ntohs(h->flags);
            if (flags & PKT_ACK)
                continue;

            uint32_t seq = ntohl(h->seq);
            if (seq == s->rcv_expected)
            {
                int plen = n - HEADER_SIZE;
                if (out_file != NULL && plen > 0)
                    fwrite(pkt + HEADER_SIZE, 1, plen, out_file);
                received += plen;
                s->rcv_expected++;
                while (s->rcv_have[s->rcv_expected % WINDOW_SIZE])
                {
                    if (out_file != NULL)
                        fwrite(s->rcv_reorder[s->rcv_expected % WINDOW_SIZE], 1,
                               s->rcv_len[s->rcv_expected % WINDOW_SIZE],
                               out_file);
                    received += s->rcv_len[s->rcv_expected % WINDOW_SIZE];
                    s->rcv_have[s->rcv_expected % WINDOW_SIZE] = 0;
                    s->rcv_len[s->rcv_expected % WINDOW_SIZE] = 0;
                    s->rcv_expected++;
                }
                rdt_send_ack(s, s->rcv_expected);
                if (cb != NULL)
                    cb(received, file_size, 0);
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

    if (out_file != NULL)
    {
        fclose(out_file);
        out_file = NULL;
    }

    char stats[128] = {0};
    long retrans = 0;
    int sn = rdt_client_recv_message(s, stats, (int)sizeof(stats) - 1);
    if (sn > 0)
    {
        stats[sn] = '\0';
        char *eq = strchr(stats, '=');
        if (eq != NULL)
            retrans = atol(eq + 1);
    }
    s->retrans_count = retrans;
    if (cb != NULL)
        cb(received, file_size, retrans);
    return 0;
}
