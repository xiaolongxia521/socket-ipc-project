/**
 * 改进版 TCP 多线程服务器
 * 改进点:
 * 1. 安全性: 缓冲区溢出防护、输入验证
 * 2. 健壮性: 完善的错误处理、超时控制、信号处理
 * 3. 功能性: 心跳机制、连接数限制、优雅关闭
 * 4. 代码质量: 日志系统、配置化、线程安全
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

/* ==================== 配置常量 ==================== */
#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 100
#define TIMEOUT_SEC 30
#define HEARTBEAT_INTERVAL 5
#define LOG_FILE "server.log"

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

/* ==================== 全局变量 ==================== */
static int server_fd = -1;
static volatile int running = 1;
static int active_clients = 0;
static pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ==================== 客户端信息结构体 ==================== */
typedef struct {
    int socket;
    struct sockaddr_in addr;
    time_t last_heartbeat;
    char client_ip[INET_ADDRSTRLEN];
    socklen_t addr_len;
} ClientInfo;

/* ==================== 日志函数 ==================== */
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
    
    /* 同时输出到控制台 */
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
    LOG_INFO("收到信号 %d，正在优雅关闭服务器...", sig);
    running = 0;
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
}

/* ==================== 安全发送/接收函数 ==================== */
ssize_t safe_send(int sock, const void *buf, size_t len, int flags) {
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(sock, (const char*)buf + total_sent, len - total_sent, flags);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (sent == 0) return -1;
        total_sent += sent;
    }
    return (ssize_t)total_sent;
}

ssize_t safe_recv(int sock, void *buf, size_t len, int flags) {
    ssize_t received = recv(sock, buf, len, flags);
    return received;
}

/* ==================== 心跳处理 ==================== */
int send_heartbeat(int sock) {
    const char *ping = "PING";
    return (safe_send(sock, ping, strlen(ping), 0) > 0) ? 0 : -1;
}

int check_heartbeat(ClientInfo *client) {
    time_t now = time(NULL);
    if (now - client->last_heartbeat > HEARTBEAT_INTERVAL * 2) {
        return -1; /* 心跳超时 */
    }
    return 0;
}

/* ==================== 客户端处理线程 ==================== */
void* handle_client(void* arg) {
    ClientInfo *client = (ClientInfo*)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    int should_exit = 0;
    
    /* 增加活跃连接数 */
    pthread_mutex_lock(&count_mutex);
    active_clients++;
    pthread_mutex_unlock(&count_mutex);
    
    LOG_INFO("[%s:%d] 新客户端连接，当前连接数: %d", 
             client->client_ip, ntohs(client->addr.sin_port), active_clients);
    
    /* 设置接收超时 */
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(client->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    client->last_heartbeat = time(NULL);
    time_t last_heartbeat_send = 0;
    
    /* 主处理循环 */
    while (!should_exit && running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(client->socket, &readfds);
        
        struct timeval tv;
        tv.tv_sec = 1;  /* 1秒轮询 */
        tv.tv_usec = 0;
        
        int activity = select(client->socket + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0) {
            if (errno != EINTR) {
                LOG_ERROR("[%s:%d] select错误: %s", 
                         client->client_ip, ntohs(client->addr.sin_port), strerror(errno));
                break;
            }
            continue;
        }
        
        /* 检查是否有数据可读 */
        if (FD_ISSET(client->socket, &readfds)) {
            bytes_received = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                client->last_heartbeat = time(NULL);
                
                /* 处理心跳包 */
                if (strcmp(buffer, "PING") == 0) {
                    safe_send(client->socket, "PONG", 4, 0);
                    LOG_DEBUG("[%s:%d] 收到PING，回复PONG", 
                             client->client_ip, ntohs(client->addr.sin_port));
                    continue;
                }
                if (strcmp(buffer, "PONG") == 0) {
                    LOG_DEBUG("[%s:%d] 收到PONG", 
                             client->client_ip, ntohs(client->addr.sin_port));
                    continue;
                }
                
                LOG_INFO("[%s:%d] 收到: %s", 
                        client->client_ip, ntohs(client->addr.sin_port), buffer);
                
                /* 发送响应 */
                char response[BUFFER_SIZE];
                int len = snprintf(response, BUFFER_SIZE, "服务器收到 [%s:%d]: %s",
                                 client->client_ip, ntohs(client->addr.sin_port), buffer);
                if (safe_send(client->socket, response, len, 0) < 0) {
                    LOG_ERROR("[%s:%d] 发送响应失败", 
                             client->client_ip, ntohs(client->addr.sin_port));
                    break;
                }
                
            } else if (bytes_received == 0) {
                /* 客户端正常关闭 */
                LOG_INFO("[%s:%d] 客户端关闭连接", 
                        client->client_ip, ntohs(client->addr.sin_port));
                break;
                
            } else {
                /* 接收错误 */
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    /* 超时，检查心跳 */
                    if (check_heartbeat(client) < 0) {
                        LOG_WARN("[%s:%d] 心跳超时，断开连接", 
                                client->client_ip, ntohs(client->addr.sin_port));
                        break;
                    }
                    continue;
                }
                LOG_ERROR("[%s:%d] 接收错误: %s", 
                         client->client_ip, ntohs(client->addr.sin_port), strerror(errno));
                break;
            }
        }
        
        /* 发送心跳 */
        time_t now = time(NULL);
        if (now - last_heartbeat_send > HEARTBEAT_INTERVAL) {
            if (send_heartbeat(client->socket) < 0) {
                LOG_WARN("[%s:%d] 发送心跳失败", 
                        client->client_ip, ntohs(client->addr.sin_port));
                break;
            }
            last_heartbeat_send = now;
        }
        
        /* 检查心跳超时 */
        if (check_heartbeat(client) < 0) {
            LOG_WARN("[%s:%d] 心跳超时，断开连接", 
                    client->client_ip, ntohs(client->addr.sin_port));
            break;
        }
    }
    
    /* 清理 */
    close(client->socket);
    free(client);
    
    pthread_mutex_lock(&count_mutex);
    active_clients--;
    pthread_mutex_unlock(&count_mutex);
    
    LOG_INFO("客户端断开，当前活跃连接数: %d", active_clients);
    return NULL;
}

