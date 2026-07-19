# API 边缘采用 cpp-httplib 与 nlohmann-json

现有 `src/main.cpp` 手写套接字 HTTP 报文读取、请求行和请求头解析，并用正则表达式提取 JSON 字符串；该实现难以安全覆盖分块或异常请求、严格 JSON 类型、multipart、大小限制、超时和受控并发。首版通过 vcpkg 锁定 [cpp-httplib](https://github.com/yhirose/cpp-httplib) 与 [nlohmann/json](https://github.com/nlohmann/json)，由前者承担 HTTP/HTTPS、线程池和 multipart 协议处理，由后者承担标准 JSON 解析、类型校验基础与序列化。

两个第三方库只存在于 `warehouse_api` 及最外层网络适配代码。路由将请求转换为项目自有 DTO、认证上下文和应用命令，再把应用结果转换为响应；领域与应用层的公共头文件不得暴露 `httplib` 或 `nlohmann::json` 类型。这样未来更换协议库时不需要修改库存、权限或会计规则。

服务使用有界线程池和配置化的请求头、JSON 正文、multipart 总体、单附件、读写及空闲超时限制，保留单附件 20 MiB 上限。CORS 改为显式允许列表；协议层统一拒绝畸形 HTTP、非法 JSON、错误字段类型及超限请求。生产 TLS 可以由受控反向代理终止或由 HTTPS 适配器提供，但任何部署都不能继续依赖只适合本地开发的裸露公共 HTTP。

API 协议测试固定路由、方法、认证头、状态码、错误对象、JSON 字段和 multipart 行为。迁移先在新适配器后运行现有端点测试，再删除手写解析器与每连接分离线程；不得在没有等价协议测试时同时替换协议层和改变业务语义。该选择增加两个受控依赖，但显著缩小自研协议与解析攻击面。
