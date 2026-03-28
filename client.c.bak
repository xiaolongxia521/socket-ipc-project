#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>

/* ==================== 配置常量 ==================== */
#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 4096
#define TIMEOUT_SEC 30
#define RECONNECT_DELAY 3
#define MAX_RECONNECT_ATTEMPTS 5
#define HEARTBEAT_INTERVAL 5  /* 添加心跳间隔定义 */

/* ==================== 日志函数 ==================== */
void log_message(const char *format, ...) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    printf("[%s] ", time_str);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
}

#define LOG_INFO(...) log_message(__VA_ARGS__)
#define LOG_ERROR(...) log_message(__VA_ARGS__)

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

/* ==================== 连接管理 ==================== */
int connect_to_server(const char *ip, int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    /* 创建socket */
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LOG_ERROR("Socket创建失败: %s", strerror(errno));
        return -1;
    }
    
    /* 设置接收超时 */
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    /* 转换IP地址 */
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        LOG_ERROR("无效地址/地址不支持: %s", ip);
        close(sock);
        return -1;
    }
    
    /* 连接服务器 */
    LOG_INFO("正在连接到服务器 %s:%d...", ip, port);
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("连接失败: %s", strerror(errno));
        close(sock);
        return -1;
    }
    
    LOG_INFO("已成功连接到服务器！");
    return sock;
}

/* ==================== 主函数 ==================== */
int main(int argc, char *argv[]) {
    int sock = -1;
    char buffer[BUFFER_SIZE];
    int reconnect_attempts = 0;
    int should_exit = 0;
    time_t last_heartbeat_send = 0;
    int heartbeat_enabled = 1;  /* 心跳功能开关 */
    
    /* 解析命令行参数 */
    const char *server_ip = SERVER_IP;
    int server_port = PORT;
    
    if (argc > 1) {
        server_ip = argv[1];
        if (argc > 2) {
            server_port = atoi(argv[2]);
        }
    }
    
    LOG_INFO("客户端启动，目标服务器: %s:%d", server_ip, server_port);
    LOG_INFO("使用提示:");
    LOG_INFO("  1. 输入消息与服务器通信");
    LOG_INFO("  2. 输入 'quit' 或 'exit' 退出");
    LOG_INFO("  3. 输入 'heartbeat on/off' 开启/关闭心跳");
    LOG_INFO("  4. 输入 'ping' 发送PING测试");
    LOG_INFO("  5. 网络异常时会自动重连");
    
    /* 主循环 */
    while (!should_exit) {
        /* 连接服务器 */
        sock = connect_to_server(server_ip, server_port);
        if (sock < 0) {
            reconnect_attempts++;
            if (reconnect_attempts >= MAX_RECONNECT_ATTEMPTS) {
                LOG_ERROR("重连尝试次数过多，客户端退出");
                break;
            }
            
            LOG_INFO("%d秒后尝试重连... (尝试 %d/%d)", 
                    RECONNECT_DELAY, reconnect_attempts, MAX_RECONNECT_ATTEMPTS);
            sleep(RECONNECT_DELAY);
            continue;
        }
        
        /* 连接成功，重置重连计数器 */
        reconnect_attempts = 0;
        last_heartbeat_send = time(NULL);
        
        /* 通信循环 */
        while (!should_exit) {
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);
            FD_SET(STDIN_FILENO, &readfds);
            
            struct timeval tv;
            tv.tv_sec = 1;  /* 1秒轮询 */
            tv.tv_usec = 0;
            
            int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;
            int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
            
            if (activity < 0) {
                if (errno == EINTR) continue;
                LOG_ERROR("select错误: %s", strerror(errno));
                break;
            }
            
            /* 检查服务器数据 */
            if (FD_ISSET(sock, &readfds)) {
                ssize_t bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
                
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    
                    /* 处理PONG响应 */
                    if (strcmp(buffer, "PONG") == 0) {
                        LOG_INFO("收到服务器PONG响应");
                        continue;
                    }
                    
                    /* 处理心跳包 */
                    if (strcmp(buffer, "PING") == 0) {
                        safe_send(sock, "PONG", 4, 0);
                        LOG_INFO("收到服务器PING，回复PONG");
                        continue;
                    }
                    
                    LOG_INFO("服务器: %s", buffer);
                    
                } else if (bytes_received == 0) {
                    /* 服务器关闭连接 */
                    LOG_INFO("服务器断开连接");
                    break;
                    
                } else {
                    /* 接收错误 */
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        /* 超时，继续检查 */
                        continue;
                    }
                    LOG_ERROR("接收错误: %s", strerror(errno));
                    break;
                }
            }
            
            /* 检查用户输入 */
            if (FD_ISSET(STDIN_FILENO, &readfds)) {
                printf("> ");
                if (!fgets(buffer, BUFFER_SIZE, stdin)) {
                    if (feof(stdin)) {
                        LOG_INFO("检测到EOF，退出程序");
                        should_exit = 1;
                        break;
                    }
                    continue;
                }
                
                /* 去除换行符 */
                buffer[strcspn(buffer, "\n")] = 0;
                
                /* 处理退出命令 */
                if (strcmp(buffer, "quit") == 0 || strcmp(buffer, "exit") == 0) {
                    LOG_INFO("正在断开连接...");
                    should_exit = 1;
                    
                    /* 通知服务器客户端要退出 */
                    if (safe_send(sock, "quit", 4, 0) < 0) {
                        LOG_ERROR("发送退出通知失败");
                    }
                    break;
                }
                
                /* 处理心跳开关 */
                if (strncmp(buffer, "heartbeat ", 10) == 0) {
                    char *cmd = buffer + 10;
                    if (strcmp(cmd, "on") == 0) {
                        heartbeat_enabled = 1;
                        LOG_INFO("心跳功能已开启");
                    } else if (strcmp(cmd, "off") == 0) {
                        heartbeat_enabled = 0;
                        LOG_INFO("心跳功能已关闭");
                    } else {
                        LOG_INFO("用法: heartbeat on/off");
                    }
                    continue;
                }
                
                /* 处理PING测试 */
                if (strcmp(buffer, "ping") == 0) {
                    if (safe_send(sock, "PING", 4, 0) < 0) {
                        LOG_ERROR("发送PING失败");
                        break;
                    }
                    LOG_INFO("已发送PING测试");
                    continue;
                }
                
                /* 发送消息到服务器 */
                if (safe_send(sock, buffer, strlen(buffer), 0) < 0) {
                    LOG_ERROR("发送消息失败");
                    break;
                }
                
                LOG_INFO("已发送: %s", buffer);
            }
            
            /* 发送心跳 */
            if (heartbeat_enabled) {
                time_t now = time(NULL);
                if (now - last_heartbeat_send > HEARTBEAT_INTERVAL) {
                    if (send_heartbeat(sock) < 0) {
                        LOG_ERROR("发送心跳失败");
                        break;
                    }
                    last_heartbeat_send = now;
                    LOG_INFO("发送心跳PING");
                }
            }
        }
        
        /* 关闭当前连接 */
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
        
        /* 检查是否应该退出 */
        if (should_exit) {
            LOG_INFO("客户端退出");
            break;
        }
        
        /* 重连前等待 */
        LOG_INFO("连接断开，准备重连...");
        sleep(RECONNECT_DELAY);
    }
    
    /* 最终清理 */
    if (sock >= 0) {
        close(sock);
    }
    
    return 0;
}