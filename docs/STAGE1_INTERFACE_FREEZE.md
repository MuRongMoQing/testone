# 阶段 1 接口冻结

状态：`FROZEN`
阶段：平台基础（`APPROVED`）
依据：ADR 0017、0030–0033、0036，以及已冻结的遗留 API 文档。

本文只冻结阶段 1 的模块接口、依赖、路径所有权和测试接缝，不新增阶段 2 及以后业务规则。接口冻结后，各 Agent 只在独占路径实现；共享接口、CMake、vcpkg、主入口、组合根和总路由由主 Agent 串行集成。

## 模块和依赖

```text
warehouse_server (main)
  -> warehouse_bootstrap
       -> warehouse_api
            -> warehouse_application
                 -> warehouse_domain
       -> warehouse_mysql
            -> warehouse_application
            -> warehouse_domain
```

- `warehouse_domain`：只包含遗留用户和货物值对象，不依赖 HTTP、JSON、MySQL 或启动配置。
- `warehouse_application`：暴露 typed legacy 用例接口、结果类型和工作单元端口；不得出现 `httplib`、`nlohmann::json`、`MYSQL*` 或 SQL。
- `warehouse_api`：唯一允许依赖 `cpp-httplib` 与 `nlohmann/json` 的模块，负责遗留协议映射和静态文件。
- `warehouse_mysql`：唯一允许依赖 MySQL C API 的模块，隐藏连接、语句、结果、事务、连接池和迁移细节。
- `warehouse_bootstrap`：加载并校验配置，表达启动失败和停止协调；最终组合根负责构造顺序。
- `main.cpp`：只调用组合根并返回进程结果。

## 冻结的应用接口

共享接口由主 Agent 在并行实现前建立：

- `src/application/common/result.hpp`
- `src/application/legacy/legacy_warehouse_api.hpp`
- `src/application/transactions/unit_of_work.hpp`
- `src/application/legacy/legacy_unit_of_work.hpp`
- `src/domain/legacy/legacy_models.hpp`

`LegacyWarehouseApi` 只暴露 `login`、`listGoods`、`createGoods`、`takeGoods` 四个 typed 用例。HTTP 状态、中文错误文案、JSON 字段和 Bearer 头解析均不进入该接口。`LegacyUnitOfWorkFactory` 只允许从 `Command`、`ShortRead`、`FinalizedRead` 三种固定模式开始工作单元，调用方不能传任意隔离级别或执行任意 SQL。

应用错误和持久化错误使用项目自有枚举及 `Result<T, E>` 返回。提交结果不确定必须显式表示，不能改写成已回滚。阶段 1 的会话适配器仍保持遗留 Bearer 协议；数据库 Cookie 会话属于阶段 2。

## 冻结的遗留 HTTP 契约

| 方法与路径 | 成功状态 | 成功对象 |
|---|---:|---|
| `POST /api/login` | 200 | `token`、`role`、`username` |
| `GET /api/goods` | 200 | `items` 和七字段货物对象 |
| `POST /api/goods` | 201 | `item` |
| `POST /api/goods/take` | 200 | `item` |

- 遗留错误仍为 `{error:<中文文案>}`；不得倒灌 `/api/v1` 的统一错误对象。
- 货物 `id` 仍格式化为 `G` 加六位数字；取出仍接受 `G`、`g` 前缀和纯数字。
- `name`、`status` 查询，默认货位 `默认货架`，字段规范化和现有成功字段保持兼容。
- 严格 JSON 会拒绝旧正则解析器曾误接受的畸形输入；该安全收紧返回 400。
- 未知 `/api/*` 返回 404；静态资源非 GET 返回 405。
- 不注册虚假的 `/api/v1` 占位路由。
- CORS 从通配符改为精确允许列表；同源或无 `Origin` 请求正常处理，未允许来源不返回授权头。

## 冻结的 MySQL 接口

`warehouse_mysql` 的外部接口只提供：

- 进程级 `ClientLibrary` 与线程级 `ThreadContext` 生命周期。
- 固定容量、有限等待、独占租约的 `ConnectionPool`。
- 实现 `LegacyUnitOfWorkFactory` 的 MySQL 适配器。
- 接受已发现迁移定义并返回结构化 `MigrationReport` 的迁移运行器。

