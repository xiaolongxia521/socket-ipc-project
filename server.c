/**
 * TCP 多线程服务器 - Step 3 (D-Bus IPC)
 *
 * 收到客户端消息后，调用 D-Bus 服务操作数据库
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include "dbus-client.h"

/* ==================== 配置常量 ==================== */
#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 100
#define TIMEOUT_SEC 30
#define HEARTBEAT_INTERVAL 5
#define LOG_FILE "server.log"
#define MSG_DELIM '\n'

/* ==================== 日志级别 ==================== */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

static const char* level_names[] = { "DEBUG", "INFO", "WARN", "ERROR" };
static FILE *log_fp = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static int server_fd = -1;
static volatile int running = 1;
static int active_clients = 0;
static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int socket;
    struct sockaddr_in addr;
    time_t last_heartbeat;
    char client_ip[INET_ADDRSTRLEN];
    socklen_t addr_len;
} ClientInfo;

/* ==================== 日志 ==================== */
void log_init(void) {
    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        log_fp = stderr;
        fprintf(stderr, "警告: 无法打开日志文件，使用标准错误输出\n");
    }
}

void log_message(LogLevel level, const char *format, ...) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    pthread_mutex_lock(&log_mutex);
    fprintf(log_fp, "[%s] [%s] ", time_str, level_names[level]);
    va_list args;
    va_start(args, format);
    vfprintf(log_fp, format, args);
    va_end(args);
    fprintf(log_fp, "\n");
    fflush(log_fp);
    pthread_mutex_unlock(&log_mutex);

    if (level >= LOG_INFO) {
        pthread_mutex_lock(&print_mutex);
        printf("[%s] [%s] ", time_str, level_names[level]);
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
        printf("\n");
        pthread_mutex_unlock(&print_mutex);
    }
}

#define LOG_DEBUG(...) log_message(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) log_message(LOG_INFO, __VA_ARGS__)
#define LOG_WARN(...) log_message(LOG_WARN, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_ERROR, __VA_ARGS__)

/* ==================== 信号处理 ==================== */
void sigint_handler(int sig) {
    LOG_INFO("收到信号 %d，正在关闭...", sig);
    running = 0;
    if (server_fd >= 0) close(server_fd);
    server_fd = -1;
}

/* ==================== Socket 工具 ==================== */
ssize_t safe_send(int sock, const void *buf, size_t len, int flags) {
    size_t total = 0;
    while (total < len) {
        ssize_t s = send(sock, (const char*)buf + total, len - total, flags);
        if (s < 0) { if (errno == EINTR) continue; return -1; }
        if (s == 0) return -1;
        total += s;
    }
    return (ssize_t)total;
}

int recv_line(int sock, char *buf, size_t max_len) {
    size_t pos = 0;
    while (pos < max_len - 1) {
        char c;
        ssize_t r = recv(sock, &c, 1, 0);
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return pos > 0 ? (ssize_t)pos : -1;
            return -1;
        }
        buf[pos++] = c;
        if (c == MSG_DELIM) {
            buf[pos] = '\0';
            return (ssize_t)pos;
        }
    }
    buf[pos] = '\0';
    return (ssize_t)pos;
}

int send_heartbeat(int sock) {
    const char *ping = "PING\n";
    return safe_send(sock, ping, strlen(ping), 0) > 0 ? 0 : -1;
}

int check_heartbeat(ClientInfo *client) {
    return (time(NULL) - client->last_heartbeat > HEARTBEAT_INTERVAL * 2) ? -1 : 0;
}

/* ==================== 处理一行消息 ==================== */
int handle_line(ClientInfo *client, char *line) {
    char response[BUFFER_SIZE];

    /* 去掉结尾换行 */
    size_t len = strlen(line);
    if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
        line[--len] = '\0';
    if (len > 0 && line[len-1] == '\r')
        line[--len] = '\0';
    if (len == 0) return 1;

    /* 心跳 - 不打印，直接跳过 */
    if (strcmp(line, "PING") == 0) {
        safe_send(client->socket, "PONG\n", 5, 0);
        return 1;
    }
    if (strcmp(line, "PONG") == 0) return 1;


    LOG_INFO("[%s:%d] 收到: %s", client->client_ip, ntohs(client->addr.sin_port), line);

    /* 构造回复 */
    int rlen = snprintf(response, BUFFER_SIZE, "服务器收到 [%s:%d]: %s\n",
                       client->client_ip, ntohs(client->addr.sin_port), line);

    /* 发给客户端 */
    if (safe_send(client->socket, response, rlen, 0) < 0) {
        LOG_ERROR("[%s:%d] 发送响应失败", client->client_ip, ntohs(client->addr.sin_port));
        return 0;
    }

    /* 通过 D-Bus 存入数据库（核心改动） */
    if (dbus_insert_message(client->client_ip, ntohs(client->addr.sin_port),
                           line, response) != 0) {
        LOG_ERROR("D-Bus InsertMessage 调用失败");
    }

    return 1;
}

