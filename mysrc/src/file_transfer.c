#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../../lvgl/lvgl.h"
#include "../inc/file_transfer.h"
#include "../inc/rdt_core.h"

#define FT_LOCAL_DIR "/tapir"
#define FT_NAME_MAX 256
#define FT_MAX_FILES 64
#define FT_STATUS_MAX 160
#define FT_HEARTBEAT_INTERVAL_S 5

typedef enum {
    FT_OP_NONE = 0,
    FT_OP_CONNECT,
    FT_OP_DISCONNECT,
    FT_OP_LIST,
    FT_OP_UPLOAD,
    FT_OP_DOWNLOAD,
    FT_OP_CD,
    FT_OP_CD_UP,
    FT_OP_RM
} ft_op_t;

/* ---------- 网络与工作线程共享状态 ---------- */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;

static ft_op_t g_req = FT_OP_NONE;
static char g_req_arg[FT_NAME_MAX] = {0};
static int g_busy = 0;
static int g_connected = 0;
static int g_thread_started = 0;

static int g_remote_dirty = 0;
static int g_local_dirty = 0;
static int g_remote_count = 0;
static char g_remote_names[FT_MAX_FILES][FT_NAME_MAX];
static int g_remote_is_dir[FT_MAX_FILES] = {0};
static char g_remote_cwd[512] = ".";
static char g_status[FT_STATUS_MAX] = "Not connected";
static char g_log[FT_STATUS_MAX] = "";
static long g_done_bytes = 0;
static long g_total_bytes = 0;
static int g_percent = 0;
static long g_retrans_count = 0;

/* 仅工作线程访问的 RDT 会话 */
static rdt_session_t g_rdt;

/* ---------- UI 状态 ---------- */
static lv_obj_t *ft_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *ip_label = NULL;
static lv_obj_t *port_label = NULL;
static lv_obj_t *progress_label = NULL;
static lv_obj_t *progress_fill = NULL;
static lv_obj_t *log_label = NULL;
static lv_obj_t *remote_list = NULL;
static lv_obj_t *local_list = NULL;
static lv_obj_t *connect_btn = NULL;
static lv_obj_t *get_btn = NULL;
static lv_obj_t *put_btn = NULL;
static lv_obj_t *rm_btn = NULL;
static lv_obj_t *remote_title = NULL;
static lv_obj_t *g_remote_btns[FT_MAX_FILES] = {0};
static lv_obj_t *g_local_btns[FT_MAX_FILES] = {0};

static char g_ip[32] = "172.5.1.159";
static char g_port[8] = "8000";
static int g_active_field = 0;   /* 0 = IP, 1 = 端口 */
static int g_ui_ready = 0;
static int g_selected_remote = -1;
static int g_selected_local = -1;
static char g_local_names[FT_MAX_FILES][FT_NAME_MAX];
static int g_local_count = 0;

/* ---------- 状态写入辅助 ---------- */
static void ft_set_status(const char *fmt, ...)
{
    va_list ap;
    char buf[FT_STATUS_MAX];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_lock);
    snprintf(g_status, sizeof(g_status), "%s", buf);
    pthread_mutex_unlock(&g_lock);
}

static void ft_set_log(const char *fmt, ...)
{
    va_list ap;
    char buf[FT_STATUS_MAX];

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&g_lock);
    snprintf(g_log, sizeof(g_log), "%s", buf);
    pthread_mutex_unlock(&g_lock);
}

static void ft_set_progress(long done, long total)
{
    pthread_mutex_lock(&g_lock);
    g_done_bytes = done;
    g_total_bytes = total;
    g_percent = (total > 0) ? (int)(done * 100 / total) : 0;
    if (g_percent > 100) g_percent = 100;
    pthread_mutex_unlock(&g_lock);
}

static void ft_progress_cb(long done, long total, long retrans)
{
    pthread_mutex_lock(&g_lock);
    g_done_bytes = done;
    g_total_bytes = total;
    g_retrans_count = retrans;
    g_percent = (total > 0) ? (int)(done * 100 / total) : 0;
    if (g_percent > 100) g_percent = 100;
    pthread_mutex_unlock(&g_lock);
}

static void ft_mark_local_dirty(void)
{
    pthread_mutex_lock(&g_lock);
    g_local_dirty = 1;
    pthread_mutex_unlock(&g_lock);
}

