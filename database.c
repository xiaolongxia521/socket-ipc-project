#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* 全局数据库连接 */
static sqlite3 *db = NULL;

/* 线程安全互斥锁 */
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 初始化数据库 */
int database_init(const char *db_name) {
    int rc;
    char *err_msg = NULL;

    /* 打开数据库连接 */
    rc = sqlite3_open(db_name, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "无法打开数据库: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        db = NULL;
        return -1;
    }

    /* 设置容错超时（防止锁等待） */
    sqlite3_busy_timeout(db, 5000);

    /* 创建消息表（包含服务器回复字段） */
    const char *create_messages_sql =
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_ip TEXT NOT NULL,"
        "client_port INTEGER NOT NULL,"
        "client_msg TEXT NOT NULL,"
        "server_response TEXT,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    rc = sqlite3_exec(db, create_messages_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建消息表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        db = NULL;
        return -1;
    }

    /* 创建连接日志表 */
    const char *create_connections_sql =
        "CREATE TABLE IF NOT EXISTS connections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_ip TEXT NOT NULL,"
        "client_port INTEGER NOT NULL,"
        "event_type TEXT NOT NULL,"
        "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    rc = sqlite3_exec(db, create_connections_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "创建连接日志表失败: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        db = NULL;
        return -1;
    }

    /* 创建索引以加速查询 */
    const char *create_index_sql =
        "CREATE INDEX IF NOT EXISTS idx_timestamp ON messages(timestamp);";
    sqlite3_exec(db, create_index_sql, 0, 0, NULL);

    printf("数据库初始化成功: %s\n", db_name);
    return 0;
}

/* 关闭数据库 */
void database_close(void) {
    pthread_mutex_lock(&db_mutex);
    if (db != NULL) {
        sqlite3_close(db);
        db = NULL;
        printf("数据库连接已关闭\n");
    }
    pthread_mutex_unlock(&db_mutex);
}

/* 插入消息记录（包含服务器回复） */
int database_insert(const char *client_ip, int port,
                    const char *client_msg, const char *server_response) {
    int rc;
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || client_ip == NULL || client_msg == NULL) {
        return -1;
    }

    pthread_mutex_lock(&db_mutex);

    /* 准备SQL语句 */
    const char *sql =
        "INSERT INTO messages (client_ip, client_port, client_msg, server_response) "
        "VALUES (?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备SQL失败: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* 绑定参数（用 SQLITE_TRANSIENT 因为指针来自外部/D-Bus 内存） */
    sqlite3_bind_text(stmt, 1, client_ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, port);
    sqlite3_bind_text(stmt, 3, client_msg, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, server_response ? server_response : "", -1, SQLITE_TRANSIENT);

    /* 执行 */
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "插入数据失败: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return 0;
}

/* 查询最近N条消息 */
int database_select(int limit,
    void (*callback)(int id, const char *ip, int port,
                     const char *msg, const char *resp, const char *time)) {
    int rc;
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || limit <= 0 || callback == NULL) {
        return -1;
    }

    pthread_mutex_lock(&db_mutex);

    /* 准备查询SQL */
    const char *sql =
        "SELECT id, client_ip, client_port, client_msg, server_response, timestamp "
        "FROM messages ORDER BY timestamp DESC LIMIT ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备查询SQL失败: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    /* 绑定limit参数 */
    sqlite3_bind_int(stmt, 1, limit);

    /* 遍历结果集 */
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *ip = (const char *)sqlite3_column_text(stmt, 1);
        int port = sqlite3_column_int(stmt, 2);
        const char *msg = (const char *)sqlite3_column_text(stmt, 3);
        const char *resp = (const char *)sqlite3_column_text(stmt, 4);
        const char *time = (const char *)sqlite3_column_text(stmt, 5);

        callback(id, ip, port, msg, resp, time);
    }

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "查询执行失败: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return 0;
}

/* 记录连接/断开事件 */
int database_log_connection(const char *ip, int port, const char *event) {
    int rc;
    sqlite3_stmt *stmt = NULL;

    if (db == NULL || ip == NULL || event == NULL) {
        return -1;
    }

    pthread_mutex_lock(&db_mutex);

    const char *sql =
        "INSERT INTO connections (client_ip, client_port, event_type) VALUES (?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备SQL失败: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, port);
    sqlite3_bind_text(stmt, 3, event, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "插入连接日志失败: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

    return 0;
}
