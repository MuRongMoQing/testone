# 仓储存取系统

这是一个 C++ 后端 + 静态前端的前后端分离示例项目，支持货物入库、查询、取出、自动编号、已取出旧记录自动清理，以及不同权限用户登录。

## 功能

- 新货物入库时自动生成编号，例如 `G000001`
- 保存货物名、货位、状态、入库时间、取出时间和操作员
- 支持按货物名和状态查询
- 支持取出货物，取出后状态变为 `taken`
- 已取出超过 30 天的旧空缺记录会在后续请求中自动移除
- 用户权限：
  - `admin/admin123`：查询、入库、取出
  - `manager/manager123`：查询、入库、取出
  - `viewer/viewer123`：仅查询
- 后端 API 与前端页面分离，前端通过 HTTP API 调用后端

## 构建运行

如果本机已安装 CMake：

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\warehouse_server.exe
```

如果使用 MinGW：

```powershell
g++ -std=c++17 src\main.cpp -lws2_32 -o warehouse_server.exe
.\warehouse_server.exe
```

Linux/macOS：

```bash
g++ -std=c++17 src/main.cpp -pthread -o warehouse_server
./warehouse_server
```

启动后访问：

```text
http://localhost:8080
```

数据会保存到运行目录下的 `warehouse_data.tsv`。

## API

### 登录

`POST /api/login`

```json
{"username":"admin","password":"admin123"}
```

### 查询货物

`GET /api/goods?name=电脑&status=stored`

需要请求头：

```text
Authorization: Bearer <token>
```

### 入库

`POST /api/goods`

```json
{"name":"显示器","location":"A-01"}
```

### 取出

`POST /api/goods/take`

```json
{"id":"G000001"}
```