/* ---------- 工作线程操作（协议逻辑全部在 rdt_client） ---------- */
static void ft_do_connect(const char *arg)
{
    if (g_connected)
    {
        ft_set_status("Already connected");
        return;
    }

    char ip[32] = {0};
    char port_str[8] = {0};
    if (arg != NULL && arg[0] != '\0')
        sscanf(arg, "%31s %7s", ip, port_str);
    if (ip[0] == '\0')
        snprintf(ip, sizeof(ip), "%s", g_ip);
    if (port_str[0] == '\0')
        snprintf(port_str, sizeof(port_str), "%s", g_port);

    int port = atoi(port_str);
    if (port <= 0 || port > 65535)
    {
        ft_set_status("Invalid port");
        return;
    }

    ft_set_status("Connecting %s:%d", ip, port);
    if (rdt_client_connect(&g_rdt, ip, port) < 0)
    {
        ft_set_status("Connect failed");
        return;
    }

    pthread_mutex_lock(&g_lock);
    g_connected = 1;
    g_remote_count = 0;
    g_retrans_count = 0;
    g_remote_dirty = 1;
    snprintf(g_remote_cwd, sizeof(g_remote_cwd), ".");
    pthread_mutex_unlock(&g_lock);
    ft_set_progress(0, 0);
    ft_set_status("Connected %s:%d", ip, port);
    ft_set_log("Connected");
}

static void ft_do_disconnect(void)
{
    if (!g_connected)
    {
        ft_set_status("Not connected");
        return;
    }

    rdt_client_disconnect(&g_rdt);
    pthread_mutex_lock(&g_lock);
    g_connected = 0;
    g_remote_count = 0;
    g_remote_dirty = 1;
    snprintf(g_remote_cwd, sizeof(g_remote_cwd), ".");
    pthread_mutex_unlock(&g_lock);
    ft_set_progress(0, 0);
    ft_set_status("Disconnected");
    ft_set_log("Connection closed");
}

static void ft_do_list(void)
{
    if (!g_connected)
    {
        ft_set_status("Not connected");
        return;
    }

    ft_set_status("Fetching remote list");
    if (rdt_client_send_command(&g_rdt, "ls") < 0)
    {
        ft_set_status("ls command failed");
        return;
    }

    char buf[2048] = {0};
    int n = rdt_client_recv_message(&g_rdt, buf, (int)sizeof(buf) - 1);
    if (n <= 0)
    {
        pthread_mutex_lock(&g_lock);
        g_remote_count = 0;
        g_remote_dirty = 1;
        pthread_mutex_unlock(&g_lock);
        ft_set_status("Remote list empty or timeout");
        return;
    }
    buf[n] = '\0';

    if (strncmp(buf, "cannot", 6) == 0 || strncmp(buf, "error", 5) == 0)
    {
        char msg[FT_STATUS_MAX] = {0};
        snprintf(msg, sizeof(msg), "%s", buf);
        msg[strcspn(msg, "\r\n")] = '\0';
        pthread_mutex_lock(&g_lock);
        g_remote_count = 0;
        g_remote_dirty = 1;
        pthread_mutex_unlock(&g_lock);
        ft_set_status("Server: %s", msg);
        return;
    }

    pthread_mutex_lock(&g_lock);
    int count = 0;
    char *p = buf;
    while (*p && count < FT_MAX_FILES)
    {
        char *nl = strchr(p, '\n');
        if (nl != NULL)
            *nl = '\0';
        if (p[0] != '\0')
        {
            size_t len = strlen(p);
            int is_dir = 0;
            if (len > 0 && p[len - 1] == '/')
            {
                is_dir = 1;
                p[len - 1] = '\0';
            }
            snprintf(g_remote_names[count], FT_NAME_MAX, "%s", p);
            g_remote_is_dir[count] = is_dir;
            count++;
        }
        if (nl == NULL)
            break;
        p = nl + 1;
    }
    g_remote_count = count;
    g_remote_dirty = 1;
    pthread_mutex_unlock(&g_lock);
    ft_set_status("Remote %d files", count);
    ft_set_log("Remote list refreshed");
}

