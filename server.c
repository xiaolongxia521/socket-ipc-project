#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024

// 客户端处理线程函数
void* handle_client(void* arg) {
    int client_socket = *(int*)arg;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    
    printf("新客户端连接，线程ID: %lu\n", pthread_self());
    
    while ((bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("收到客户端消息: %s\n", buffer);
        
        // 发送确认消息
        char response[BUFFER_SIZE];
        snprintf(response, BUFFER_SIZE, "服务器收到: %s", buffer);
        send(client_socket, response, strlen(response), 0);
    }
    
    printf("客户端断开连接\n");
    close(client_socket);
    free(arg);
    pthread_exit(NULL);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    // 创建socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket创建失败");
        exit(EXIT_FAILURE);
    }
    
    // 设置socket选项
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt失败");
        exit(EXIT_FAILURE);
    }
    
    // 绑定地址和端口
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("绑定失败");
        exit(EXIT_FAILURE);
    }
    
    // 监听连接
    if (listen(server_fd, 3) < 0) {
        perror("监听失败");
        exit(EXIT_FAILURE);
    }
    
    printf("服务器启动，监听端口 %d...\n", PORT);
    
    // 接受客户端连接
    while (1) {
        int *new_client = malloc(sizeof(int));
        if ((*new_client = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("接受连接失败");
            free(new_client);
            continue;
        }
        
        printf("新连接来自: %s:%d\n", 
               inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        
        // 创建新线程处理客户端
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_client) != 0) {
            perror("线程创建失败");
            close(*new_client);
            free(new_client);
            continue;
        }
        
        // 分离线程，使其结束后自动释放资源
        pthread_detach(thread_id);
    }
    
    close(server_fd);
    return 0;
}
