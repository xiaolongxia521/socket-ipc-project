# Socket IPC Project

一个基于Socket的多线程IPC通信项目，包含服务端和客户端程序。

## 项目结构

```
socket-ipc-project/
├── server.c          # 服务端程序
├── client.c          # 客户端程序
├── Makefile          # 编译脚本
├── server            # 编译后的服务端可执行文件
├── client            # 编译后的客户端可执行文件
└── README.md         # 项目说明文档
```

## 功能特性

- **多线程服务端**：支持同时处理多个客户端连接
- **简单客户端**：提供命令行界面发送消息到服务端
- **双向通信**：客户端发送消息，服务端回复确认
- **线程安全**：使用pthread库实现线程管理

## 编译与运行

### 编译项目
```bash
make all
```

### 清理编译文件
```bash
make clean
```

### 运行服务端
```bash
./server
```

### 运行客户端
```bash
./client
```

## 使用说明

1. 首先启动服务端程序：
   ```bash
   ./server
   ```
   服务端将在8080端口监听连接。

2. 然后启动一个或多个客户端程序：
   ```bash
   ./client
   ```

3. 在客户端中输入消息，服务端会回复确认。

4. 输入 "exit" 退出客户端。

## 技术细节

### 服务端 (server.c)
- 使用 `socket()` 创建TCP socket
- 使用 `bind()` 绑定到8080端口
- 使用 `listen()` 监听连接
- 使用 `accept()` 接受客户端连接
- 为每个客户端创建独立的线程处理
- 使用 `pthread_create()` 和 `pthread_detach()` 管理线程

### 客户端 (client.c)
- 使用 `socket()` 创建TCP socket
- 使用 `connect()` 连接到服务端
- 使用 `send()` 发送消息
- 使用 `read()` 接收服务端响应
- 提供简单的命令行界面

## 端口配置
默认使用8080端口，可以在代码中修改 `PORT` 常量来更改端口。

## 依赖
- GCC编译器
- POSIX线程库 (pthread)

## 许可证
私有项目