static void ft_do_cd(const char *dir)
{
    if (!g_connected)
    {
        ft_set_status("Not connected");
        return;
    }

    char cmd[FT_NAME_MAX + 8];
    snprintf(cmd, sizeof(cmd), "cd %s", dir);
    ft_set_status("Entering %s", dir);

    if (rdt_client_send_command(&g_rdt, cmd) < 0)
    {
        ft_set_status("cd command failed");
        return;
    }

    char resp[512] = {0};
    int n = rdt_client_recv_message(&g_rdt, resp, (int)sizeof(resp) - 1);
    if (n <= 0)
    {
        ft_set_status("cd timeout");
        return;
    }
    resp[n] = '\0';
    resp[strcspn(resp, "\r\n")] = '\0';

    if (strncmp(resp, "error", 5) == 0)
    {
        ft_set_status("Server: %s", resp);
        return;
    }

    pthread_mutex_lock(&g_lock);
    snprintf(g_remote_cwd, sizeof(g_remote_cwd), "%s", resp);
    pthread_mutex_unlock(&g_lock);
    ft_do_list();
}

static void ft_do_rm(const char *name)
{
    if (!g_connected)
    {
        ft_set_status("Not connected");
        return;
    }

    char cmd[FT_NAME_MAX + 8];
    snprintf(cmd, sizeof(cmd), "rm %s", name);
    ft_set_status("Deleting %s", name);

    if (rdt_client_send_command(&g_rdt, cmd) < 0)
    {
        ft_set_status("rm command failed");
        return;
    }

    char resp[256] = {0};
    int n = rdt_client_recv_message(&g_rdt, resp, (int)sizeof(resp) - 1);
    if (n <= 0)
    {
        ft_set_status("rm timeout");
        return;
    }
    resp[n] = '\0';
    resp[strcspn(resp, "\r\n")] = '\0';

    if (strncmp(resp, "error", 5) == 0)
    {
        ft_set_status("Server: %s", resp);
        return;
    }

    ft_set_status("Removed %s", name);
    ft_do_list();
}

static void ft_do_download(const char *name)
{
    if (!g_connected)
    {
        ft_set_status("Not connected");
        return;
    }

    char cmd[FT_NAME_MAX + 8];
    snprintf(cmd, sizeof(cmd), "get %s", name);
    ft_set_status("Downloading %s", name);
    ft_set_log("Downloading");

    if (rdt_client_send_command(&g_rdt, cmd) < 0)
    {
        ft_set_status("Download command failed");
        return;
    }

    if (rdt_client_recv_file(&g_rdt, FT_LOCAL_DIR, ft_progress_cb) < 0)
    {
        ft_set_status("Download failed: %s", name);
        return;
    }

    ft_set_status("Downloaded %s", name);
    ft_set_log("Downloaded");
    ft_mark_local_dirty();
}

static void ft_do_upload(const char *path)
{
    if (!g_connected)
    {
        ft_set_status("Not connected");
        return;
    }

    const char *name = strrchr(path, '/');
    name = name != NULL ? name + 1 : path;

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
    {
        ft_set_status("Local file not found: %s", name);
        return;
    }

    ft_set_status("Uploading %s", name);
    ft_set_log("Uploading");

    if (rdt_client_send_command(&g_rdt, "put") < 0)
    {
        ft_set_status("put command failed");
        return;
    }

    if (rdt_client_send_file(&g_rdt, path, ft_progress_cb) < 0)
    {
        ft_set_status("Upload failed: %s", name);
        return;
    }

    pthread_mutex_lock(&g_lock);
    long retrans = g_rdt.retrans_count;
    pthread_mutex_unlock(&g_lock);
    ft_set_status("Uploaded %s", name);
    ft_set_log("Uploaded, retrans=%ld", retrans);
}

