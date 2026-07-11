# 开发指南

## 架构概览

```
┌──────────────────────────────────────────────────┐
│  浏览器 (index.html + app.js + styles.css)        │
│  登录 → 获取 Token → 调用 API → 渲染表格           │
└─────────────────┬────────────────────────────────┘
                  │ HTTP (localhost:8081)
┌─────────────────▼────────────────────────────────┐
│  main.cpp (C++17, 单文件 ~750 行)                  │
│                                                    │
│  handleClient() ─── parseRequest()                 │
│       │                    │                       │
│       ▼                    ▼                       │
│  handleRequest()  ──── 路由分发 ────┐               │
│       │              │       │       │              │
│       ▼              ▼       ▼       ▼              │
│  serveStatic   handleLogin  handleCreate   ...     │
│  (public/*)               handleList               │
│                           handleTake               │
│       │                    │                       │
│       ▼                    ▼                       │
│  ┌─────────────────────────────┐                   │
│  │  g_db (mysql_real_connect)  │                   │
│  │  g_sessions (内存 Token 表) │                   │
│  └─────────────┬───────────────┘                   │
│                │                                    │
└────────────────┼────────────────────────────────────┘
                 │
┌────────────────▼────────────────────────────────────┐
│  MySQL 8.0 (localhost:3306 / first_test)             │
│  ┌──────────────┐  ┌───────────────────────────────┐│
│  │ users        │  │ goods                         ││
│  │  id          │  │  id (AUTO_INCREMENT)           ││
│  │  username    │  │  name                          ││
│  │  password    │  │  location                      ││
│  │  role        │  │  status (stored / taken)       ││
│  └──────────────┘  │  stored_at (DATETIME)           ││
│                    │  taken_at (DATETIME, nullable)  ││
│                    │  operator                      ││
│                    └───────────────────────────────┘│
└─────────────────────────────────────────────────────┘
```

## 技术决策

### 为什么单文件

所有后端代码集中在 `src/main.cpp`（~750 行），未拆分为头文件/多翻译单元。原因：

- 项目规模小（4 个 API 端点），拆分会增加认知负担
- 匿名命名空间提供编译单元级封装，无需 `static` / 内部链接
- 部署简单：单源文件，编译产物即单可执行文件

### 为什么原生 Socket 而非 HTTP 库

直接使用 POSIX/Winsock API，未引入任何第三方 HTTP 库。原因：

- 零外部依赖（除 MySQL C client），编译快，部署简单
- 需求简单（仅 JSON API + 静态文件），不需要全功能 HTTP 框架
- 仅实现了必要的 HTTP/1.1 子集（解析请求行+头部、Content-Length 分块、CORS 头）

### 为什么 MySQL 而非文件存储

早期版本使用 TSV 文件存储，后迁移至 MySQL。原因：

- 并发安全：`g_dbMutex` 保护所有 DB 操作，避免文件锁问题
- 查询能力：SQL `LIKE` 模糊搜索、`ORDER BY` 排序天然支持
- 自动清理：`DELETE ... WHERE ... < NOW() - INTERVAL 30 DAY` 更可靠

### 为什么 Token 存内存

用户会话保存在 `g_sessions`（`std::map<std::string, User>`），未持久化。原因：

- 服务重启后自动清空过期会话，无需实现 Token 过期逻辑
- 单进程部署，无需 Redis 等外部会话存储
- 项目规模不需要分布式会话

## 数据库设计

### 初始化

首次启动时 `initDb()` 自动执行：

1. `CREATE TABLE IF NOT EXISTS users (...)` — 用户表
2. `CREATE TABLE IF NOT EXISTS goods (...)` — 货物表
3. `INSERT IGNORE INTO users ...` — 种子数据（3 个默认用户）

### 表结构

**users**

| 列 | 类型 | 约束 |
|----|------|------|
| `id` | `INT AUTO_INCREMENT` | PRIMARY KEY |
| `username` | `VARCHAR(64)` | NOT NULL, UNIQUE |
| `password` | `VARCHAR(128)` | NOT NULL |
| `role` | `VARCHAR(32)` | NOT NULL, DEFAULT 'viewer' |

**goods**

| 列 | 类型 | 约束 |
|----|------|------|
| `id` | `INT AUTO_INCREMENT` | PRIMARY KEY |
| `name` | `VARCHAR(256)` | NOT NULL |
| `location` | `VARCHAR(256)` | NOT NULL, DEFAULT '默认货架' |
| `status` | `VARCHAR(32)` | NOT NULL, DEFAULT 'stored' |
| `stored_at` | `DATETIME` | NOT NULL, DEFAULT CURRENT_TIMESTAMP |
| `taken_at` | `DATETIME` | NULL |
| `operator` | `VARCHAR(64)` | NOT NULL |

## 构建配置

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(warehouse_storage_system LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

set(MYSQL_DIR "D:/Program Files/MySQL/MySQL Server 8.0")  ## 按需修改

add_executable(warehouse_server src/main.cpp)

target_include_directories(warehouse_server PRIVATE "${MYSQL_DIR}/include")
target_link_libraries(warehouse_server PRIVATE
    Threads::Threads
    "${MYSQL_DIR}/lib/libmysql.lib"
)

if (WIN32)
    target_link_libraries(warehouse_server PRIVATE ws2_32)
endif()
```

> `MYSQL_DIR` 需根据本机 MySQL 安装路径修改。Linux 下通常为 `/usr`，链接 `libmysqlclient` 而非 `libmysql.lib`。

### Windows 运行依赖

编译产物 `warehouse_server.exe` 运行时需要以下 DLL 在路径中：

- `libmysql.dll` — MySQL C client
- `libssl-3-x64.dll`、`libcrypto-3-x64.dll` — OpenSSL（MySQL 依赖）
- `libwinpthread-1.dll`、`libstdc++-6.dll`、`libgcc_s_seh-1.dll` — MinGW 运行时（仅 MinGW 构建）

`cmake-build-debug/` 目录下已包含这些 DLL，在该目录运行 exe 即可。

## CI/CD

GitHub Actions 在 push/PR 到 `master` 分支时自动构建矩阵：

| 平台 | 编译器 |
|------|--------|
| `windows-latest` | MSVC (`cl`) |
| `ubuntu-latest` | GCC (`g++`) |
| `ubuntu-latest` | Clang (`clang++`) |

仅在 CI 中执行编译验证，无自动化测试步骤。

## 开发环境

### 推荐 IDE

项目包含 `.idea/` 目录，开箱即用 **CLion**。`CMakeSettings.json` 提供 **Visual Studio** 的 CMake 集成配置。

### 编码约定

- C++17，无异常（未使用 `try-catch`，通过返回值处理错误）
- 所有函数和数据放在匿名命名空间内（内部链接）
- 全局变量前缀 `g_`（`g_db`、`g_dbMutex`、`g_sessions`、`g_tokenSeed`）
- 常量 `constexpr` + `kPascalCase`（`kPort`、`kVacancyRetentionDays`）
- JSON 手动拼接（无 JSON 库依赖）
- SQL 字符串拼接（使用 `mysql_real_escape_string` 防注入）

### 已知限制

- 无 HTTPS 支持
- Token 无过期时间（仅服务重启时清空）
- 单线程接收连接，每个连接 `detach` 新线程；高并发场景下线程数不可控
- `Content-Length` 解析依赖正则，非严格解析
- 密码明文存储（未哈希）
