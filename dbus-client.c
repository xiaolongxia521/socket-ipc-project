/**
 * D-Bus 客户端库 - Step 3
 *
 * 使用同步 sd_bus_call 替代 sd_bus_call_async + semaphore 等待，
 * 避免主线程事件循环依赖问题。
 */

#include "dbus-client.h"
#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <pthread.h>

static sd_bus *bus = NULL;
static pthread_mutex_t bus_mutex = PTHREAD_MUTEX_INITIALIZER;
static int initialized = 0;

/* ==================== 数据库查询回调 ==================== */
static void dbus_select_callback(int id, const char *ip, int port,
                                  const char *msg, const char *resp,
                                  const char *time) {
    (void)id;
    printf("[%s] %s:%d -> %s\n    回复: %s\n", time, ip, port, msg, resp);
}

/* ==================== 初始化 ==================== */
int dbus_client_init(void) {
    int r;
    pthread_mutex_lock(&bus_mutex);
    if (initialized) {
        pthread_mutex_unlock(&bus_mutex);
        return 0;
    }
    r = sd_bus_default_user(&bus);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Client] 连接 session bus 失败: %s\n", strerror(-r));
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }
    initialized = 1;
    pthread_mutex_unlock(&bus_mutex);
    return 0;
}

/* ==================== 插入消息（同步调用） ==================== */
int dbus_insert_message(const char *ip, int port,
                       const char *client_msg, const char *server_resp) {
    int r;
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;

    if (!initialized) {
        fprintf(stderr, "[D-Bus Client] 未初始化\n");
        return -1;
    }

    pthread_mutex_lock(&bus_mutex);

    r = sd_bus_message_new_method_call(bus, &m,
                                       DBUS_SERVICE,
                                       DBUS_OBJ_PATH,
                                       DBUS_IFACE,
                                       "InsertMessage");
    if (r < 0) {
        fprintf(stderr, "[D-Bus Client] 构建消息失败: %s\n", strerror(-r));
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }

    r = sd_bus_message_append(m, "sisi", ip, port, client_msg, server_resp);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Client] 追加参数失败: %s\n", strerror(-r));
        sd_bus_message_unref(m);
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }

    /* 同步调用：1秒超时，replies 会在调用内处理 */
    r = sd_bus_call(bus, m, 1000000, NULL, &reply);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Client] D-Bus InsertMessage 调用失败: %s\n", strerror(-r));
        sd_bus_message_unref(m);
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }

    sd_bus_message_unref(m);
    if (reply) sd_bus_message_unref(reply);
    pthread_mutex_unlock(&bus_mutex);
    return 0;
}

/* ==================== 查询消息 ==================== */
int dbus_select_messages(int limit) {
    if (!initialized) return -1;
    return database_select(limit, dbus_select_callback);
}

/* ==================== 记录连接日志（同步调用） ==================== */
int dbus_log_connection(const char *ip, int port, const char *event) {
    int r;
    sd_bus_message *m = NULL;
    sd_bus_message *reply = NULL;

    if (!initialized) {
        fprintf(stderr, "[D-Bus Client] 未初始化\n");
        return -1;
    }

    pthread_mutex_lock(&bus_mutex);

    r = sd_bus_message_new_method_call(bus, &m,
                                       DBUS_SERVICE,
                                       DBUS_OBJ_PATH,
                                       DBUS_IFACE,
                                       "LogConnection");
    if (r < 0) {
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }

    r = sd_bus_message_append(m, "sis", ip, port, event);
    if (r < 0) {
        sd_bus_message_unref(m);
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }

    r = sd_bus_call(bus, m, 1000000, NULL, &reply);
    if (r < 0) {
        fprintf(stderr, "[D-Bus Client] D-Bus LogConnection 调用失败: %s\n", strerror(-r));
        sd_bus_message_unref(m);
        pthread_mutex_unlock(&bus_mutex);
        return -1;
    }

    sd_bus_message_unref(m);
    if (reply) sd_bus_message_unref(reply);
    pthread_mutex_unlock(&bus_mutex);
    return 0;
}

/* ==================== 关闭 ==================== */
void dbus_client_close(void) {
    pthread_mutex_lock(&bus_mutex);
    if (bus) {
        sd_bus_unref(bus);
        bus = NULL;
    }
    initialized = 0;
    pthread_mutex_unlock(&bus_mutex);
}
