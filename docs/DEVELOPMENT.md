# 开发指南

> **当前状态：遗留实现基线。** 本文件的架构、表结构、依赖和测试章节描述当前仓库事实。已批准但尚未实现的目标见 [系统架构](ARCHITECTURE.md) 和 [实施计划](IMPLEMENTATION_PLAN.md)；不得把目标模块、迁移、测试或安全机制写成已经可用。

## 当前遗留架构

```text
浏览器 public/*
        │ HTTP/JSON
        ▼
src/main.cpp
  ├─ Socket 与 HTTP 请求解析
  ├─ API 路由和静态文件服务
  ├─ 仓储业务与 MySQL 访问
  └─ 内存会话
        │
        ├─ src/password_hash.cpp ── libsodium / Argon2id
        └─ MySQL 8.0 ── users + goods
```

`main.cpp` 仍承载规模较小的 HTTP 与仓储业务；密码哈希被提取为独立模块，因为生产登录、旧数据迁移和单元测试必须共享同一实现。

目标将按实施计划重构为 C++17 模块化单体、MySQL 8、版本化 `/api/v1` 和 Vue 3 + TypeScript 前端。当前阶段未引入 cpp-httplib、nlohmann/json、OpenSSL、Vue、Node 构建链或新数据库表。

## 数据库配置

服务不包含数据库默认凭据。启动前必须设置：

| 变量 | 说明 |
|------|------|
| `WAREHOUSE_DB_HOST` | MySQL 主机 |
| `WAREHOUSE_DB_PORT` | 1–65535 的端口 |
| `WAREHOUSE_DB_NAME` | 数据库名；数据库本身需预先创建 |
| `WAREHOUSE_DB_USER` | 应用专用数据库用户 |
| `WAREHOUSE_DB_PASSWORD` | 数据库口令 |

不要使用 MySQL `root` 运行服务，也不要把这些值写入仓库或 `CMakeSettings.json`。

## 初始化管理员

`users` 表中不存在 `admin` 角色时，服务要求：

| 变量 | 约束 |
|------|------|
| `WAREHOUSE_ADMIN_USERNAME` | 1–64 个字符 |
| `WAREHOUSE_ADMIN_PASSWORD` | 12–128 个字符 |

口令在插入前使用 libsodium `crypto_pwhash_str()` 生成 Argon2id 哈希。已有管理员时不读取这两个变量，也不自动创建 manager/viewer。

## 旧密码迁移

启动时按以下状态处理 `users.password`：

- 合法 Argon2id/Argon2i 字符串：保留；参数过旧时在成功登录后重新哈希。
- 普通字符串：视为旧版明文。
- 以 Argon2 格式开头但无法解析：视为损坏数据，拒绝启动。

检测到明文时，服务默认拒绝启动。完成数据库备份后设置 `WAREHOUSE_MIGRATE_PASSWORDS=1`，服务会：

1. 开启 MySQL 事务。
2. `SELECT ... FOR UPDATE` 锁定用户记录并再次校验。
3. 将所有明文转换为独立加盐的 Argon2id 哈希。
4. 全部成功后提交；任一失败则回滚。

迁移后清除开关。哈希不可还原，退回旧程序必须恢复迁移前备份。决策背景见 [ADR 0001](adr/0001-argon2id-password-migration.md)。

## 表结构

`users`：

| 列 | 类型 | 说明 |
|----|------|------|
| `id` | `INT AUTO_INCREMENT` | 主键 |
| `username` | `VARCHAR(64)` | 唯一用户名 |
| `password` | `VARCHAR(128)` | libsodium 密码验证字符串 |
| `role` | `VARCHAR(32)` | admin / manager / viewer |

`goods`：

| 列 | 类型 | 说明 |
|----|------|------|
| `id` | `INT AUTO_INCREMENT` | 主键和货物编号来源 |
| `name` | `VARCHAR(256)` | 货物名称 |
| `location` | `VARCHAR(256)` | 货位 |
| `status` | `VARCHAR(32)` | stored / taken |
| `stored_at` | `DATETIME` | 入库时间 |
| `taken_at` | `DATETIME NULL` | 取出时间 |
| `operator` | `VARCHAR(64)` | 最后操作人 |

## 构建与依赖

Windows 使用 vcpkg manifest 中的 `libmysql` 和 `libsodium`；CMake 链接 `unofficial::libmysql::libmysql` 与 `unofficial-sodium::sodium`。Linux 使用 pkg-config 查找 `mysqlclient` 和 `libsodium`。

`CMakeSettings.json` 提供四个 Visual Studio 配置：

- `x64-Debug`
- `x64-Release`
- `Linux-Debug`（远程 Linux）
- `Linux-Release`（远程 Linux）

Visual Studio 远程 Linux 主机仍需预装 `libmysqlclient-dev`、`libsodium-dev` 和 `pkg-config`。

## 测试与 CI

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

`password_hash_test` 覆盖：

- 旧明文和损坏 Argon2 字符串分类。
- 同一密码的独立随机盐。
- 正确密码、错误密码验证。
- 当前参数的 rehash 判断。
- 敏感字符串清理。

唯一保留的 GitHub Actions 工作流在 Ubuntu/GCC、Ubuntu/Clang、Windows/MSVC 上构建并执行 CTest。

## 本地文件

CLion、CodeGraph、构建目录、vcpkg 安装树、日志、`.env*` 和代理会话状态均由根 `.gitignore` 排除。`.idea/` 不再由 Git 跟踪，但现有本机配置可以继续使用。

## 已知限制

- 服务仅监听本机 HTTP，没有 TLS。
- Token 可预测、没有主动过期时间，仅在服务重启时清空。
- CORS 当前允许任意来源。
- 每个连接创建一个分离线程，不适合高并发部署。
- 没有用户管理 API，非初始管理员账户需通过受控运维流程创建。
- SQL 仍由字符串拼接并使用 `mysql_real_escape_string()` 转义，尚未迁移到预处理语句。

## 计划中的迁移与验证语义

目标迁移器将使用预检、单条原子 DDL、事务化 DML 回填、切换和后续清理的可重入步骤。MySQL 多条 DDL 不能被宣称为普通事务整体回滚；失败时必须保存真实步骤状态、拒绝业务启动，并通过修复后的可重入执行或已验证备份恢复。

每个实施阶段的验证报告只允许使用 `PASSED`、`FAILED`、`BLOCKED`、`NOT_RUN` 和 `SKIPPED`。只有实际执行、退出码为零且输出已检查的命令可以标为 `PASSED`；本地通过不能代替 CI，通过模拟器不能代替真实供应商或扫描器验证，文档检查也不能代替构建、数据库或功能测试。
