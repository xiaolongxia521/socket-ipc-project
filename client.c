#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    
    // 创建socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket创建失败\n");
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    
    // 转换IP地址
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("无效地址/地址不支持\n");
        return -1;
    }
    
    // 连接服务器
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("连接失败\n");
        return -1;
    }
    
    printf("已连接到服务器！\n");
    printf("输入消息（输入'exit'退出）:\n");
    
    // 从console输入数据
    while (1) {
        printf("> ");
        fgets(buffer, BUFFER_SIZE, stdin);
        
        // 去除换行符
        buffer[strcspn(buffer, "\n")] = 0;
        
        // 检查是否退出
        if (strcmp(buffer, "exit") == 0) {
            printf("退出程序\n");
            break;
        }
        
        // 发送消息到服务器
        send(sock, buffer, strlen(buffer), 0);
        
        // 接收服务器响应
        int valread = read(sock, buffer, BUFFER_SIZE);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("服务器响应: %s\n", buffer);
        }
    }
    
    close(sock);
    return 0;
}