/* ---------- 工作线程 ---------- */
static int ft_request(ft_op_t op, const char *arg)
{
    pthread_mutex_lock(&g_lock);
    if (g_busy || g_req != FT_OP_NONE)
    {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    g_busy = 1;
    g_req = op;
    if (arg != NULL)
        snprintf(g_req_arg, sizeof(g_req_arg), "%s", arg);
    else
        g_req_arg[0] = '\0';
    pthread_cond_signal(&g_cond);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static void *ft_worker(void *arg)
{
    (void)arg;
    while (1)
    {
        pthread_mutex_lock(&g_lock);
        while (g_req == FT_OP_NONE)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += FT_HEARTBEAT_INTERVAL_S;
            int rc = pthread_cond_timedwait(&g_cond, &g_lock, &ts);
            if (rc == ETIMEDOUT)
            {
                int connected = g_connected;
                pthread_mutex_unlock(&g_lock);
                if (connected)
                    rdt_client_send_heartbeat(&g_rdt);
                pthread_mutex_lock(&g_lock);
            }
        }

        ft_op_t op = g_req;
        char arg_buf[FT_NAME_MAX];
        snprintf(arg_buf, sizeof(arg_buf), "%s", g_req_arg);
        g_req = FT_OP_NONE;
        pthread_mutex_unlock(&g_lock);

        switch (op)
        {
        case FT_OP_CONNECT:
            ft_do_connect(arg_buf);
            break;
        case FT_OP_DISCONNECT:
            ft_do_disconnect();
            break;
        case FT_OP_LIST:
            ft_do_list();
            break;
        case FT_OP_UPLOAD:
            ft_do_upload(arg_buf);
            break;
        case FT_OP_DOWNLOAD:
            ft_do_download(arg_buf);
            break;
        case FT_OP_CD:
            ft_do_cd(arg_buf);
            break;
        case FT_OP_CD_UP:
            ft_do_cd("..");
            break;
        case FT_OP_RM:
            ft_do_rm(arg_buf);
            break;
        default:
            break;
        }

        pthread_mutex_lock(&g_lock);
        g_busy = 0;
        pthread_mutex_unlock(&g_lock);
    }
    return NULL;
}

/* ---------- 本地文件列表 ---------- */
static void ft_scan_local(void)
{
    g_local_count = 0;
    DIR *dir = opendir(FT_LOCAL_DIR);
    if (dir == NULL)
        return;

    struct dirent *e;
    while ((e = readdir(dir)) != NULL && g_local_count < FT_MAX_FILES)
    {
        if (e->d_name[0] == '.')
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", FT_LOCAL_DIR, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        {
            snprintf(g_local_names[g_local_count], FT_NAME_MAX, "%s", e->d_name);
            g_local_count++;
        }
    }
    closedir(dir);
}

static void local_file_cb(lv_event_t *e);
static void remote_file_cb(lv_event_t *e);

static void ft_rebuild_local_list(void)
{
    if (local_list == NULL)
        return;

    ft_scan_local();
    g_selected_local = -1;
    memset(g_local_btns, 0, sizeof(g_local_btns));
    lv_obj_clean(local_list);
    for (int i = 0; i < g_local_count; i++)
    {
        lv_obj_t *btn = lv_list_add_btn(local_list, LV_SYMBOL_FILE, g_local_names[i]);
        lv_obj_add_event_cb(btn, local_file_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        g_local_btns[i] = btn;
    }
}

static void ft_rebuild_remote_list(void)
{
    if (remote_list == NULL)
        return;

    char names[FT_MAX_FILES][FT_NAME_MAX];
    int dirs[FT_MAX_FILES] = {0};
    int count = 0;
    pthread_mutex_lock(&g_lock);
    count = g_remote_count;
    for (int i = 0; i < count; i++)
    {
        memcpy(names[i], g_remote_names[i], FT_NAME_MAX);
        dirs[i] = g_remote_is_dir[i];
    }
    pthread_mutex_unlock(&g_lock);

    g_selected_remote = -1;
    memset(g_remote_btns, 0, sizeof(g_remote_btns));
    lv_obj_clean(remote_list);

    lv_obj_t *up_btn = lv_list_add_btn(remote_list, LV_SYMBOL_DIRECTORY, "..");
    lv_obj_add_event_cb(up_btn, remote_file_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)(-1));

    for (int i = 0; i < count; i++)
    {
        char label[FT_NAME_MAX + 4];
        snprintf(label, sizeof(label), "%s%s", names[i], dirs[i] ? "/" : "");
        lv_obj_t *btn = lv_list_add_btn(remote_list,
            dirs[i] ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, label);
        lv_obj_add_event_cb(btn, remote_file_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        g_remote_btns[i] = btn;
    }
}

/* ---------- UI 回调 ---------- */
static void connect_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    char arg[48];
    snprintf(arg, sizeof(arg), "%s %s", g_ip, g_port);
    if (ft_request(FT_OP_CONNECT, arg) < 0)
        ft_set_status("Busy, wait for current task");
}

static void disconnect_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    if (ft_request(FT_OP_DISCONNECT, NULL) < 0)
        ft_set_status("Busy, wait for current task");
}

static void list_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    if (ft_request(FT_OP_LIST, NULL) < 0)
        ft_set_status("Busy, wait for current task");
}

