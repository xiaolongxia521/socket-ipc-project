/**
 * D-Bus 客户端库 - Step 3
 *
 * TCP 服务器通过这些函数调用 D-Bus 服务
 * 所有函数内部共用一个 sd_bus 实例（线程安全访问）
 */

#ifndef DBUS_CLIENT_H
#define DBUS_CLIENT_H

#define DBUS_SERVICE  "com.example.IPCDemo"
#define DBUS_OBJ_PATH "/com/example/IPCDemo"
#define DBUS_IFACE    "com.example.IPCDemo"

/**
 * 初始化 D-Bus 客户端连接
 * 调用一次即可
 * 返回 0 成功，-1 失败
 */
int dbus_client_init(void);

/**
 * 通过 D-Bus 插入消息
 * 参数: ip, port, client_msg, server_response
 * 返回: 0 成功，-1 失败
 */
int dbus_insert_message(const char *ip, int port,
                        const char *client_msg, const char *server_resp);

/**
 * 通过 D-Bus 查询消息
 * 参数: limit - 查询条数
 *       结果打印到 stdout，每行: [id] ip:port - msg -> resp (time)
 * 返回: 0 成功，-1 失败
 */
int dbus_select_messages(int limit);

/**
 * 通过 D-Bus 记录连接日志
 * 参数: ip, port, event ("connect" 或 "disconnect")
 * 返回: 0 成功，-1 失败
 */
int dbus_log_connection(const char *ip, int port, const char *event);

/**
 * 关闭 D-Bus 客户端连接
 */
void dbus_client_close(void);

#endif /* DBUS_CLIENT_H */
