# API 参考

仓储存取系统后端 API，默认基地址为 `http://localhost:8081`。所有请求和响应均使用 `application/json; charset=utf-8`。

## 登录

`POST /api/login`

请求体：

```json
{
  "username": "<username>",
  "password": "<password>"
}
```

成功响应 `200`：

```json
{
  "token": "<token>",
  "role": "admin",
  "username": "<username>"
}
```

| 状态码 | 说明 |
|--------|------|
| 400 | 用户名或密码为空 |
| 401 | 用户名不存在或密码不匹配 |
| 500 | 数据库查询或密码哈希升级失败 |

系统不提供默认登录口令。首次管理员由服务端环境变量创建，其他账户由管理员按部署流程配置。

## 查询货物

`GET /api/goods?name=<name>&status=<status>`

请求头：

```http
Authorization: Bearer <token>
```

`name` 为可选模糊匹配，`status` 可取 `stored` 或 `taken`。

成功响应 `200`：

```json
{
  "items": [
    {
      "id": "G000001",
      "name": "显示器",
      "location": "A-01",
      "status": "stored",
      "storedAt": "2026-07-18 10:00:00",
      "takenAt": "",
      "operator": "warehouse-admin"
    }
  ]
}
```

## 入库

`POST /api/goods`，需要 `manager` 或 `admin`。

```json
{
  "name": "显示器",
  "location": "A-01"
}
```

`name` 必填；`location` 为空时使用“默认货架”。成功返回 `201` 和新建的 `item`。

## 取出货物

`POST /api/goods/take`，需要 `manager` 或 `admin`。

```json
{
  "id": "G000001"
}
```

编号也可以使用不带 `G` 前缀的数字。成功返回 `200` 和更新后的 `item`。

## 静态资源

| 路径 | 文件 |
|------|------|
| `/`、`/index.html` | `public/index.html` |
| `/app.js` | `public/app.js` |
| `/styles.css` | `public/styles.css` |

非 `/api/` 的 GET 请求由 `public/` 提供；其他方法返回 `405`。

## 通用行为

- 所有 `/api/goods*` 端点都要求 `Authorization: Bearer <token>`。
- Token 保存在进程内存中，服务重启后失效。
- 服务当前返回 `Access-Control-Allow-Origin: *`。
- 每次查询、入库或取出前，物理删除已取出超过 30 天的记录。
