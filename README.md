# 仓储存取系统

> **当前状态：阶段 1 平台基础已获用户审查批准。** Debug 与 Release 的完整 CTest 各 14 项均已通过，其中包含专用本地 MySQL 8 的真实迁移、事务和连接测试；阶段状态为 `APPROVED`，本次提交发布该阶段成果。本页不代表阶段 2 以后目标已经可用；目标架构、权限和实施阶段分别见 [系统架构](docs/ARCHITECTURE.md)、[身份与权限](docs/PERMISSIONS.md)、[领域语言](CONTEXT.md) 和 [实施计划](docs/IMPLEMENTATION_PLAN.md)。

C++17 模块化单体后端 + 静态前端。阶段 1 保留原有登录、货物入库、查询、取出、自动编号和三级角色协议，但已将 HTTP/JSON、应用用例、MySQL 工作单元、迁移和启动生命周期拆为独立模块。旧记录不再由请求路径物理删除；六身份、申请审批、数量金额、Cookie 会话和 `/api/v1` 仍未实现。

## 当前遗留功能

| 功能 | 说明 |
|------|------|
| 入库 | 填写货物名称与货位，自动生成 `G000001` 格式编号 |
| 查询 | 按货物名和状态（在库/已取出）筛选 |
| 取出 | 记录取出时间与操作员 |
| 历史保留 | 已取出记录继续保留；阶段 1 不再执行请求时物理删除 |
| 权限控制 | `admin`、`manager`、`viewer` 三级角色 |
| 登录鉴权 | Argon2id 密码哈希 + 内存 Bearer Token |

首次启动只创建一个由环境变量指定的管理员。系统不会创建或公开默认账号和口令。

## 前置条件

- C++17 编译器
- CMake 3.16 或更高版本
- MySQL 8.0
- Windows：vcpkg（manifest 模式安装 `cpp-httplib`、`libmysql`、`libsodium`、`nlohmann-json`、`openssl`）
- Linux：`libcpp-httplib-dev`、`libmysqlclient-dev`、`libsodium-dev`、`libssl-dev`、`nlohmann-json3-dev`、`pkg-config`

## 构建

Windows（在 MSVC 开发者终端中）：

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Linux：

```bash
sudo apt-get install libmysqlclient-dev libsodium-dev pkg-config
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## 配置与启动

数据库连接不使用代码内默认值，以下变量全部必填：

```powershell
$env:WAREHOUSE_DB_HOST = "localhost"
$env:WAREHOUSE_DB_PORT = "3306"
$env:WAREHOUSE_DB_NAME = "warehouse"
$env:WAREHOUSE_DB_USER = "warehouse_app"
$env:WAREHOUSE_DB_PASSWORD = "<database-password>"
```

当数据库里还没有管理员时，还需为首次启动设置：

```powershell
$env:WAREHOUSE_ADMIN_USERNAME = "<initial-admin>"
$env:WAREHOUSE_ADMIN_PASSWORD = "<strong-password>"
```

初始管理员密码要求 12–128 个字符，写入数据库前会转换为 Argon2id 哈希。启动后访问 [http://localhost:8081](http://localhost:8081)。

### 迁移旧版明文密码

升级旧数据库前先完成备份。服务检测到明文密码时会拒绝启动；确认备份有效后，只为一次启动设置：

```powershell
$env:WAREHOUSE_MIGRATE_PASSWORDS = "1"
```

迁移在 MySQL 事务中执行，任一用户失败会整体回滚。成功后立即清除此变量。迁移不可逆；如需退回不支持 Argon2id 的旧程序，必须恢复迁移前备份。

## 项目结构

```text
.
├── src/
│   ├── main.cpp              # 仅进入启动组合根
│   ├── api/                  # cpp-httplib / nlohmann JSON 兼容适配器
│   ├── application/          # typed 遗留用例和工作单元端口
│   ├── bootstrap/            # 配置、迁移门、组合根和停止协调
│   ├── domain/               # 遗留领域值对象
│   ├── infrastructure/       # MySQL、密码和会话适配器
│   ├── legacy_server_reference.cpp # 未编译的阶段前单文件参考
│   └── password_hash.*       # Argon2id 密码哈希边界
├── migrations/              # 版本化、可重入 SQL 步骤
├── tests/
│   └── ...                   # 应用、API、bootstrap、MySQL 与密码测试
├── public/                   # HTML/CSS/JS 前端
├── docs/
│   ├── API.md
│   ├── DEVELOPMENT.md
│   └── adr/
├── CONTEXT.md                # 领域词汇表
├── CMakeLists.txt
├── CMakeSettings.json        # Visual Studio 本地/远程配置
└── vcpkg.json                # Windows C++ 依赖清单
```

## 当前遗留 API 概览

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|:--:|
| POST | `/api/login` | 登录，返回 Token | ✗ |
| GET | `/api/goods` | 查询货物列表 | ✓ |
| POST | `/api/goods` | 入库新货物 | manager+ |
| POST | `/api/goods/take` | 取出货物 | manager+ |

详细格式见 [当前遗留 API 参考](docs/API.md)，开发与迁移说明见 [开发指南](docs/DEVELOPMENT.md)。计划中的 `/api/v1` 尚未实现，其资源边界和引入阶段只在实施计划及 ADR 0036 中记录。

## 许可证

Eclipse Public License 2.0，详见 [LICENSE.txt](LICENSE.txt)。
