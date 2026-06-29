let token = localStorage.getItem("warehouse_token") || "";
let role = localStorage.getItem("warehouse_role") || "";
let username = localStorage.getItem("warehouse_username") || "";

const loginPanel = document.querySelector("#loginPanel");
const dashboard = document.querySelector("#dashboard");
const userInfo = document.querySelector("#userInfo");
const message = document.querySelector("#message");
const rows = document.querySelector("#goodsRows");
const editorPanel = document.querySelector("#editorPanel");

function canWrite() {
  return role === "admin" || role === "manager";
}

function setMessage(text, isError = false) {
  message.textContent = text;
  message.className = isError ? "message error" : "message";
}

function cell(text) {
  const td = document.createElement("td");
  td.textContent = text;
  return td;
}

function showApp() {
  const loggedIn = Boolean(token);
  loginPanel.classList.toggle("hidden", loggedIn);
  dashboard.classList.toggle("hidden", !loggedIn);
  userInfo.textContent = loggedIn ? `${username} / ${role}` : "";
  editorPanel.classList.toggle("hidden", !canWrite());
  if (loggedIn) {
    loadGoods();
  }
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      "Authorization": token ? `Bearer ${token}` : "",
      ...(options.headers || {})
    }
  });
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || "请求失败");
  }
  return data;
}

async function login() {
  try {
    const data = await api("/api/login", {
      method: "POST",
      body: JSON.stringify({
        username: document.querySelector("#username").value.trim(),
        password: document.querySelector("#password").value
      })
    });
    token = data.token;
    role = data.role;
    username = data.username;
    localStorage.setItem("warehouse_token", token);
    localStorage.setItem("warehouse_role", role);
    localStorage.setItem("warehouse_username", username);
    setMessage("");
    showApp();
  } catch (error) {
    alert(error.message);
  }
}

async function loadGoods() {
  try {
    const name = encodeURIComponent(document.querySelector("#searchName").value.trim());
    const status = encodeURIComponent(document.querySelector("#statusFilter").value);
    const data = await api(`/api/goods?name=${name}&status=${status}`);
    rows.innerHTML = "";
    for (const item of data.items) {
      const tr = document.createElement("tr");
      tr.appendChild(cell(item.id));
      tr.appendChild(cell(item.name));
      tr.appendChild(cell(item.location));
      const statusCell = document.createElement("td");
      const badge = document.createElement("span");
      badge.className = `badge ${item.status === "taken" ? "taken" : ""}`;
      badge.textContent = item.status === "stored" ? "在库" : "已取出";
      statusCell.appendChild(badge);
      tr.appendChild(statusCell);
      tr.appendChild(cell(item.storedAt));
      tr.appendChild(cell(item.takenAt || "-"));
      tr.appendChild(cell(item.operator));
      const actionCell = document.createElement("td");
      const button = document.createElement("button");
      button.textContent = "取出";
      button.disabled = item.status !== "stored" || !canWrite();
      button.addEventListener("click", () => takeGoods(item.id));
      actionCell.appendChild(button);
      tr.appendChild(actionCell);
      rows.appendChild(tr);
    }
    if (data.items.length === 0) {
      const tr = document.createElement("tr");
      tr.innerHTML = `<td colspan="8">暂无货物记录</td>`;
      rows.appendChild(tr);
    }
  } catch (error) {
    setMessage(error.message, true);
  }
}

async function storeGoods() {
  try {
    const name = document.querySelector("#goodsName").value.trim();
    const location = document.querySelector("#goodsLocation").value.trim();
    const data = await api("/api/goods", {
      method: "POST",
      body: JSON.stringify({ name, location })
    });
    document.querySelector("#goodsName").value = "";
    document.querySelector("#goodsLocation").value = "";
    setMessage(`入库成功，编号 ${data.item.id}`);
    loadGoods();
  } catch (error) {
    setMessage(error.message, true);
  }
}

async function takeGoods(id) {
  try {
    await api("/api/goods/take", {
      method: "POST",
      body: JSON.stringify({ id })
    });
    setMessage(`${id} 已取出`);
    loadGoods();
  } catch (error) {
    setMessage(error.message, true);
  }
}

document.querySelector("#loginBtn").addEventListener("click", login);
document.querySelector("#logoutBtn").addEventListener("click", () => {
  token = "";
  role = "";
  username = "";
  localStorage.clear();
  showApp();
});
document.querySelector("#searchBtn").addEventListener("click", loadGoods);
document.querySelector("#storeBtn").addEventListener("click", storeGoods);

showApp();