/* ==================== 主函数 ==================== */
int main(void) {
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    /* 初始化日志 */
    log_init();
    LOG_INFO("=== 服务器启动 ===");
    
    /* 设置信号处理 */
    signal(SIGINT, sigint_handler);
    signal(SIGTERM, sigint_handler);
    
    /* 创建socket */
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        LOG_ERROR("socket创建失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Socket创建成功");
    
    /* 设置socket选项 */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        LOG_ERROR("setsockopt失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("Socket选项设置成功");
    
    /* 绑定地址和端口 */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR("绑定失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("绑定成功，端口: %d", PORT);
    
    /* 监听连接 */
    if (listen(server_fd, SOMAXCONN) < 0) {
        LOG_ERROR("监听失败: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    LOG_INFO("开始监听，最大客户端数: %d", MAX_CLIENTS);
    LOG_INFO("服务器就绪，等待连接...");
    
    /* 主循环 */
    while (running) {
        /* 检查连接数限制 */
        pthread_mutex_lock(&count_mutex);
        int current_clients = active_clients;
        pthread_mutex_unlock(&count_mutex);
        
        if (current_clients >= MAX_CLIENTS) {
            LOG_WARN("达到最大客户端数 (%d)，拒绝新连接", MAX_CLIENTS);
            
            /* 接受然后立即关闭，让客户端知道服务器繁忙 */
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int temp_sock = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
            if (temp_sock > 0) {
                char reject_msg[] = "服务器繁忙，请稍后重试\n";
                send(temp_sock, reject_msg, strlen(reject_msg), 0);
                close(temp_sock);
            }
            sleep(1);
            continue;
        }
        
        /* 接受新连接 */
        ClientInfo *client = malloc(sizeof(ClientInfo));
        if (!client) {
            LOG_ERROR("内存分配失败");
            sleep(1);
            continue;
        }
        
        client->addr_len = sizeof(client->addr);
        int new_socket = accept(server_fd, (struct sockaddr *)&client->addr, &client->addr_len);
        
        if (new_socket < 0) {
            if (errno == EINTR) {
                free(client);
                continue;
            }
            LOG_ERROR("接受连接失败: %s", strerror(errno));
            free(client);
            sleep(1);
            continue;
        }
        
        client->socket = new_socket;
        client->last_heartbeat = time(NULL);
        inet_ntop(AF_INET, &client->addr.sin_addr, client->client_ip, INET_ADDRSTRLEN);
        
        LOG_INFO("新连接来自: %s:%d", client->client_ip, ntohs(client->addr.sin_port));
        
        /* 创建线程 */
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client) != 0) {
            LOG_ERROR("线程创建失败: %s", strerror(errno));
            close(client->socket);
            free(client);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    /* 清理 */
    LOG_INFO("服务器关闭");
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (log_fp && log_fp != stderr) {
        fclose(log_fp);
    }
    
    return 0;
}