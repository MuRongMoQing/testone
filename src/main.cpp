#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif

#include <mysql.h>

namespace {
    constexpr int kPort = 8081;
constexpr int kVacancyRetentionDays = 30;

struct User {
    std::string username;
    std::string password;
    std::string role;
};

struct Goods {
    int id = 0;
    std::string name;
    std::string location;
    std::string status;
    std::time_t storedAt = 0;
    std::time_t takenAt = 0;
    std::string operatorName;
};

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::mutex g_dbMutex;                    // protects DB calls AND g_sessions
MYSQL* g_db = nullptr;                   // MySQL connection handle
std::map<std::string, User> g_sessions;  // in-memory sessions (unchanged)
std::atomic<int> g_tokenSeed{1000};

void closeSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string normalizeField(std::string value) {
    for (char& c : value) {
        if (c == '\t' || c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return trim(value);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c; break;
        }
    }
    return out.str();
}

std::string jsonString(const std::string& value) {
    return "\"" + escapeJson(value) + "\"";
}

std::string timeToString(std::time_t value) {
    if (value == 0) {
        return "";
    }
    std::tm tmValue{};
#ifdef _WIN32
    localtime_s(&tmValue, &value);
#else
    localtime_r(&value, &tmValue);
#endif
    std::ostringstream out;
    out << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string getJsonString(const std::string& body, const std::string& key) {
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"((?:\\\\.|[^\"])*)\"");
    std::smatch match;
    if (!std::regex_search(body, match, pattern)) {
        return "";
    }
    std::string value = match[1].str();
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            if (next == 'n') result.push_back('\n');
            else if (next == 'r') result.push_back('\r');
            else if (next == 't') result.push_back('\t');
            else result.push_back(next);
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::string urlDecode(const std::string& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char decoded = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            result.push_back(decoded);
            i += 2;
        } else if (value[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(value[i]);
        }
    }
    return result;
}

std::map<std::string, std::string> parseQuery(const std::string& query) {
    std::map<std::string, std::string> params;
    std::stringstream stream(query);
    std::string part;
    while (std::getline(stream, part, '&')) {
        size_t eq = part.find('=');
        if (eq == std::string::npos) {
            params[urlDecode(part)] = "";
        } else {
            params[urlDecode(part.substr(0, eq))] = urlDecode(part.substr(eq + 1));
        }
    }
    return params;
}

// ── MySQL helpers ──

std::string dbEscape(const std::string& s) {
    if (!g_db) return s;
    std::vector<char> buf(s.size() * 2 + 1);
    mysql_real_escape_string(g_db, buf.data(), s.c_str(), static_cast<unsigned long>(s.size()));
    return std::string(buf.data());
}

std::time_t dbTimeToTimeT(const std::string& datetime) {
    std::tm tm = {};
    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) return 0;
    return std::mktime(&tm);
}

// ── DB lifecycle ──

bool initDb() {
    g_db = mysql_init(nullptr);
    if (!g_db) return false;

    if (!mysql_real_connect(g_db, "localhost", "root", "JBY@ll370079", "first_test",
                            3306, nullptr, 0)) {
        std::cerr << "MySQL connect error: " << mysql_error(g_db) << "\n";
        return false;
    }

    const char* createUsers =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  username VARCHAR(64) NOT NULL UNIQUE,"
        "  password VARCHAR(128) NOT NULL,"
        "  role VARCHAR(32) NOT NULL DEFAULT 'viewer'"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    const char* createGoods =
        "CREATE TABLE IF NOT EXISTS goods ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  name VARCHAR(256) NOT NULL,"
        "  location VARCHAR(256) NOT NULL DEFAULT '默认货架',"
        "  status VARCHAR(32) NOT NULL DEFAULT 'stored',"
        "  stored_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  taken_at DATETIME NULL,"
        "  operator VARCHAR(64) NOT NULL"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(g_db, createUsers) != 0 ||
        mysql_query(g_db, createGoods) != 0) {
        std::cerr << "MySQL create table error: " << mysql_error(g_db) << "\n";
        return false;
    }

    const char* seedAdmin    = "INSERT IGNORE INTO users (username,password,role) VALUES ('admin','admin123','admin')";
    const char* seedManager  = "INSERT IGNORE INTO users (username,password,role) VALUES ('manager','manager123','manager')";
    const char* seedViewer   = "INSERT IGNORE INTO users (username,password,role) VALUES ('viewer','viewer123','viewer')";

    if (mysql_query(g_db, seedAdmin) != 0 ||
        mysql_query(g_db, seedManager) != 0 ||
        mysql_query(g_db, seedViewer) != 0) {
        std::cerr << "MySQL seed error: " << mysql_error(g_db) << "\n";
        return false;
    }

    std::cout << "MySQL connected, tables ready.\n";
    return true;
}

