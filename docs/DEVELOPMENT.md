# 开发指南

> **当前状态：阶段 1 平台基础已获用户审查批准。** Debug 与 Release 的完整 CTest 各 14 项均已通过，包括专用本地 MySQL 8 集成测试；本次提交发布该阶段成果。本文件只描述仓库中已经落地的模块、构建和测试接缝。六身份、Cookie 会话、TOTP、库存数量与金额、申请审批、附件扫描、`/api/v1` 和 Vue 前端仍是后续阶段目标，见 [系统架构](ARCHITECTURE.md) 与 [实施计划](IMPLEMENTATION_PLAN.md)。

## 当前架构

```text
src/main.cpp
  -> bootstrap（配置、启动门、组合根、停止协调）
       -> api（cpp-httplib、nlohmann/json、静态文件）
            -> application（typed 遗留用例、工作单元端口）
                 -> domain（遗留值对象）
       -> infrastructure/mysql（libmysql RAII、连接池、事务、迁移）
       -> infrastructure/security/session（Argon2id、内存会话）
```

`src/main.cpp` 只调用组合根。原单文件实现保存在未参与构建的 `src/legacy_server_reference.cpp`，仅供阶段迁移核对；旧 API 的实际请求路径已经由模块化实现承载。当前依赖方向由独立 CMake 目标表达，应用层和领域层不包含 HTTP、JSON 或 MySQL 类型。

阶段 1 保留 `/api/login`、`/api/goods`、`/api/goods/take` 的遗留协议，不注册 `/api/v1`。取出记录不再被请求路径物理删除。

## 数据库配置

服务不包含数据库默认凭据。启动前必须设置：

| 变量 | 说明 |
|---|---|
| `WAREHOUSE_DB_HOST` | MySQL 主机 |
| `WAREHOUSE_DB_PORT` | 1–65535 的端口 |
| `WAREHOUSE_DB_NAME` | 数据库名；数据库本身需预先创建 |
| `WAREHOUSE_DB_USER` | 应用专用数据库用户 |
| `WAREHOUSE_DB_PASSWORD` | 数据库口令 |
| `WAREHOUSE_DB_POOL_SIZE` | 可选；有界连接池容量，默认 1 |
| `WAREHOUSE_HTTP_BIND_ADDRESS` | 可选；默认 `127.0.0.1` |
| `WAREHOUSE_HTTP_PORT` | 可选；默认 8081 |
| `WAREHOUSE_HTTP_WORKER_THREADS` | 可选；有界 HTTP 工作线程数，默认 1 |
| `WAREHOUSE_CORS_ALLOWED_ORIGINS` | 可选；逗号分隔的精确来源，禁止 `*` |

不要使用 MySQL `root` 运行服务，也不要把这些值写入仓库或 `CMakeSettings.json`。配置校验一次收集安全错误，不把口令原值写入错误消息。

## 初始化管理员与旧密码

`users` 表中不存在 `admin` 角色时，服务要求同时提供 `WAREHOUSE_ADMIN_USERNAME` 和 `WAREHOUSE_ADMIN_PASSWORD`。用户名为 1–64 个字符，口令为 12–128 个字符；口令写入前通过 libsodium Argon2id 哈希。已有管理员时不会自动创建其他账户。

启动时按以下状态处理 `users.password`：

- 合法 Argon2id/Argon2i 字符串：保留；参数过旧时在成功登录后重新哈希。
- 普通字符串：视为旧版明文，默认拒绝启动。
- 以 Argon2 格式开头但无法解析：视为损坏数据，拒绝启动。

完成数据库备份后，可只为一次启动设置 `WAREHOUSE_MIGRATE_PASSWORDS=1`。迁移使用事务和 `SELECT ... FOR UPDATE`；全部成功才提交，任一失败则回滚对应 DML 事务。成功后应清除此变量。哈希转换不可逆，退回旧程序必须恢复迁移前备份。

## 当前表与迁移元数据

阶段 1 的版本化 SQL 只建立或验证遗留 `users`、`goods` 基线及迁移控制表，不创建阶段 2 以后业务表：

- `schema_migrations`：记录版本名称和 `RUNNING`、`SUCCEEDED`、`FAILED` 状态。
- `schema_migration_steps`：记录步骤、校验和、阶段、执行类型和失败摘要。
- `users`：遗留用户、密码哈希和 `admin`/`manager`/`viewer` 角色。
- `goods`：遗留货物名称、自由文本货位、`stored`/`taken` 状态和操作时间。

