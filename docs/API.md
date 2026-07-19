# 当前遗留 API 参考

> **实现状态：CURRENT LEGACY。** 本文件以下接口与安全行为对应当前 `src/main.cpp`，不是目标 `/api/v1` 契约。计划中的 Cookie 会话、CSRF、六身份权限、库存申请和多仓接口尚未实现；其实施顺序见 [实施计划](IMPLEMENTATION_PLAN.md)，版本策略见 [ADR 0036](adr/0036-version-new-apis-under-api-v1.md)。

当前遗留后端 API 的默认基地址为 `http://localhost:8081`。所有请求和响应均使用 `application/json; charset=utf-8`。

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

## 已批准但尚未实现的目标接口

所有新业务接口计划进入 `/api/v1`，当前阶段不得视为可调用。目标资源组包括会话/TOTP/通知、仓库与货位、SKU 与汇总库存、附件、公共库存申请及审批/复核/取消/执行、治理、会计、跨仓调拨和账册集成。普通列表计划使用默认 50、最大 200 条的稳定键游标并返回 `items`/`nextCursor`；错误对象计划包含 `code`、`message`、`details`、`requestId`。

旧接口将在对应新用例、数据迁移、对账和用户审批完成前冻结保留；本文件只有在相应代码真实落地后才能增加已实现 `/api/v1` 请求与响应示例。
