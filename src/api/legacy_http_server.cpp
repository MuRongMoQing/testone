#include "api/legacy_http_server.hpp"

#include "application/legacy/legacy_warehouse_api.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace warehouse::api {
namespace {

using application::legacy::GoodsView;
using application::legacy::LegacyApplicationErrorCode;
using nlohmann::json;

void setJson(httplib::Response& response, int status, const json& value) {
    response.status = status;
    response.set_content(value.dump(-1, ' ', false, json::error_handler_t::replace),
                         "application/json; charset=utf-8");
}

void setError(httplib::Response& response, int status, const char* message) {
    setJson(response, status, json{{"error", message}});
}

std::string formattedGoodsId(std::int64_t id) {
    std::ostringstream output;
    output << 'G' << std::setw(6) << std::setfill('0') << id;
    return output.str();
}

json goodsJson(const GoodsView& goods) {
    return {{"id", formattedGoodsId(goods.numericId)},
            {"name", goods.name},
            {"location", goods.location},
            {"status", goods.status},
            {"storedAt", goods.storedAt},
            {"takenAt", goods.takenAt},
            {"operator", goods.operatorName}};
}

enum class Operation { Login, List, Create, Take };

void setApplicationError(httplib::Response& response, LegacyApplicationErrorCode code,
                         Operation operation) {
    switch (code) {
    case LegacyApplicationErrorCode::InvalidLoginInput:
        return setError(response, 400, "用户名和密码不能为空");
    case LegacyApplicationErrorCode::InvalidCredentials:
        return setError(response, 401, "用户名或密码错误");
    case LegacyApplicationErrorCode::Unauthenticated:
        return setError(response, 401, "请先登录");
    case LegacyApplicationErrorCode::Forbidden:
        return setError(response, 403,
                        operation == Operation::Take ? "当前用户无取出权限"
                                                     : "当前用户无此权限");
    case LegacyApplicationErrorCode::InvalidGoodsName:
        return setError(response, 400, "货物名称不能为空");
    case LegacyApplicationErrorCode::InvalidGoodsId:
        return setError(response, 400, "无效的货物编号");
    case LegacyApplicationErrorCode::PersistenceReadFailed:
        return setError(response, 500, "数据库查询失败");
    case LegacyApplicationErrorCode::PasswordUpgradeFailed:
        return setError(response, 500, "密码哈希升级失败");
    case LegacyApplicationErrorCode::PersistenceCreateFailed:
        return setError(response, 500, "入库失败");
    case LegacyApplicationErrorCode::PersistenceTakeFailed:
        return setError(response, 500, "取出失败");
    case LegacyApplicationErrorCode::GoodsNotFound:
        return setError(response, 404, "未找到该货物");
    case LegacyApplicationErrorCode::GoodsAlreadyTaken:
        return setError(response, 400, "该货物已取出");
    }
    setError(response, 500, "服务器内部错误");
}

bool parseObject(const httplib::Request& request, httplib::Response& response,
                 std::size_t maxBytes, json& output) {
    if (request.body.size() > maxBytes) {
        setError(response, 413, "请求体过大");
        return false;
    }
    try {
        output = json::parse(request.body);
        if (!output.is_object()) {
            setError(response, 400, "请求格式错误");
            return false;
        }
        return true;
    } catch (const json::exception&) {
        setError(response, 400, "请求格式错误");
        return false;
    }
}

bool requiredString(const json& body, const char* key, httplib::Response& response,
                    std::string& value) {
    const auto found = body.find(key);
    if (found == body.end() || !found->is_string()) {
        setError(response, 400, "请求格式错误");
        return false;
    }
    value = found->get<std::string>();
    return true;
}

bool optionalString(const json& body, const char* key, httplib::Response& response,
                    std::string& value) {
    const auto found = body.find(key);
    if (found == body.end()) {
        value.clear();
        return true;
    }
    if (!found->is_string()) {
        setError(response, 400, "请求格式错误");
        return false;
    }
    value = found->get<std::string>();
    return true;
}

bool bearerToken(const httplib::Request& request, httplib::Response& response,
                 std::string& token) {
    constexpr const char prefix[] = "Bearer ";
    const auto authorization = request.get_header_value("Authorization");
    if (authorization.compare(0, sizeof(prefix) - 1, prefix) != 0 ||
        authorization.size() == sizeof(prefix) - 1) {
        setError(response, 401, "请先登录");
        return false;
    }
    token = authorization.substr(sizeof(prefix) - 1);
    return true;
}

}  // namespace