static void get_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    if (g_selected_remote < 0)
    {
        ft_set_status("Select a remote file");
        return;
    }

    char name[FT_NAME_MAX] = {0};
    pthread_mutex_lock(&g_lock);
    if (g_selected_remote < g_remote_count && !g_remote_is_dir[g_selected_remote])
        snprintf(name, sizeof(name), "%s", g_remote_names[g_selected_remote]);
    pthread_mutex_unlock(&g_lock);

    if (name[0] == '\0')
    {
        ft_set_status("Select a remote file");
        return;
    }
    if (ft_request(FT_OP_DOWNLOAD, name) < 0)
        ft_set_status("Busy, wait for current task");
}

static void put_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    if (g_selected_local < 0 || g_selected_local >= g_local_count)
    {
        ft_set_status("Select a local file");
        return;
    }

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FT_LOCAL_DIR, g_local_names[g_selected_local]);
    if (ft_request(FT_OP_UPLOAD, path) < 0)
        ft_set_status("Busy, wait for current task");
}

static void rm_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    if (g_selected_remote < 0)
    {
        ft_set_status("Select a remote file");
        return;
    }

    char name[FT_NAME_MAX] = {0};
    pthread_mutex_lock(&g_lock);
    if (g_selected_remote < g_remote_count && !g_remote_is_dir[g_selected_remote])
        snprintf(name, sizeof(name), "%s", g_remote_names[g_selected_remote]);
    pthread_mutex_unlock(&g_lock);

    if (name[0] == '\0')
    {
        ft_set_status("Select a remote file");
        return;
    }
    if (ft_request(FT_OP_RM, name) < 0)
        ft_set_status("Busy, wait for current task");
}

static void local_file_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= g_local_count)
        return;

    g_selected_local = idx;
    g_selected_remote = -1;
    for (int i = 0; i < FT_MAX_FILES; i++)
    {
        if (g_local_btns[i] != NULL)
            lv_obj_clear_state(g_local_btns[i], LV_STATE_CHECKED);
    }
    for (int i = 0; i < FT_MAX_FILES; i++)
    {
        if (g_remote_btns[i] != NULL)
            lv_obj_clear_state(g_remote_btns[i], LV_STATE_CHECKED);
    }
    if (g_local_btns[idx] != NULL)
        lv_obj_add_state(g_local_btns[idx], LV_STATE_CHECKED);
    ft_set_status("Selected %s", g_local_names[idx]);
}

static void remote_file_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (idx < 0)
    {
        if (ft_request(FT_OP_CD_UP, NULL) < 0)
            ft_set_status("Busy, wait for current task");
        return;
    }

    char name[FT_NAME_MAX] = {0};
    int is_dir = 0;
    pthread_mutex_lock(&g_lock);
    if (idx >= 0 && idx < g_remote_count)
    {
        snprintf(name, sizeof(name), "%s", g_remote_names[idx]);
        is_dir = g_remote_is_dir[idx];
    }
    pthread_mutex_unlock(&g_lock);

    if (name[0] == '\0')
        return;

    if (is_dir)
    {
        if (ft_request(FT_OP_CD, name) < 0)
            ft_set_status("Busy, wait for current task");
        return;
    }

    g_selected_remote = idx;
    g_selected_local = -1;
    for (int i = 0; i < FT_MAX_FILES; i++)
    {
        if (g_remote_btns[i] != NULL)
            lv_obj_clear_state(g_remote_btns[i], LV_STATE_CHECKED);
    }
    for (int i = 0; i < FT_MAX_FILES; i++)
    {
        if (g_local_btns[i] != NULL)
            lv_obj_clear_state(g_local_btns[i], LV_STATE_CHECKED);
    }
    if (g_remote_btns[idx] != NULL)
        lv_obj_add_state(g_remote_btns[idx], LV_STATE_CHECKED);
    ft_set_status("Selected %s", name);
}

static void field_ip_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    g_active_field = 0;
    lv_obj_set_style_border_color(ip_label, lv_color_hex(0x22CC44), 0);
    lv_obj_set_style_border_color(port_label, lv_color_hex(0x666666), 0);
}

static void field_port_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    g_active_field = 1;
    lv_obj_set_style_border_color(ip_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_border_color(port_label, lv_color_hex(0x22CC44), 0);
}

