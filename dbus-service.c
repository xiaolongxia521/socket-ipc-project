/**
 * D-Bus 服务进程 - Step 3（稳定版）
 *
 * 总线名称: com.example.IPCDemo
 * 对象路径: /com/example/IPCDemo
 * 接口:     com.example.IPCDemo
 *
 * 方法:
 *   InsertMessage(s,i,s,i) -> i
 *   SelectMessages(i)      -> a(sisis)
 *   LogConnection(s,i,s)  -> ""
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-bus-vtable.h>
#include <pthread.h>
#include "database.h"

#define BUS_NAME    "com.example.IPCDemo"
#define OBJ_PATH    "/com/example/IPCDemo"
#define IFACE_NAME  "com.example.IPCDemo"

static sd_bus *bus = NULL;
static pthread_mutex_t bus_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 确认指针非空的辅助宏 */
#define ENSURE_NONNULL(ptr) do { if (!(ptr) || *(ptr) == '\0') { \
    fprintf(stderr, "[D-Bus] NULL/empty argument in %s\n", __func__); \
    return sd_bus_error_set_errno(ret_error, EINVAL); } } while(0)

/* ==================== D-Bus 方法实现 ==================== */

static int method_insert_message(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *ip = NULL, *msg = NULL, *resp = NULL;
    int port = 0;
    int r;

    (void)userdata;

    /* 解析参数，明确检查返回值 */
    r = sd_bus_message_read(m, "sisi", &ip, &port, &msg, &resp);
    if (r < 0) {
        fprintf(stderr, "[D-Bus] InsertMessage: sd_bus_message_read 失败: %s\n", strerror(-r));
        return r;
    }

    /* 防御性检查：确认所有指针有效 */
    if (!ip || !msg) {
        fprintf(stderr, "[D-Bus] InsertMessage: NULL parameter ip=%p msg=%p\n", ip, msg);
        sd_bus_error_set_errno(ret_error, EINVAL);
        return -EINVAL;
    }
    if (!resp) resp = "";

    pthread_mutex_lock(&bus_mutex);
    r = database_insert(ip, port, msg, resp);
    pthread_mutex_unlock(&bus_mutex);

    if (r < 0) {
        fprintf(stderr, "[D-Bus] InsertMessage: database_insert 失败\n");
        sd_bus_error_set_errno(ret_error, EIO);
        return -EIO;
    }

    return sd_bus_reply_method_return(m, "i", 0);
}

static int method_select_messages(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    int limit = 0;
    int r;

    (void)userdata;

    r = sd_bus_message_read(m, "i", &limit);
    if (r < 0) {
        fprintf(stderr, "[D-Bus] SelectMessages: 读取参数失败: %s\n", strerror(-r));
        return r;
    }
    if (limit <= 0) limit = 10;

    sd_bus_message *reply = NULL;
    r = sd_bus_message_new_method_return(m, &reply);
    if (r < 0) {
        fprintf(stderr, "[D-Bus] SelectMessages: 创建回复消息失败: %s\n", strerror(-r));
        return r;
    }

    r = sd_bus_message_open_container(reply, 'a', "(sisis)");
    if (r < 0) {
        fprintf(stderr, "[D-Bus] SelectMessages: 打开容器失败: %s\n", strerror(-r));
        sd_bus_message_unref(reply);
        return r;
    }

    /* 直接在 dbus-service 进程内查询数据库（避免跨进程共享句柄） */
    sqlite3 *db_local = NULL;
    sqlite3_stmt *stmt = NULL;

    pthread_mutex_lock(&bus_mutex);
    r = sqlite3_open("server.db", &db_local);
    if (r != SQLITE_OK) {
        fprintf(stderr, "[D-Bus] SelectMessages: 无法打开数据库: %s\n", sqlite3_errmsg(db_local));
        if (db_local) sqlite3_close(db_local);
        pthread_mutex_unlock(&bus_mutex);
        sd_bus_message_unref(reply);
        return -EIO;
    }

    const char *sql = "SELECT id, client_ip, client_port, client_msg, server_response, timestamp "
                     "FROM messages ORDER BY timestamp DESC LIMIT ?;";
    r = sqlite3_prepare_v2(db_local, sql, -1, &stmt, NULL);
    if (r != SQLITE_OK) {
        fprintf(stderr, "[D-Bus] SelectMessages: SQL 准备失败: %s\n", sqlite3_errmsg(db_local));
        sqlite3_close(db_local);
        pthread_mutex_unlock(&bus_mutex);
        sd_bus_message_unref(reply);
        return -EIO;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *col_ip   = (const char*)sqlite3_column_text(stmt, 1);
        const char *col_msg  = (const char*)sqlite3_column_text(stmt, 3);
        const char *col_resp = (const char*)sqlite3_column_text(stmt, 4);
        const char *col_time = (const char*)sqlite3_column_text(stmt, 5);

        r = sd_bus_message_append(reply, "(sisis)",
                                  col_ip   ? col_ip   : "",
                                  sqlite3_column_int(stmt, 2),
                                  col_msg  ? col_msg  : "",
                                  col_resp ? col_resp : "",
                                  col_time ? col_time : "");
        if (r < 0) {
            fprintf(stderr, "[D-Bus] SelectMessages: 追加行失败\n");
            break;
        }
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db_local);
    pthread_mutex_unlock(&bus_mutex);

    sd_bus_message_close_container(reply);

    r = sd_bus_send(bus, reply, NULL);
    sd_bus_message_unref(reply);
    return r < 0 ? r : 0;
}

