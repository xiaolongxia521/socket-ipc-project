#include <stdio.h>
#include "database.h"

void print_message(int id, const char *ip, int port, const char *msg, const char *time) {
    printf("[%d] %s | %s:%d | %s\n", id, time, ip, port, msg);
}

int main() {
    printf("测试数据库功能...\n");
    
    // 初始化
    if (database_init("test.db") != 0) {
        printf("数据库初始化失败!\n");
        return 1;
    }
    printf("✓ 数据库初始化成功\n");
    
    // 测试插入
    if (database_insert("127.0.0.1", 12345, "测试消息1") != 0) {
        printf("插入失败!\n");
    } else {
        printf("✓ 消息插入成功\n");
    }
    
    if (database_insert("192.168.1.100", 54321, "测试消息2") != 0) {
        printf("插入失败!\n");
    } else {
        printf("✓ 消息插入成功\n");
    }
    
    // 测试连接日志
    database_log_connection("127.0.0.1", 12345, "connect");
    printf("✓ 连接日志记录成功\n");
    
    // 测试查询
    printf("\n查询最近5条消息:\n");
    database_select(5, print_message);
    
    // 关闭
    database_close();
    printf("\n✓ 数据库测试完成\n");
    
    return 0;
}