void closeDb() {
    if (g_db) {
        mysql_close(g_db);
        g_db = nullptr;
    }
}

bool roleAtLeast(const User& user, const std::string& required) {
    auto rank = [](const std::string& role) {
        if (role == "admin") return 3;
        if (role == "manager") return 2;
        return 1;
    };
    return rank(user.role) >= rank(required);
}

bool getAuthenticatedUser(const Request& req, User& user) {
    auto it = req.headers.find("authorization");
    if (it == req.headers.end()) {
        return false;
    }
    const std::string prefix = "Bearer ";
    if (it->second.rfind(prefix, 0) != 0) {
        return false;
    }
    std::string token = it->second.substr(prefix.size());
    std::lock_guard<std::mutex> lock(g_dbMutex);
    auto session = g_sessions.find(token);
    if (session == g_sessions.end()) {
        return false;
    }
    user = session->second;
    return true;
}

std::string goodsToJson(const Goods& item) {
    std::ostringstream out;
    out << "{"
        << R"("id":"G)" << std::setw(6) << std::setfill('0') << item.id << "\","
        << "\"name\":" << jsonString(item.name) << ","
        << "\"location\":" << jsonString(item.location) << ","
        << "\"status\":" << jsonString(item.status) << ","
        << "\"storedAt\":" << jsonString(timeToString(item.storedAt)) << ","
        << "\"takenAt\":" << jsonString(timeToString(item.takenAt)) << ","
        << "\"operator\":" << jsonString(item.operatorName)
        << "}";
    return out.str();
}

std::string response(int status, const std::string& contentType, const std::string& body) {
    std::string statusText = "OK";
    if (status == 201) statusText = "Created";
    if (status == 400) statusText = "Bad Request";
    if (status == 401) statusText = "Unauthorized";
    if (status == 403) statusText = "Forbidden";
    if (status == 404) statusText = "Not Found";
    if (status == 405) statusText = "Method Not Allowed";
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << statusText << "\r\n"
        << "Content-Type: " << contentType << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

std::string jsonResponse(int status, const std::string& body) {
    return response(status, "application/json", body);
}

std::string errorResponse(int status, const std::string& message) {
    return jsonResponse(status, "{\"error\":" + jsonString(message) + "}");
}

std::string handleLogin(const Request& req) {
    std::string username = getJsonString(req.body, "username");
    std::string password = getJsonString(req.body, "password");
    if (username.empty() || password.empty()) {
        return errorResponse(400, "用户名和密码不能为空");
    }
    std::lock_guard<std::mutex> lock(g_dbMutex);
    std::string sql = "SELECT username, password, role FROM users WHERE username='"
        + dbEscape(username) + "'";
    if (mysql_query(g_db, sql.c_str()) != 0) {
        return errorResponse(500, "数据库查询失败");
    }
    MYSQL_RES* result = mysql_store_result(g_db);
    if (!result) {
        return errorResponse(500, "数据库查询失败");
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row || std::string(row[1]) != password) {
        mysql_free_result(result);
        return errorResponse(401, "用户名或密码错误");
    }
    std::string dbUser = row[0];
    std::string dbRole = row[2];
    mysql_free_result(result);
    std::string token = username + "-" + std::to_string(std::time(nullptr))
        + "-" + std::to_string(g_tokenSeed++);
    g_sessions[token] = {dbUser, password, dbRole};
    return jsonResponse(200,
        "{\"token\":" + jsonString(token)
        + ",\"role\":" + jsonString(dbRole)
        + ",\"username\":" + jsonString(username) + "}");
}

std::string handleListGoods(const Request& req) {
    User user;
    if (!getAuthenticatedUser(req, user)) {
        return errorResponse(401, "请先登录");
    }
    auto params = parseQuery(req.query);
    std::string name = lower(params["name"]);
    std::string status = params["status"];
    std::lock_guard<std::mutex> lock(g_dbMutex);

    // Cleanup expired taken records
    mysql_query(g_db,
        "DELETE FROM goods WHERE status='taken' AND taken_at IS NOT NULL"
        " AND taken_at < NOW() - INTERVAL 30 DAY");

    // Build SELECT
    std::string sql =
        "SELECT id, name, location, status, stored_at, taken_at, operator"
        " FROM goods WHERE 1=1";
    if (!name.empty()) {
        sql += " AND LOWER(name) LIKE '%" + dbEscape(name) + "%'";
    }
    if (!status.empty()) {
        sql += " AND status='" + dbEscape(status) + "'";
    }
    sql += " ORDER BY id";

    if (mysql_query(g_db, sql.c_str()) != 0) {
        return errorResponse(500, "数据库查询失败");
    }
    MYSQL_RES* result = mysql_store_result(g_db);
    if (!result) {
        return errorResponse(500, "数据库查询失败");
    }
    std::ostringstream out;
    out << "{\"items\":[";
    bool first = true;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        Goods item;
        item.id = row[0] ? std::stoi(row[0]) : 0;
        item.name = row[1] ? row[1] : "";
        item.location = row[2] ? row[2] : "";
        item.status = row[3] ? row[3] : "";
        item.storedAt = (row[4] && row[4][0]) ? dbTimeToTimeT(row[4]) : 0;
        item.takenAt = (row[5] && row[5][0]) ? dbTimeToTimeT(row[5]) : 0;
        item.operatorName = row[6] ? row[6] : "";
        if (!first) out << ",";
        first = false;
        out << goodsToJson(item);
    }
    mysql_free_result(result);
    out << "]}";
    return jsonResponse(200, out.str());
}