/* ==================== 客户端线程 ==================== */
void* handle_client(void* arg) {
    ClientInfo *client = (ClientInfo*)arg;
    char buffer[BUFFER_SIZE];

    pthread_mutex_lock(&count_mutex);
    active_clients++;
    pthread_mutex_unlock(&count_mutex);

    LOG_INFO("[%s:%d] 新连接，当前: %d",
             client->client_ip, ntohs(client->addr.sin_port), active_clients);

    /* 通过 D-Bus 记录连接 */
    dbus_log_connection(client->client_ip, ntohs(client->addr.sin_port), "connect");

    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    client->last_heartbeat = time(NULL);
    time_t last_hb_send = 0;
    int should_exit = 0;

    while (!should_exit && running) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(client->socket, &rfds);
        struct timeval t = { .tv_sec = 1, .tv_usec = 0 };
        int act = select(client->socket + 1, &rfds, NULL, NULL, &t);
        if (act < 0) { if (errno == EINTR) continue; break; }

        if (FD_ISSET(client->socket, &rfds)) {
            int r = recv_line(client->socket, buffer, BUFFER_SIZE);
            if (r > 0) {
                client->last_heartbeat = time(NULL);
                char *start = buffer;
                char *nl;
                while ((nl = strchr(start, MSG_DELIM)) != NULL) {
                    *nl = '\0';
                    if (strlen(start) > 0)
                        if (handle_line(client, start) == 0) { should_exit = 1; break; }
                    start = nl + 1;
                }
            } else if (r == 0) {
                LOG_INFO("[%s:%d] 客户端关闭", client->client_ip, ntohs(client->addr.sin_port));
                dbus_log_connection(client->client_ip, ntohs(client->addr.sin_port), "disconnect");
                break;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) { break; }
                if (check_heartbeat(client) < 0) { LOG_WARN("心跳超时"); break; }
            }
        }

        time_t now = time(NULL);
        if (now - last_hb_send > HEARTBEAT_INTERVAL) {
            if (send_heartbeat(client->socket) < 0) break;
            last_hb_send = now;
        }
        if (check_heartbeat(client) < 0) break;
    }

    close(client->socket);
    free(client);
    pthread_mutex_lock(&count_mutex); active_clients--; pthread_mutex_unlock(&count_mutex);
    LOG_INFO("客户端断开，当前: %d", active_clients);
    return NULL;
}

/* ==================== 主函数 ==================== */
int main(void) {
    struct sockaddr_in addr;

    log_init();
    LOG_INFO("=== TCP 服务器启动 (D-Bus IPC) ===");

    /* 初始化 D-Bus 客户端 */
    if (dbus_client_init() != 0) {
        LOG_ERROR("D-Bus 客户端初始化失败");
        exit(EXIT_FAILURE);
    }
    LOG_INFO("D-Bus 客户端就绪");

    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        LOG_ERROR("socket 创建失败: %s", strerror(errno)); exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind 失败: %s", strerror(errno)); exit(EXIT_FAILURE);
    }
    if (listen(server_fd, SOMAXCONN) < 0) {
        LOG_ERROR("listen 失败: %s", strerror(errno)); exit(EXIT_FAILURE);
    }

    LOG_INFO("监听端口 %d，最大客户端 %d", PORT, MAX_CLIENTS);

    while (running) {
        pthread_mutex_lock(&count_mutex);
        if (active_clients >= MAX_CLIENTS) {
            pthread_mutex_unlock(&count_mutex);
            struct sockaddr_in ca; socklen_t cl = sizeof(ca);
            int ts = accept(server_fd, (struct sockaddr *)&ca, &cl);
            if (ts > 0) { send(ts, "服务器繁忙\n", 9, 0); close(ts); }
            sleep(1); continue;
        }
        pthread_mutex_unlock(&count_mutex);

        ClientInfo *ci = malloc(sizeof(ClientInfo));
        ci->addr_len = sizeof(ci->addr);
        int ns = accept(server_fd, (struct sockaddr *)&ci->addr, &ci->addr_len);
        if (ns < 0) { free(ci); if (errno == EINTR) continue; sleep(1); continue; }

        ci->socket = ns;
        ci->last_heartbeat = time(NULL);
        inet_ntop(AF_INET, &ci->addr.sin_addr, ci->client_ip, INET_ADDRSTRLEN);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, ci) != 0) {
            close(ns); free(ci); continue;
        }
        pthread_detach(tid);
    }

    LOG_INFO("服务器关闭");
    dbus_client_close();
    if (server_fd >= 0) close(server_fd);
    if (log_fp && log_fp != stderr) fclose(log_fp);
    return 0;
}