class LegacyHttpServer::Impl {
public:
    Impl(application::legacy::LegacyWarehouseApi& api, HttpServerOptions options)
        : api_(api), options_(std::move(options)) {
        options_.workerThreads = std::max<std::size_t>(1, options_.workerThreads);
        options_.maxQueuedRequests = std::max<std::size_t>(1, options_.maxQueuedRequests);
        options_.maxHeaderBytes = std::max<std::size_t>(1, options_.maxHeaderBytes);
        options_.maxJsonBodyBytes = std::max<std::size_t>(1, options_.maxJsonBodyBytes);
        options_.maxPayloadBytes = std::max(options_.maxJsonBodyBytes, options_.maxPayloadBytes);
        options_.maxAttachmentBytes =
            std::min(options_.maxAttachmentBytes, options_.maxPayloadBytes);
        configureServer();
        registerRoutes();
    }

    bool listen(const std::string& host, int port) { return server_.listen(host, port); }
    bool bind(const std::string& host, int port) { return server_.bind_to_port(host, port); }
    int bindToAnyPort(const std::string& host) { return server_.bind_to_any_port(host); }
    bool listenAfterBind() { return server_.listen_after_bind(); }
    void stop() { server_.stop(); }
    bool isRunning() const { return server_.is_running(); }

private:
    void configureServer() {
        const auto workers = options_.workerThreads;
        const auto queueLimit = options_.maxQueuedRequests;
        server_.new_task_queue = [workers, queueLimit] {
            return new httplib::ThreadPool(workers, queueLimit);
        };
        server_.set_payload_max_length(options_.maxPayloadBytes);
        server_.set_read_timeout(std::max(0, options_.readTimeoutSeconds), 0);
        server_.set_write_timeout(std::max(0, options_.writeTimeoutSeconds), 0);
        server_.set_idle_interval(std::max(0, options_.idleIntervalSeconds), 0);
        server_.set_pre_routing_handler([this](const httplib::Request& request,
                                               httplib::Response& response) {
            std::size_t headerBytes = 0;
            for (const auto& header : request.headers) {
                headerBytes += header.first.size() + header.second.size() + 4;
            }
            if (headerBytes > options_.maxHeaderBytes) {
                setError(response, 431, "请求头过大");
                return httplib::Server::HandlerResponse::Handled;
            }
            if (request.is_multipart_form_data()) {
                for (const auto& entry : request.form.files) {
                    if (entry.second.content.size() > options_.maxAttachmentBytes) {
                        setError(response, 413, "附件过大");
                        return httplib::Server::HandlerResponse::Handled;
                    }
                }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server_.set_post_routing_handler([this](const httplib::Request& request,
                                                httplib::Response& response) {
            applyCors(request, response);
        });
        server_.set_error_handler([](const httplib::Request& request,
                                     httplib::Response& response) {
            if (!response.body.empty()) return;
            if (response.status == 413) return setError(response, 413, "请求体过大");
            if (response.status == 431) return setError(response, 431, "请求头过大");
            if (request.path.rfind("/api/", 0) == 0) {
                return setError(response, 404, "接口不存在");
            }
            if (request.method != "GET") return setError(response, 405, "方法不支持");
            setError(response, 404, "资源不存在");
        });
    }

    void registerRoutes() {
        server_.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& response) {
            response.status = 204;
        });
        server_.Post("/api/login", [this](const httplib::Request& request,
                                          httplib::Response& response) {
            try {
                json body;
                std::string username;
                std::string password;
                if (!parseObject(request, response, options_.maxJsonBodyBytes, body) ||
                    !requiredString(body, "username", response, username) ||
                    !requiredString(body, "password", response, password)) return;
                auto result = api_.login({std::move(username), std::move(password)});
                if (!result) {
                    return setApplicationError(response, result.error().code, Operation::Login);
                }
                const auto& view = result.value();
                setJson(response, 200,
                        {{"token", view.token}, {"role", view.role},
                         {"username", view.username}});
            } catch (...) {
                setError(response, 500, "服务器内部错误");
            }
        });
        server_.Get("/api/goods", [this](const httplib::Request& request,
                                         httplib::Response& response) {
            try {
                std::string token;
                if (!bearerToken(request, response, token)) return;
                const auto name = request.has_param("name") ? request.get_param_value("name") : "";
                const auto status =
                    request.has_param("status") ? request.get_param_value("status") : "";
                auto result = api_.listGoods({{std::move(token)}, name, status});
                if (!result) {
                    return setApplicationError(response, result.error().code, Operation::List);
                }
                json items = json::array();
                for (const auto& goods : result.value()) items.push_back(goodsJson(goods));
                setJson(response, 200, {{"items", std::move(items)}});
            } catch (...) {
                setError(response, 500, "服务器内部错误");
            }
        });
        server_.Post("/api/goods", [this](const httplib::Request& request,
                                          httplib::Response& response) {
            try {
                std::string token;
                if (!bearerToken(request, response, token)) return;
                json body;
                std::string name;
                std::string location;
                if (!parseObject(request, response, options_.maxJsonBodyBytes, body) ||
                    !requiredString(body, "name", response, name) ||
                    !optionalString(body, "location", response, location)) return;
                auto result = api_.createGoods(
                    {{std::move(token)}, std::move(name), std::move(location)});
                if (!result) {
                    return setApplicationError(response, result.error().code, Operation::Create);
                }
                setJson(response, 201, {{"item", goodsJson(result.value())}});
            } catch (...) {
                setError(response, 500, "服务器内部错误");
            }
        });
        server_.Post("/api/goods/take", [this](const httplib::Request& request,
                                               httplib::Response& response) {
            try {
                std::string token;
                if (!bearerToken(request, response, token)) return;
                json body;
                std::string suppliedId;
                if (!parseObject(request, response, options_.maxJsonBodyBytes, body) ||
                    !requiredString(body, "id", response, suppliedId)) return;
                auto result = api_.takeGoods({{std::move(token)}, std::move(suppliedId)});
                if (!result) {
                    return setApplicationError(response, result.error().code, Operation::Take);
                }
                setJson(response, 200, {{"item", goodsJson(result.value())}});
            } catch (...) {
                setError(response, 500, "服务器内部错误");
            }
        });
        server_.Get(R"(/.*)", [this](const httplib::Request& request,
                                      httplib::Response& response) {
            try {
                serveStatic(request, response);
            } catch (...) {
                setError(response, 500, "服务器内部错误");
            }
        });
    }

    void applyCors(const httplib::Request& request, httplib::Response& response) const {
        const auto origin = request.get_header_value("Origin");
        if (origin.empty() ||
            std::find(options_.allowedOrigins.begin(), options_.allowedOrigins.end(), origin) ==
                options_.allowedOrigins.end()) return;
        response.set_header("Access-Control-Allow-Origin", origin);
        response.set_header("Vary", "Origin");
        response.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    }

    void serveStatic(const httplib::Request& request, httplib::Response& response) const {
        if (request.path.rfind("/api/", 0) == 0) {
            return setError(response, 404, "接口不存在");
        }
        std::string relative = request.path == "/" ? "index.html" : request.path.substr(1);
        if (relative.empty() || relative.find('\\') != std::string::npos ||
            relative.find('\0') != std::string::npos) {
            return setError(response, 404, "资源不存在");
        }
        for (const auto& part : std::filesystem::path(relative)) {
            if (part == "." || part == "..") return setError(response, 404, "资源不存在");
        }
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(options_.publicDirectory, error);
        if (error) return setError(response, 404, "资源不存在");
        const auto candidate = std::filesystem::weakly_canonical(root / relative, error);
        if (error) return setError(response, 404, "资源不存在");
        auto rootPart = root.begin();
        auto candidatePart = candidate.begin();
        for (; rootPart != root.end(); ++rootPart, ++candidatePart) {
            if (candidatePart == candidate.end() || *candidatePart != *rootPart) {
                return setError(response, 404, "资源不存在");
            }
        }
        if (!std::filesystem::is_regular_file(candidate, error) || error) {
            return setError(response, 404, "资源不存在");
        }
        std::ifstream input(candidate, std::ios::binary);
        if (!input) return setError(response, 404, "资源不存在");
        std::ostringstream content;
        content << input.rdbuf();
        const auto extension = candidate.extension().string();
        const char* mime = extension == ".html" ? "text/html; charset=utf-8"
                          : extension == ".css" ? "text/css; charset=utf-8"
                          : extension == ".js" ? "application/javascript; charset=utf-8"
                                               : "text/plain; charset=utf-8";
        response.set_content(content.str(), mime);
    }

    application::legacy::LegacyWarehouseApi& api_;
    HttpServerOptions options_;
    httplib::Server server_;
};

LegacyHttpServer::LegacyHttpServer(application::legacy::LegacyWarehouseApi& api,
                                   HttpServerOptions options)
    : impl_(std::make_unique<Impl>(api, std::move(options))) {}
LegacyHttpServer::~LegacyHttpServer() = default;
LegacyHttpServer::LegacyHttpServer(LegacyHttpServer&&) noexcept = default;
LegacyHttpServer& LegacyHttpServer::operator=(LegacyHttpServer&&) noexcept = default;
bool LegacyHttpServer::listen(const std::string& host, int port) {
    return impl_->listen(host, port);
}
bool LegacyHttpServer::bind(const std::string& host, int port) {
    return impl_->bind(host, port);
}
int LegacyHttpServer::bindToAnyPort(const std::string& host) {
    return impl_->bindToAnyPort(host);
}
bool LegacyHttpServer::listenAfterBind() { return impl_->listenAfterBind(); }
void LegacyHttpServer::stop() { impl_->stop(); }
bool LegacyHttpServer::isRunning() const { return impl_->isRunning(); }

}  // namespace warehouse::api