std::string handleCreateGoods(const Request& req) {
    User user;
    if (!getAuthenticatedUser(req, user)) {
        return errorResponse(401, "请先登录");
    }
    if (!roleAtLeast(user, "manager")) {
        return errorResponse(403, "当前用户无此权限");
    }
    std::string name = normalizeField(getJsonString(req.body, "name"));
    std::string location = normalizeField(getJsonString(req.body, "location"));
    if (name.empty()) {
        return errorResponse(400, "货物名称不能为空");
    }
    if (location.empty()) {
        location = "默认货架";
    }
    std::lock_guard<std::mutex> lock(g_dbMutex);
    // Cleanup expired
    mysql_query(g_db,
        "DELETE FROM goods WHERE status='taken' AND taken_at IS NOT NULL"
        " AND taken_at < NOW() - INTERVAL 30 DAY");
    // INSERT
    std::string sql = "INSERT INTO goods (name, location, status, stored_at, operator)"
        " VALUES ('" + dbEscape(name) + "','"
        + dbEscape(location) + "','stored',NOW(),'"
        + dbEscape(user.username) + "')";
    if (mysql_query(g_db, sql.c_str()) != 0) {
        return errorResponse(500, "入库失败");
    }
    int newId = static_cast<int>(mysql_insert_id(g_db));
    // Read back full row
    std::string sel = "SELECT id, name, location, status, stored_at, taken_at, operator"
        " FROM goods WHERE id=" + std::to_string(newId);
    mysql_query(g_db, sel.c_str());
    MYSQL_RES* result = mysql_store_result(g_db);
    Goods item;
    if (result) {
        MYSQL_ROW row = mysql_fetch_row(result);
        if (row) {
            item.id = row[0] ? std::stoi(row[0]) : 0;
            item.name = row[1] ? row[1] : "";
            item.location = row[2] ? row[2] : "";
            item.status = row[3] ? row[3] : "";
            item.storedAt = (row[4] && row[4][0]) ? dbTimeToTimeT(row[4]) : 0;
            item.operatorName = row[6] ? row[6] : "";
        }
        mysql_free_result(result);
    }
    return jsonResponse(201, "{\"item\":" + goodsToJson(item) + "}");
}

std::string handleTakeGoods(const Request& req) {
    User user;
    if (!getAuthenticatedUser(req, user)) {
        return errorResponse(401, "请先登录");
    }
    if (!roleAtLeast(user, "manager")) {
        return errorResponse(403, "当前用户无取出权限");
    }
    std::string idText = getJsonString(req.body, "id");
    if (idText.rfind("G", 0) == 0 || idText.rfind("g", 0) == 0) {
        idText = idText.substr(1);
    }
    int id = std::atoi(idText.c_str());
    if (id <= 0) {
        return errorResponse(400, "无效的货物编号");
    }
    std::lock_guard<std::mutex> lock(g_dbMutex);
    // Cleanup expired
    mysql_query(g_db,
        "DELETE FROM goods WHERE status='taken' AND taken_at IS NOT NULL"
        " AND taken_at < NOW() - INTERVAL 30 DAY");
    // Check status
    std::string checkSql = "SELECT status FROM goods WHERE id=" + std::to_string(id);
    if (mysql_query(g_db, checkSql.c_str()) != 0) {
        return errorResponse(500, "数据库查询失败");
    }
    MYSQL_RES* result = mysql_store_result(g_db);
    if (!result) {
        return errorResponse(500, "数据库查询失败");
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return errorResponse(404, "未找到该货物");
    }
    std::string currentStatus = row[0] ? row[0] : "";
    mysql_free_result(result);
    if (currentStatus == "taken") {
        return errorResponse(400, "该货物已取出");
    }
    // Update
    std::string upd = "UPDATE goods SET status='taken', taken_at=NOW(), operator='"
        + dbEscape(user.username) + "' WHERE id=" + std::to_string(id);
    if (mysql_query(g_db, upd.c_str()) != 0) {
        return errorResponse(500, "取出失败");
    }
    // Read back for response
    std::string sel = "SELECT id, name, location, status, stored_at, taken_at, operator"
        " FROM goods WHERE id=" + std::to_string(id);
    mysql_query(g_db, sel.c_str());
    result = mysql_store_result(g_db);
    Goods item;
    if (result) {
        row = mysql_fetch_row(result);
        if (row) {
            item.id = row[0] ? std::stoi(row[0]) : 0;
            item.name = row[1] ? row[1] : "";
            item.location = row[2] ? row[2] : "";
            item.status = row[3] ? row[3] : "";
            item.storedAt = (row[4] && row[4][0]) ? dbTimeToTimeT(row[4]) : 0;
            item.takenAt = (row[5] && row[5][0]) ? dbTimeToTimeT(row[5]) : 0;
            item.operatorName = row[6] ? row[6] : "";
        }
        mysql_free_result(result);
    }
    return jsonResponse(200, "{\"item\":" + goodsToJson(item) + "}");
}