模块内部使用 move-only `Connection`、`PreparedStatement`、`ResultSet` 和 `Transaction`。所有业务值必须绑定；无 `double` 参数接口；连接禁用 multi-statements 和静默自动重连，设置 UTC 与严格 SQL 模式。不洁净连接、失联连接或提交结果不确定的连接不得归还池中。

## 迁移磁盘契约

- 根目录为 `migrations/NNNN_slug/`，按版本号排序。
- 每个步骤声明唯一 step id、`precheck|expand|backfill|switch|cleanup` 阶段、执行类型、apply SQL、verify SQL 和 SHA-256。
- 每个 apply 文件只允许一个服务器语句；变更步骤必须有返回单个布尔/整数的 verify。
- 已成功步骤 checksum 改变立即拒绝启动；`RUNNING` 或 `FAILED` 重启时先 verify，已达目标则记成功，否则只允许相同 checksum 重试。
- DDL 逐步骤执行并验证，不声称多 DDL 整版回滚；事务化 DML 失败只回滚对应 DML 事务。
- cleanup 默认不在普通启动执行，阶段 1 基线不包含 cleanup。
- 迁移器持有数据库级命名锁；锁超时、结构不兼容或任一必需步骤失败均拒绝监听 HTTP。
- 阶段 1 只建立并验证遗留 `users`、`goods` 基线和迁移元数据，不创建阶段 2+ 业务表，不修改、合并或删除旧数据。

## 配置与组合根

- 保留五个现有必填数据库变量：`WAREHOUSE_DB_HOST`、`WAREHOUSE_DB_PORT`、`WAREHOUSE_DB_NAME`、`WAREHOUSE_DB_USER`、`WAREHOUSE_DB_PASSWORD`。
- HTTP 地址和端口默认保持 `127.0.0.1:8081`。
- HTTP worker 与数据库池容量可配置；未提供时均使用 1，以保持当前串行数据库访问的保守兼容语义。
- CORS 允许来源默认空列表，可通过配置提供精确来源。
- 配置一次收集全部错误，错误对象只含键名和安全说明，不含密码原值。
- 启动顺序固定为配置、MySQL 运行时、连接池、迁移、遗留启动检查、应用/API 组装、最后监听。
- 迁移或任何前置步骤失败时不得绑定端口或输出“server running”。

## 路径所有权

- `mysql_foundation`：`src/infrastructure/mysql/**`、`migrations/**`、`tests/unit/infrastructure/mysql/**`、`tests/integration/mysql/**`。
- `api_foundation`：`src/api/**`、`tests/api/**`、`tests/support/fake_legacy_warehouse_api.hpp`。
- `bootstrap_foundation`：`src/bootstrap/configuration.*`、`src/bootstrap/lifecycle.*` 及其独立测试。
- 主 Agent：共享 application/domain 接口和实现、CMake、vcpkg、CI、`src/main.cpp`、组合根、总路由、公共文档及串行集成测试。

任何 Agent 不得修改其他 Agent 独占路径。接口变更必须先停止并交由主 Agent 更新本冻结文件。

## 测试接缝和当前环境

- API：用 fake `LegacyWarehouseApi` 经真实回环 HTTP 验证四个遗留路由、JSON、状态、CORS 和静态文件。
- 应用：用 fake `LegacyUnitOfWorkFactory` 验证认证、权限、字段规范化、默认货位和错误映射。
- MySQL 单元：用 fake connection/executor 验证池、事务、迁移状态机、校验和及失败停止。
- MySQL 集成：必须使用真实 MySQL 8 验证绑定、事务隔离、连接重建、迁移恢复和旧数据不变。
- bootstrap：用内存配置源和假启动适配器验证配置错误、启动顺序及失败时不监听。

接口冻结时本机 PATH 中没有 `cmake`、`ctest`、`ninja` 或 C++ 编译器。该事实只表示构建门当前受阻；不得把静态检查或 Agent 声明记为构建、CTest、MySQL 集成或协议测试通过。阶段 1 在获得真实工具链验证前保持 `IN_PROGRESS`。

## 阶段 1 非目标

- 不实现六身份、Cookie 会话、TOTP、CSRF 或 `/api/v1` 业务资源。
- 不创建新库存、申请、会计、附件、多仓或审计业务表。
- 不迁移 Vue，不修改现有前端业务流程。
- 不执行生产迁移、连接真实总账或发布生产版本。