控制表 DDL 与结构验证也来自 `migrations/*.bootstrap*.sql` 固定文件，不在 C++ 中拼接建表结构。普通启动不执行 cleanup，但仍核对已经记录的 cleanup 元数据；每个 apply/verify 文件只允许一个服务器语句。步骤校验和覆盖步骤 ID、阶段、执行类型和 apply/verify SQL，数据库已记录步骤与当前版本清单双向对账，删除或篡改均拒绝启动。迁移器取得数据库命名锁，逐步记录事实，并分别报告已执行、已完成、待执行、延后 cleanup 和可能无法自动回滚的原子 DDL 步骤。失败时拒绝监听；不会声称多条 MySQL DDL 可以整体事务回滚。

## 构建与依赖

Windows vcpkg manifest 依赖为 `cpp-httplib`、`libmysql`、`libsodium`、`nlohmann-json` 和 `openssl`。Linux 通过 pkg-config/CMake 查找对应开发包。OpenSSL 3 已作为后续安全模块基础链接，但阶段 1 尚未实现 TOTP。

Windows（MSVC 开发者终端）：

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DBUILD_TESTING=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Linux：

```bash
sudo apt-get install libcpp-httplib-dev libmysqlclient-dev libsodium-dev \
  libssl-dev nlohmann-json3-dev pkg-config
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Release 测试目标显式取消 `NDEBUG`，避免基于 `assert` 的测试在 CI 的 Release 构建中变成空操作。

## 测试与 CI

当前 CMake 注册 14 项测试；不需外部服务的 13 项覆盖：

- Argon2id 分类、加盐、验证、rehash 和敏感字符串清理。
- typed 遗留用例的认证、权限、字段规范化、默认货位和错误映射。
- 配置校验、组合根顺序、前置失败不监听、生命周期与信号停止。
- 有界连接池、事务隔离/提交未知、参数化遗留仓储和不物理删除。
- 迁移发现、单语句约束、校验和、版本/步骤状态、命名锁和失败停止。
- 真实回环 HTTP 上的四个遗留路由、严格 JSON、状态码、精确 CORS 和静态路径防穿越。

GitHub Actions 工作流配置为 Ubuntu/GCC、Ubuntu/Clang 和 Windows/MSVC 的 Release 构建与 CTest。工作流配置存在不表示当前工作区已经实际运行 CI；阶段报告必须单独记录真实 CI 状态。

第 14 项 `mysql_integration_test` 使用真实 MySQL 8 验证绑定、并发可见性所体现的事务隔离、连接重建、迁移重入、旧数据保留和长二进制。它只在显式提供 `WAREHOUSE_TEST_DB_*`、数据库名包含 `test` 且 `WAREHOUSE_TEST_DB_ALLOW_SCHEMA_CHANGES=YES` 时改动专用测试库；否则以 CTest skip 码 77 安全跳过。本阶段已在专用本地测试库上分别以 Debug 和 Release 运行通过。本地没有专用测试库凭据时仍必须标为 `BLOCKED` 或 `NOT_RUN`，不得用 fake executor 单元测试替代。

## 当前安全与运行边界

- HTTP 适配器使用有界线程池、队列、请求头/载荷限制、读写超时和安全静态路径解析。
- JSON 由 `nlohmann/json` 严格解析；业务值通过 MySQL 预处理语句绑定，二进制参数使用 `mysql_stmt_send_long_data`。
- MySQL 客户端库、线程上下文、连接、租约和事务由 RAII 管理；会话设置 UTC、严格 SQL 模式且禁用自动重连。
- 监听器只有报告 ready 后才输出 listening；启动前退出或运行时异常停止会返回失败。
- CORS 默认不授权跨源，仅对配置中的精确来源返回允许头。
- 遗留 Bearer Token 由 libsodium 生成 256 位随机值，但仍只保存在进程内存中，没有阶段 2 的数据库撤销、闲置/绝对期限、Cookie、CSRF 或 TOTP。
- 当前只提供 HTTP，不内置 TLS；生产部署不能据此声称浏览器会话安全目标已完成。
- 没有用户管理 API，非初始管理员账户仍需受控运维流程。

## 本地文件与验证语义

CLion、构建目录、vcpkg 安装树、日志、`.env*` 和代理状态由根 `.gitignore` 排除。构建输出只应位于受忽略的构建目录，不应在仓库根目录留下 `.obj` 或探针文件。

阶段报告只使用 `PASSED`、`FAILED`、`BLOCKED`、`NOT_RUN` 和 `SKIPPED`。只有实际执行、退出码为零且输出已检查的命令可以标为 `PASSED`；本地通过不能代替 CI，fake executor 不能代替真实 MySQL，文档检查也不能代替构建、迁移或功能测试。
