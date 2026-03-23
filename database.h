#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

/* 初始化数据库，打开连接，创建表 */
int database_init(const char *db_name);

/* 关闭数据库连接 */
void database_close(void);

/* 插入消息记录
 * 参数: 客户端IP, 端口, 消息内容
 * 返回: 0成功，-1失败
 */
int database_insert(const char *client_ip, int port, const char *message);

/* 查询最近N条消息
 * 参数: 查询条数, 回调函数(处理每一行结果)
 * 返回: 0成功，-1失败
 */
int database_select(int limit, 
    void (*callback)(int id, const char *ip, int port, const char *msg, const char *time));

/* 记录连接/断开事件
 * 参数: IP, 端口, 事件类型("connect"或"disconnect")
 * 返回: 0成功，-1失败
 */
int database_log_connection(const char *ip, int port, const char *event);

#endif
