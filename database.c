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

/* 内部辅助函数：获取当前时间字符串 */
static void get_current_time(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

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
    
    /* 创建消息表 */
    const char *create_messages_sql = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "client_ip TEXT NOT NULL,"
        "client_port INTEGER NOT NULL,"
        "message TEXT NOT NULL,"
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

/* 插入消息记录 */
int database_insert(const char *client_ip, int port, const char *message) {
    int rc;
    sqlite3_stmt *stmt = NULL;
    
    if (db == NULL || client_ip == NULL || message == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&db_mutex);
    
    /* 准备SQL语句 */
    const char *sql = "INSERT INTO messages (client_ip, client_port, message) VALUES (?, ?, ?);";
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备SQL失败: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    
    /* 绑定参数 */
    sqlite3_bind_text(stmt, 1, client_ip, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, port);
    sqlite3_bind_text(stmt, 3, message, -1, SQLITE_STATIC);
    
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
    void (*callback)(int id, const char *ip, int port, const char *msg, const char *time)) {
    int rc;
    sqlite3_stmt *stmt = NULL;
    
    if (db == NULL || limit <= 0 || callback == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&db_mutex);
    
    /* 准备查询SQL */
    const char *sql = "SELECT id, client_ip, client_port, message, timestamp FROM messages "
                      "ORDER BY timestamp DESC LIMIT ?;";
    
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
        const char *time = (const char *)sqlite3_column_text(stmt, 4);
        
        callback(id, ip, port, msg, time);
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
    
    const char *sql = "INSERT INTO connections (client_ip, client_port, event_type) VALUES (?, ?, ?);";
    
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "准备SQL失败: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return -1;
    }
    
    sqlite3_bind_text(stmt, 1, ip, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, port);
    sqlite3_bind_text(stmt, 3, event, -1, SQLITE_STATIC);
    
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