static void key_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
        return;
    const char *key = (const char *)lv_event_get_user_data(e);
    if (key == NULL)
        return;

    char *target = g_active_field == 0 ? g_ip : g_port;
    int maxlen = g_active_field == 0 ? (int)sizeof(g_ip) - 1 : (int)sizeof(g_port) - 1;

    if (strcmp(key, "<") == 0)
    {
        int len = (int)strlen(target);
        if (len > 0)
            target[len - 1] = '\0';
    }
    else if (strcmp(key, "C") == 0)
    {
        target[0] = '\0';
    }
    else
    {
        if ((int)strlen(target) < maxlen)
            strncat(target, key, maxlen - (int)strlen(target));
    }

    if (g_active_field == 0)
        lv_label_set_text(ip_label, g_ip);
    else
        lv_label_set_text(port_label, g_port);
}

/* ---------- LVGL 定时刷新 ---------- */
void file_transfer_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!g_ui_ready)
        return;

    char status[FT_STATUS_MAX];
    char log[FT_STATUS_MAX];
    long done = 0, total = 0;
    long retrans = 0;
    int percent = 0, busy = 0, connected = 0;
    int dirty_remote = 0, dirty_local = 0;
    char cwd[512] = ".";

    pthread_mutex_lock(&g_lock);
    snprintf(status, sizeof(status), "%s", g_status);
    snprintf(log, sizeof(log), "%s", g_log);
    snprintf(cwd, sizeof(cwd), "%s", g_remote_cwd);
    done = g_done_bytes;
    total = g_total_bytes;
    percent = g_percent;
    retrans = g_retrans_count;
    busy = g_busy;
    connected = g_connected;
    dirty_remote = g_remote_dirty;
    dirty_local = g_local_dirty;
    g_remote_dirty = 0;
    g_local_dirty = 0;
    pthread_mutex_unlock(&g_lock);

    char ip_buf[40];
    char port_buf[16];
    snprintf(ip_buf, sizeof(ip_buf), "%s%s", g_ip, g_active_field == 0 ? "_" : "");
    snprintf(port_buf, sizeof(port_buf), "%s%s", g_port, g_active_field == 1 ? "_" : "");
    if (ip_label != NULL)
    {
        lv_label_set_text(ip_label, ip_buf);
        lv_obj_set_style_border_color(ip_label,
            g_active_field == 0 ? lv_color_hex(0x22CC44) : lv_color_hex(0x666666), 0);
    }
    if (port_label != NULL)
    {
        lv_label_set_text(port_label, port_buf);
        lv_obj_set_style_border_color(port_label,
            g_active_field == 1 ? lv_color_hex(0x22CC44) : lv_color_hex(0x666666), 0);
    }

    if (status_label != NULL)
        lv_label_set_text(status_label, status);
    if (log_label != NULL)
        lv_label_set_text(log_label, log);
    if (remote_title != NULL)
    {
        char rbuf[600];
        snprintf(rbuf, sizeof(rbuf), "Remote: %s", cwd);
        lv_label_set_text(remote_title, rbuf);
    }
    if (connect_btn != NULL)
    {
        if (busy)
            lv_obj_add_state(connect_btn, LV_STATE_DISABLED);
        else
            lv_obj_clear_state(connect_btn, LV_STATE_DISABLED);
    }
    if (get_btn != NULL)
    {
        if (connected && !busy && g_selected_remote >= 0)
            lv_obj_clear_state(get_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(get_btn, LV_STATE_DISABLED);
    }
    if (put_btn != NULL)
    {
        if (connected && !busy && g_selected_local >= 0)
            lv_obj_clear_state(put_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(put_btn, LV_STATE_DISABLED);
    }
    if (rm_btn != NULL)
    {
        if (connected && !busy && g_selected_remote >= 0)
            lv_obj_clear_state(rm_btn, LV_STATE_DISABLED);
        else
            lv_obj_add_state(rm_btn, LV_STATE_DISABLED);
    }

    if (progress_label != NULL)
    {
        char buf[96];
        if (total > 0)
        {
            snprintf(buf, sizeof(buf), "%d%%  %.1fKB / %.1fKB  retrans=%ld",
                     percent, done / 1024.0, total / 1024.0, retrans);
        }
        else
        {
            snprintf(buf, sizeof(buf), "%d%%  retrans=%ld", percent, retrans);
        }
        lv_label_set_text(progress_label, buf);
    }

    if (progress_fill != NULL)
    {
        int w = 0;
        if (total > 0)
            w = (int)(done * 410 / total);
        if (w > 410) w = 410;
        lv_obj_set_width(progress_fill, w);
    }

    if (dirty_remote)
        ft_rebuild_remote_list();
    if (dirty_local)
        ft_rebuild_local_list();
}

/* ---------- 界面创建 ---------- */
void draw_file_transfer_on_screen(lv_obj_t *parent)
{
    if (g_ui_ready)
        return;

    ft_screen = parent;
    lv_obj_set_style_bg_color(ft_screen, lv_color_hex(0x101820), 0);

    lv_obj_t *title = lv_label_create(ft_screen);
    lv_label_set_text(title, "File Transfer");
    lv_obj_set_pos(title, 10, 5);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFD700), 0);

    status_label = lv_label_create(ft_screen);
    lv_obj_set_size(status_label, 410, 28);
    lv_obj_set_pos(status_label, 10, 35);
    lv_label_set_text(status_label, "Not connected");
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *ip_title = lv_label_create(ft_screen);
    lv_label_set_text(ip_title, "Server IP");
    lv_obj_set_pos(ip_title, 10, 70);
    lv_obj_set_style_text_font(ip_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ip_title, lv_color_hex(0xAAAAAA), 0);

    ip_label = lv_label_create(ft_screen);
    lv_obj_set_size(ip_label, 300, 28);
    lv_obj_set_pos(ip_label, 110, 68);
    lv_label_set_text(ip_label, g_ip);
    lv_obj_set_style_text_font(ip_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(ip_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(ip_label, 1, 0);
    lv_obj_set_style_border_color(ip_label, lv_color_hex(0x22CC44), 0);
    lv_obj_set_style_bg_color(ip_label, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_opa(ip_label, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(ip_label, field_ip_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(ip_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *port_title = lv_label_create(ft_screen);
    lv_label_set_text(port_title, "Port");
    lv_obj_set_pos(port_title, 10, 106);
    lv_obj_set_style_text_font(port_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(port_title, lv_color_hex(0xAAAAAA), 0);

    port_label = lv_label_create(ft_screen);
    lv_obj_set_size(port_label, 120, 28);
    lv_obj_set_pos(port_label, 110, 104);
    lv_label_set_text(port_label, g_port);
    lv_obj_set_style_text_font(port_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(port_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(port_label, 1, 0);
    lv_obj_set_style_border_color(port_label, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_color(port_label, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_opa(port_label, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(port_label, field_port_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(port_label, LV_OBJ_FLAG_CLICKABLE);

    connect_btn = lv_button_create(ft_screen);
    lv_obj_set_size(connect_btn, 190, 40);
    lv_obj_set_pos(connect_btn, 10, 143);
    lv_obj_add_event_cb(connect_btn, connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_center(connect_label);
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_24, 0);

    lv_obj_t *disconnect_btn = lv_button_create(ft_screen);
    lv_obj_set_size(disconnect_btn, 190, 40);
    lv_obj_set_pos(disconnect_btn, 210, 143);
    lv_obj_add_event_cb(disconnect_btn, disconnect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *disconnect_label = lv_label_create(disconnect_btn);
    lv_label_set_text(disconnect_label, "Disconnect");
    lv_obj_center(disconnect_label);
    lv_obj_set_style_text_font(disconnect_label, &lv_font_montserrat_24, 0);

    static const char *keys[] = {
        "1", "2", "3", "<",
        "4", "5", "6", "C",
        "7", "8", "9", ".",
        "0"
    };
    for (int i = 0; i < 13; i++)
    {
        int row = i / 4;
        int col = i % 4;
        lv_obj_t *btn = lv_button_create(ft_screen);
        lv_obj_set_size(btn, 70, 36);
        lv_obj_set_pos(btn, 10 + col * 76, 195 + row * 42);
        lv_obj_add_event_cb(btn, key_click_cb, LV_EVENT_CLICKED,
                            (void *)keys[i]);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, keys[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    }

    progress_label = lv_label_create(ft_screen);
    lv_obj_set_size(progress_label, 410, 26);
    lv_obj_set_pos(progress_label, 10, 360);
    lv_label_set_text(progress_label, "0%");
    lv_obj_set_style_text_font(progress_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(progress_label, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t *progress_bg = lv_obj_create(ft_screen);
    lv_obj_set_size(progress_bg, 410, 16);
    lv_obj_set_pos(progress_bg, 10, 392);
    lv_obj_set_style_radius(progress_bg, 4, 0);
    lv_obj_set_style_bg_color(progress_bg, lv_color_hex(0x22303C), 0);
    lv_obj_set_style_border_width(progress_bg, 0, 0);
    lv_obj_set_style_pad_all(progress_bg, 0, 0);

    progress_fill = lv_obj_create(ft_screen);
    lv_obj_set_size(progress_fill, 0, 16);
    lv_obj_set_pos(progress_fill, 10, 392);
    lv_obj_set_style_radius(progress_fill, 4, 0);
    lv_obj_set_style_bg_color(progress_fill, lv_color_hex(0x22CC44), 0);
    lv_obj_set_style_border_width(progress_fill, 0, 0);
    lv_obj_set_style_pad_all(progress_fill, 0, 0);

    log_label = lv_label_create(ft_screen);
    lv_obj_set_size(log_label, 410, 170);
    lv_obj_set_pos(log_label, 10, 418);
    lv_label_set_text(log_label, "");
    lv_obj_set_style_bg_color(log_label, lv_color_hex(0x0C141C), 0);
    lv_obj_set_style_bg_opa(log_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(log_label, 6, 0);
    lv_obj_set_style_text_color(log_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(log_label, &lv_font_montserrat_24, 0);

    remote_title = lv_label_create(ft_screen);
    lv_label_set_text(remote_title, "Remote: .");
    lv_obj_set_pos(remote_title, 440, 5);
    lv_obj_set_size(remote_title, 300, 26);
    lv_obj_set_style_text_font(remote_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(remote_title, lv_color_hex(0x88CCFF), 0);

    lv_obj_t *refresh_btn = lv_button_create(ft_screen);
    lv_obj_set_size(refresh_btn, 170, 34);
    lv_obj_set_pos(refresh_btn, 760, 2);
    lv_obj_add_event_cb(refresh_btn, list_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, "Refresh");
    lv_obj_center(refresh_label);
    lv_obj_set_style_text_font(refresh_label, &lv_font_montserrat_24, 0);

    remote_list = lv_list_create(ft_screen);
    lv_obj_set_size(remote_list, 574, 258);
    lv_obj_set_pos(remote_list, 440, 42);

    lv_obj_t *local_title = lv_label_create(ft_screen);
    lv_label_set_text(local_title, "Local Files");
    lv_obj_set_pos(local_title, 440, 310);
    lv_obj_set_style_text_font(local_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(local_title, lv_color_hex(0xCCFF88), 0);

    get_btn = lv_button_create(ft_screen);
    lv_obj_set_size(get_btn, 100, 34);
    lv_obj_set_pos(get_btn, 650, 310);
    lv_obj_add_event_cb(get_btn, get_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *get_label = lv_label_create(get_btn);
    lv_label_set_text(get_label, "Get");
    lv_obj_center(get_label);
    lv_obj_set_style_text_font(get_label, &lv_font_montserrat_24, 0);

    put_btn = lv_button_create(ft_screen);
    lv_obj_set_size(put_btn, 100, 34);
    lv_obj_set_pos(put_btn, 770, 310);
    lv_obj_add_event_cb(put_btn, put_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *put_label = lv_label_create(put_btn);
    lv_label_set_text(put_label, "Put");
    lv_obj_center(put_label);
    lv_obj_set_style_text_font(put_label, &lv_font_montserrat_24, 0);

    rm_btn = lv_button_create(ft_screen);
    lv_obj_set_size(rm_btn, 100, 34);
    lv_obj_set_pos(rm_btn, 890, 310);
    lv_obj_add_event_cb(rm_btn, rm_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rm_label = lv_label_create(rm_btn);
    lv_label_set_text(rm_label, "RM");
    lv_obj_center(rm_label);
    lv_obj_set_style_text_font(rm_label, &lv_font_montserrat_24, 0);

    local_list = lv_list_create(ft_screen);
    lv_obj_set_size(local_list, 574, 240);
    lv_obj_set_pos(local_list, 440, 350);

    ft_scan_local();
    ft_rebuild_local_list();
    ft_rebuild_remote_list();

    if (!g_thread_started)
    {
        pthread_t tid;
        if (pthread_create(&tid, NULL, ft_worker, NULL) == 0)
            g_thread_started = 1;
    }
    lv_timer_create(file_transfer_timer_cb, 100, NULL);
    g_ui_ready = 1;
}
