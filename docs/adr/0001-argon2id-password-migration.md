---
status: accepted
---

# 使用 Argon2id 并显式授权旧密码迁移

用户密码从数据库明文比较迁移到 libsodium Argon2id 验证字符串。迁移采用显式环境开关和单个 MySQL 事务，因为立即消除明文比保持旧程序兼容更重要，同时必须防止一次普通启动在没有备份和操作员确认时执行不可逆转换。

## Consequences

旧数据库第一次迁移前必须完成可恢复备份并设置 `WAREHOUSE_MIGRATE_PASSWORDS=1`；迁移成功后旧版程序无法验证新哈希，回退必须同时恢复数据库。Windows 和 Linux 构建都新增 libsodium 依赖。
