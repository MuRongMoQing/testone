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
#define NOMINMAX
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

namespace {
    constexpr int kPort = 8081;
constexpr int kVacancyRetentionDays = 30;
const char* kDataFile = "warehouse_data.tsv";

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

std::mutex g_mutex;
std::vector<Goods> g_goods;
std::map<std::string, User> g_users = {
    {"admin", {"admin", "admin123", "admin"}},
    {"manager", {"manager", "manager123", "manager"}},
    {"viewer", {"viewer", "viewer123", "viewer"}},
};
std::map<std::string, User> g_sessions;
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

std::vector<std::string> splitTsv(const std::string& line) {
    std::vector<std::string> parts;
    std::stringstream stream(line);
    std::string part;
    while (std::getline(stream, part, '\t')) {
        parts.push_back(part);
    }
    return parts;
}

void saveData() {
    std::ofstream file(kDataFile, std::ios::trunc);
    for (const auto& item : g_goods) {
        file << item.id << '\t'
             << item.name << '\t'
             << item.location << '\t'
             << item.status << '\t'
             << item.storedAt << '\t'
             << item.takenAt << '\t'
             << item.operatorName << '\n';
    }
}

void loadData() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ifstream file(kDataFile);
    std::string line;
    while (std::getline(file, line)) {
        auto parts = splitTsv(line);
        if (parts.size() < 7) {
            continue;
        }
        Goods item;
        item.id = std::stoi(parts[0]);
        item.name = parts[1];
        item.location = parts[2];
        item.status = parts[3];
        item.storedAt = static_cast<std::time_t>(std::stoll(parts[4]));
        item.takenAt = static_cast<std::time_t>(std::stoll(parts[5]));
        item.operatorName = parts[6];
        g_goods.push_back(item);
    }
}

void cleanupExpiredVacanciesLocked() {
    const std::time_t now = std::time(nullptr);
    constexpr std::time_t maxAge = static_cast<std::time_t>(kVacancyRetentionDays) * 24 * 60 * 60;
    const auto oldSize = g_goods.size();
    g_goods.erase(std::remove_if(g_goods.begin(), g_goods.end(), [&](const Goods& item) {
        return item.status == "taken" && item.takenAt > 0 && now - item.takenAt > maxAge;
    }), g_goods.end());
    if (g_goods.size() != oldSize) {
        saveData();
    }
}

int nextGoodsIdLocked() {
    int maxId = 0;
    for (const auto& item : g_goods) {
        maxId = std::max(maxId, item.id);
    }
    return maxId + 1;
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
    std::lock_guard<std::mutex> lock(g_mutex);
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
        << "\"id\":\"G" << std::setw(6) << std::setfill('0') << item.id << "\","
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
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_users.find(username);
    if (it == g_users.end() || it->second.password != password) {
        return errorResponse(401, "用户名或密码错误");
    }
    std::string token = username + "-" + std::to_string(std::time(nullptr)) + "-" + std::to_string(g_tokenSeed++);
    g_sessions[token] = it->second;
    return jsonResponse(200, "{\"token\":" + jsonString(token) + ",\"role\":" + jsonString(it->second.role) + ",\"username\":" + jsonString(username) + "}");
}

std::string handleListGoods(const Request& req) {
    User user;
    if (!getAuthenticatedUser(req, user)) {
        return errorResponse(401, "请先登录");
    }
    auto params = parseQuery(req.query);
    std::string name = lower(params["name"]);
    std::string status = params["status"];
    std::lock_guard<std::mutex> lock(g_mutex);
    cleanupExpiredVacanciesLocked();
    std::ostringstream out;
    out << "{\"items\":[";
    bool first = true;
    for (const auto& item : g_goods) {
        if (!name.empty() && lower(item.name).find(name) == std::string::npos) {
            continue;
        }
        if (!status.empty() && item.status != status) {
            continue;
        }
        if (!first) {
            out << ",";
        }
        first = false;
        out << goodsToJson(item);
    }
    out << "]}";
    return jsonResponse(200, out.str());
}

std::string handleCreateGoods(const Request& req) {
    User user;
    if (!getAuthenticatedUser(req, user)) {
        return errorResponse(401, "请先登录");
    }
    if (!roleAtLeast(user, "manager")) {
        return errorResponse(403, "当前用户无入库权限");
    }
    std::string name = normalizeField(getJsonString(req.body, "name"));
    std::string location = normalizeField(getJsonString(req.body, "location"));
    if (name.empty()) {
        return errorResponse(400, "货物名不能为空");
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    cleanupExpiredVacanciesLocked();
    Goods item;
    item.id = nextGoodsIdLocked();
    item.name = name;
    item.location = location.empty() ? "默认货架" : location;
    item.status = "stored";
    item.storedAt = std::time(nullptr);
    item.operatorName = user.username;
    g_goods.push_back(item);
    saveData();
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
    std::lock_guard<std::mutex> lock(g_mutex);
    cleanupExpiredVacanciesLocked();
    for (auto& item : g_goods) {
        if (item.id == id) {
            if (item.status == "taken") {
                return errorResponse(400, "该货物已取出");
            }
            item.status = "taken";
            item.takenAt = std::time(nullptr);
            item.operatorName = user.username;
            saveData();
            return jsonResponse(200, "{\"item\":" + goodsToJson(item) + "}");
        }
    }
    return errorResponse(404, "未找到该货物");
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
        return errorResponse(404, "资源不存在");
    }
    std::string filePath = "public" + path;
    std::string content = readFile(filePath);
    if (content.empty()) {
        return errorResponse(404, "资源不存在");
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
        return errorResponse(404, "接口不存在");
    }
    if (req.method != "GET") {
        return errorResponse(405, "方法不支持");
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
        : errorResponse(400, "请求格式错误");
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
    loadData();

    Socket server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "Cannot create socket\n";
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
        closeSocket(server);
        cleanupSockets();
        return 1;
    }
    if (listen(server, 16) == SOCKET_ERROR) {
        std::cerr << "Cannot listen on port " << kPort << "\n";
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
