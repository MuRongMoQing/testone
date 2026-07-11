# API 参考

仓储存取系统后端 API，基地址 `http://localhost:8081`。

---

## 1. 登录

`POST /api/login`

**请求体** (JSON)：

| 字段 | 类型 | 必填 | 说明 |
|------|------|:--:|------|
| `username` | string | ✓ | 用户名 |
| `password` | string | ✓ | 密码 |

**请求示例**：

```json
{"username":"admin","password":"admin123"}
```

**成功响应** `200`：

```json
{
  "token": "admin-1752249600-1000",
  "role": "admin",
  "username": "admin"
}
```

| 字段 | 说明 |
|------|------|
| `token` | Bearer Token，后续请求需放入 `Authorization` 头 |
| `role` | 用户角色：`admin` / `manager` / `viewer` |
| `username` | 登录用户名 |

**错误响应**：

| 状态码 | error 消息 | 触发条件 |
|--------|-----------|----------|
| 400 | `用户名和密码不能为空` | username 或 password 为空 |
| 401 | `用户名或密码错误` | 用户名不存在或密码不匹配 |
| 500 | `数据库查询失败` | MySQL 连接/查询异常 |

---

## 2. 查询货物列表

`GET /api/goods?name=<name>&status=<status>`

**请求头**：

```http
Authorization: Bearer <token>
```

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|:--:|------|
| `name` | string | ✗ | 货物名模糊匹配（大小写不敏感） |
| `status` | string | ✗ | 状态筛选：`stored` / `taken` |

**请求示例**：

```http
GET /api/goods?name=电脑&status=stored
Authorization: Bearer admin-1752249600-1000
```

**成功响应** `200`：

```json
{
  "items": [
    {
      "id": "G000001",
      "name": "笔记本电脑",
      "location": "A-01",
      "status": "stored",
      "storedAt": "2025-06-15 14:30:00",
      "takenAt": "",
      "operator": "admin"
    },
    {
      "id": "G000002",
      "name": "台式电脑",
      "location": "B-03",
      "status": "stored",
      "storedAt": "2025-06-16 09:15:00",
      "takenAt": "",
      "operator": "manager"
    }
  ]
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | string | 货物编号（`G` + 6 位数字） |
| `name` | string | 货物名称 |
| `location` | string | 货位 |
| `status` | string | `stored`（在库）/ `taken`（已取出） |
| `storedAt` | string | 入库时间 `YYYY-MM-DD HH:MM:SS` |
| `takenAt` | string | 取出时间；在库时为空字符串 |
| `operator` | string | 最后操作人用户名 |

**错误响应**：

| 状态码 | error 消息 | 触发条件 |
|--------|-----------|----------|
| 401 | `请先登录` | 未提供或 Token 无效 |
| 500 | `数据库查询失败` | 数据库异常 |

---

## 3. 入库

`POST /api/goods`

**权限**：manager 及以上。

**请求头**：

```http
Authorization: Bearer <token>
```

**请求体** (JSON)：

| 字段 | 类型 | 必填 | 说明 |
|------|------|:--:|------|
| `name` | string | ✓ | 货物名称 |
| `location` | string | ✗ | 货位；不填默认 `默认货架` |

**请求示例**：

```json
{"name":"显示器","location":"A-01"}
```

**成功响应** `201`：

```json
{
  "item": {
    "id": "G000003",
    "name": "显示器",
    "location": "A-01",
    "status": "stored",
    "storedAt": "2025-06-16 10:00:00",
    "takenAt": "",
    "operator": "admin"
  }
}
```

**错误响应**：

| 状态码 | error 消息 | 触发条件 |
|--------|-----------|----------|
| 400 | `货物名称不能为空` | name 为空 |
| 401 | `请先登录` | 未鉴权 |
| 403 | `当前用户无此权限` | 角色为 viewer |
| 500 | `入库失败` | 数据库写入异常 |

---

## 4. 取出货物

`POST /api/goods/take`

**权限**：manager 及以上。

**请求头**：

```http
Authorization: Bearer <token>
```

**请求体** (JSON)：

| 字段 | 类型 | 必填 | 说明 |
|------|------|:--:|------|
| `id` | string | ✓ | 货物编号（`G000001` 或 `1` 均可） |

**请求示例**：

```json
{"id":"G000001"}
```

**成功响应** `200`：

```json
{
  "item": {
    "id": "G000001",
    "name": "笔记本电脑",
    "location": "A-01",
    "status": "taken",
    "storedAt": "2025-06-15 14:30:00",
    "takenAt": "2025-06-16 15:00:00",
    "operator": "admin"
  }
}
```

**错误响应**：

| 状态码 | error 消息 | 触发条件 |
|--------|-----------|----------|
| 400 | `无效的货物编号` | id 不合法 |
| 400 | `该货物已取出` | 货物当前状态已是 taken |
| 401 | `请先登录` | 未鉴权 |
| 403 | `当前用户无取出权限` | 角色为 viewer |
| 404 | `未找到该货物` | id 对应的货物不存在 |
| 500 | `数据库查询失败` / `取出失败` | 数据库异常 |

---

## 5. 静态资源

任何非 `/api/` 前缀的 GET 请求路由到 `public/` 目录下的文件。

| 路径 | 文件 |
|------|------|
| `/`、`/index.html` | `public/index.html` |
| `/app.js` | `public/app.js` |
| `/styles.css` | `public/styles.css` |

**错误响应**：

| 状态码 | error 消息 |
|--------|-----------|
| 404 | `资源不存在` |
| 405 | `方法不支持` |

---

## 通用说明

### 鉴权

所有 `/api/goods*` 端点需在请求头中携带 Token：

```http
Authorization: Bearer <token>
```

Token 由 `/api/login` 返回，格式为 `<username>-<timestamp>-<seed>`，保存在服务端内存中。**服务重启后所有 Token 失效**。

### 内容类型

所有请求和响应均为 `application/json; charset=utf-8`。

### CORS

服务端设置 `Access-Control-Allow-Origin: *`，允许任意来源跨域请求。

### 自动清理

每次查询、入库、取出操作前，会执行：

```sql
DELETE FROM goods WHERE status='taken'
  AND taken_at IS NOT NULL
  AND taken_at < NOW() - INTERVAL 30 DAY
```

即已取出超过 30 天的记录会被自动物理删除。