std::string mimeType(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript";
    return "text/plain";
}

std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string serveStatic(const Request& req) {
    std::string path = req.path == "/" ? "/index.html" : req.path;
    if (path.find("..") != std::string::npos) {
        return errorResponse(404, "��Դ������");
    }
    std::string filePath = "public" + path;
    std::string content = readFile(filePath);
    if (content.empty()) {
        return errorResponse(404, "��Դ������");
    }
    return response(200, mimeType(filePath), content);
}

std::string handleRequest(const Request& req) {
    if (req.method == "OPTIONS") {
        return response(200, "text/plain", "");
    }
    if (req.path == "/api/login" && req.method == "POST") {
        return handleLogin(req);
    }
    if (req.path == "/api/goods" && req.method == "GET") {
        return handleListGoods(req);
    }
    if (req.path == "/api/goods" && req.method == "POST") {
        return handleCreateGoods(req);
    }
    if (req.path == "/api/goods/take" && req.method == "POST") {
        return handleTakeGoods(req);
    }
    if (req.path.rfind("/api/", 0) == 0) {
        return errorResponse(404, "�ӿڲ�����");
    }
    if (req.method != "GET") {
        return errorResponse(405, "������֧��");
    }
    return serveStatic(req);
}

bool parseRequest(const std::string& raw, Request& req) {
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }
    std::string headers = raw.substr(0, headerEnd);
    req.body = raw.substr(headerEnd + 4);
    std::stringstream stream(headers);
    std::string line;
    if (!std::getline(stream, line)) {
        return false;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    std::stringstream start(line);
    std::string target;
    start >> req.method >> target;
    size_t q = target.find('?');
    req.path = q == std::string::npos ? target : target.substr(0, q);
    req.query = q == std::string::npos ? "" : target.substr(q + 1);
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }
    return true;
}

void handleClient(Socket client) {
    std::string raw;
    char buffer[4096];
    int received = 0;
    do {
        received = recv(client, buffer, sizeof(buffer), 0);
        if (received > 0) {
            raw.append(buffer, buffer + received);
        }
        size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            size_t contentLength = 0;
            std::string headerText = raw.substr(0, headerEnd);
            std::regex contentLengthPattern("Content-Length:\\s*(\\d+)", std::regex_constants::icase);
            std::smatch match;
            if (std::regex_search(headerText, match, contentLengthPattern)) {
                contentLength = static_cast<size_t>(std::stoul(match[1].str()));
            }
            if (raw.size() >= headerEnd + 4 + contentLength) {
                break;
            }
        }
    } while (received > 0);

    Request req;
    std::string output = parseRequest(raw, req)
        ? handleRequest(req)
        : errorResponse(400, "�����ʽ����");
    send(client, output.c_str(), static_cast<int>(output.size()), 0);
    closeSocket(client);
}

bool initializeSockets() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void cleanupSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

} // namespace

int main() {
    if (!initializeSockets()) {
        std::cerr << "Socket initialization failed\n";
        return 1;
    }
    if (!initDb()) {
        std::cerr << "Database initialization failed\n";
        cleanupSockets();
        return 1;
    }

    Socket server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Cannot create socket\n";
        closeDb();
        cleanupSockets();
        return 1;
    }

    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(kPort);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Cannot bind port " << kPort << "\n";
        closeDb();
        closeSocket(server);
        cleanupSockets();
        return 1;
    }
    if (listen(server, 16) == SOCKET_ERROR) {
        std::cerr << "Cannot listen on port " << kPort << "\n";
        closeDb();
        closeSocket(server);
        cleanupSockets();
        return 1;
    }

    std::cout << "Warehouse server running: http://localhost:" << kPort << "\n";
    while (true) {
        Socket client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }
        std::thread(handleClient, client).detach();
    }
}
