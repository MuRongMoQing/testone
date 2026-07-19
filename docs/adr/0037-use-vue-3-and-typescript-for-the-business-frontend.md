# 业务前端采用 Vue 3 与 TypeScript

目标六身份工作台使用 Vue 3、Vite、TypeScript、Vue Router 和 Pinia，单元测试使用 Vitest，浏览器端到端测试使用 Playwright。前端按身份和业务能力拆分路由、页面及状态模块，并通过 `/api/v1` 访问后端。

当前 `public/` 下的原生 HTML、CSS 和 JavaScript 仍是遗留实现；只有在新工作台的对应用例通过协议、权限及端到端测试并经用户批准后才切换。前端隐藏字段或入口只改善体验，不能替代后端权限检查。该选择增加 Node 构建链，但为复杂角色工作流、类型化 API 和自动化浏览器验收提供明确边界。
