# 仓储存取系统

C++ 后端 + 静态前端的前后端分离仓库管理系统。支持货物入库、查询、取出、自动编号、超期记录自动清理及三级权限控制。

## 功能

| 功能 | 说明 |
|------|------|
| 入库 | 填写货物名称与货位，自动生成编号（`G000001` 格式） |
| 查询 | 按货物名（模糊）和状态（在库/已取出）筛选 |
| 取出 | 将货物状态置为 `taken`，记录取出时间与操作员 |
| 自动清理 | 已取出超 30 天的记录在后续请求中自动删除 |
| 权限控制 | 三级角色：admin、manager、viewer |
| 登录鉴权 | Bearer Token，会话保存在内存中 |

### 用户角色

| 角色 | 默认账号 | 查询 | 入库 | 取出 |
|------|----------|:--:|:--:|:--:|
| admin | `admin` / `admin123` | ✓ | ✓ | ✓ |
| manager | `manager` / `manager123` | ✓ | ✓ | ✓ |
| viewer | `viewer` / `viewer123` | ✓ | ✗ | ✗ |

## 快速开始

### 前置条件

- C++17 编译器（MSVC 2019+、GCC 8+ 或 Clang 7+）
- CMake ≥ 3.16
- MySQL 8.0（已创建数据库 `first_test`）

### 构建 & 运行

```powershell
# Windows (MSVC + Ninja)
cmake -S . -B build
cmake --build build
.\build\warehouse_server.exe
```

启动后访问 **http://localhost:8081**。

> MySQL 连接参数硬编码在 `src/main.cpp` 第 216 行：`localhost:3306`，用户 `root`，密码 `JBY@ll370079`，数据库 `first_test`。如需修改，编辑该行后重新编译。

## 技术栈

| 层 | 技术 |
|----|------|
| 后端 | C++17、原生 Berkeley Sockets、libmysql |
| 前端 | 原生 HTML/CSS/JS（无框架） |
| 数据库 | MySQL 8.0（InnoDB，utf8mb4） |
| 构建 | CMake + Ninja (Windows) / Make (Linux) |
| CI | GitHub Actions（多平台矩阵：Windows + Linux） |

## 项目结构

```
.
├── src/
│   └── main.cpp          # 后端全部代码（单文件，~750 行）
├── public/
│   ├── index.html        # 前端登录 + 管理页面
│   ├── app.js            # 前端逻辑（API 调用、DOM 操作）
│   └── styles.css        # 前端样式
├── CMakeLists.txt         # CMake 构建配置
├── CMakeSettings.json     # Visual Studio CMake 配置
├── .github/workflows/     # CI 流水线
└── docs/                  # 更多文档 →
    ├── API.md             # API 接口参考
    └── DEVELOPMENT.md     # 开发指南与架构说明
```

## API 概览

| 方法 | 路径 | 说明 | 鉴权 |
|------|------|------|:--:|
| POST | `/api/login` | 登录，返回 token | ✗ |
| GET | `/api/goods` | 查询货物列表 | ✓ |
| POST | `/api/goods` | 入库新货物 | ✓ (manager+) |
| POST | `/api/goods/take` | 取出货物 | ✓ (manager+) |

详细请求/响应格式见 [API.md](docs/API.md)。

## 许可证

MIT License — 详见 [LICENSE.txt](LICENSE.txt)。