static int method_log_connection(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *ip = NULL, *event = NULL;
    int port = 0;
    int r;

    (void)userdata;

    r = sd_bus_message_read(m, "sis", &ip, &port, &event);
    if (r < 0) {
        fprintf(stderr, "[D-Bus] LogConnection: 读取参数失败: %s\n", strerror(-r));
        return r;
    }

    if (!ip || !event) {
        sd_bus_error_set_errno(ret_error, EINVAL);
        return -EINVAL;
    }

    pthread_mutex_lock(&bus_mutex);
    r = database_log_connection(ip, port, event);
    pthread_mutex_unlock(&bus_mutex);

    if (r < 0) {
        sd_bus_error_set_errno(ret_error, EIO);
        return -EIO;
    }

    return sd_bus_reply_method_return(m, "");
}

/* ==================== vtable 定义 ==================== */

static const sd_bus_vtable service_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("InsertMessage",  "sisi", "i",        method_insert_message,  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SelectMessages", "i",    "a(sisis)", method_select_messages, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("LogConnection",  "sis",  "",          method_log_connection,  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END
};

/* ==================== 主函数 ==================== */

int main(void) {
    int r;

    printf("[D-Bus Service] 启动中...\n");

    if (database_init("server.db") != 0) {
        fprintf(stderr, "[D-Bus Service] 数据库初始化失败\n");
        return 1;
    }
    printf("[D-Bus Service] 数据库就绪\n");

    r = sd_bus_default(&bus);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Service] 连接 session bus 失败: %s\n", strerror(-r));
        return 1;
    }
    printf("[D-Bus Service] 已连接 session bus\n");

    r = sd_bus_add_object_vtable(bus, NULL, OBJ_PATH, IFACE_NAME, service_vtable, NULL);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Service] 注册对象失败: %s\n", strerror(-r));
        return 1;
    }
    printf("[D-Bus Service] 对象已注册: %s\n", OBJ_PATH);

    r = sd_bus_request_name(bus, BUS_NAME, 0);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Service] 申请总线名称失败: %s\n", strerror(-r));
        return 1;
    }
    if (r == 0) {
        fprintf(stderr, "[D-Bus Service] 总线名称 %s 已被占用\n", BUS_NAME);
        return 1;
    }
    printf("[D-Bus Service] 总线名称: %s\n", BUS_NAME);
    printf("[D-Bus Service] 等待 D-Bus 方法调用...\n");

    for (;;) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) {
            fprintf(stderr, "[D-Bus Service] 处理总线事件失败: %s\n", strerror(-r));
            break;
        }
        if (r > 0) continue;
        r = sd_bus_wait(bus, (uint64_t)-1);
        if (r < 0) {
            fprintf(stderr, "[D-Bus Service] 等待失败: %s\n", strerror(-r));
            break;
        }
    }

    database_close();
    sd_bus_unref(bus);
    return 0;
}
