#include "api/legacy_http_server.hpp"
#include "support/fake_legacy_warehouse_api.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

using json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class RunningServer {
public:
    RunningServer(warehouse::test_support::FakeLegacyWarehouseApi& api,
                  warehouse::api::HttpServerOptions options)
        : server_(api, std::move(options)), port_(server_.bindToAnyPort("127.0.0.1")) {
        require(port_ > 0, "server must bind");
        thread_ = std::thread([this] { server_.listenAfterBind(); });
    }
    ~RunningServer() {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }
    httplib::Client client() const { return httplib::Client("127.0.0.1", port_); }

private:
    warehouse::api::LegacyHttpServer server_;
    int port_;
    std::thread thread_;
};

void exerciseProtocol(const std::filesystem::path& publicDirectory) {
    using warehouse::application::legacy::LegacyApplicationError;
    using warehouse::application::legacy::LegacyApplicationErrorCode;
    warehouse::test_support::FakeLegacyWarehouseApi api;
    api.onLogin = [](warehouse::application::legacy::LoginCommand command) {
        require(command.username == "alice" && command.password == "secret",
                "login command must be mapped");
        return warehouse::test_support::FakeLegacyWarehouseApi::LoginResult::success(
            {"opaque-token", "admin", "alice"});
    };
    api.onListGoods = [](warehouse::application::legacy::ListGoodsQuery query) {
        require(query.auth.value == "opaque-token", "Bearer token must be mapped");
        require(query.name == u8"显示器" && query.status == "stored",
                "filters must be mapped");
        return warehouse::test_support::FakeLegacyWarehouseApi::GoodsListResult::success({
            {7, u8"显示器", "A-01", "stored", "2026-07-19 10:00:00", "", "alice"},
        });
    };
    api.onCreateGoods = [](warehouse::application::legacy::CreateGoodsCommand command) {
        require(command.auth.value == "opaque-token", "create auth must be mapped");
        require(command.name == "keyboard" && command.location == "B-01",
                "create fields must be mapped");
        return warehouse::test_support::FakeLegacyWarehouseApi::GoodsResult::success(
            {8, command.name, command.location, "stored", "2026-07-19 10:30:00", "", "alice"});
    };
    api.onTakeGoods = [](warehouse::application::legacy::TakeGoodsCommand command) {
        require(command.auth.value == "opaque-token" && command.suppliedId == "G000008",
                "take command must be mapped");
        return warehouse::test_support::FakeLegacyWarehouseApi::GoodsResult::success(
            {8, "keyboard", "B-01", "taken", "2026-07-19 10:30:00",
             "2026-07-19 11:00:00", "alice"});
    };

    warehouse::api::HttpServerOptions options;
    options.publicDirectory = publicDirectory;
    options.allowedOrigins = {"https://warehouse.example"};
    RunningServer server(api, options);
    auto client = server.client();

    auto login = client.Post("/api/login", R"({"username":"alice","password":"secret"})",
                             "application/json");
    require(login && login->status == 200, "login must return 200");
    require(json::parse(login->body) ==
                json{{"token", "opaque-token"}, {"role", "admin"}, {"username", "alice"}},
            "login response must preserve legacy fields");

    httplib::Headers auth{{"Authorization", "Bearer opaque-token"}};
    auto list = client.Get("/api/goods?name=%E6%98%BE%E7%A4%BA%E5%99%A8&status=stored", auth);
    require(list && list->status == 200, "list must return 200");
    const auto listed = json::parse(list->body);
    require(listed.at("items").at(0).at("id") == "G000007", "id must be formatted");
    require(listed.at("items").at(0).at("operator") == "alice",
            "operator must be serialized");

    auto created = client.Post("/api/goods", auth,
                               R"({"name":"keyboard","location":"B-01"})",
                               "application/json");
    require(created && created->status == 201, "create must return 201");
    require(json::parse(created->body).at("item").at("id") == "G000008",
            "created item must preserve wrapper");

    auto taken = client.Post("/api/goods/take", auth, R"({"id":"G000008"})",
                             "application/json");
    require(taken && taken->status == 200, "take must return 200");
    require(json::parse(taken->body).at("item").at("status") == "taken",
            "take response must preserve item");

    auto invalid = client.Post("/api/login", R"({"username":1})", "application/json");
    if (!invalid || invalid->status != 400) {
        throw std::runtime_error(
            "strict JSON types must be rejected; status=" +
            std::to_string(invalid ? invalid->status : 0) +
            "; body=" + (invalid ? invalid->body : "<no response>"));
    }

    auto unknown = client.Get("/api/not-real");
    require(unknown && unknown->status == 404 &&
                json::parse(unknown->body).at("error") == u8"接口不存在",
            "unknown legacy API must use legacy error shape");

    auto index = client.Get("/");
    require(index && index->status == 200 && index->body == "warehouse",
            "static index must be served");
    auto traversal = client.Get("/%2e%2e/CMakeLists.txt");
    require(traversal && traversal->status == 404, "static traversal must be rejected");

    httplib::Headers allowed{{"Origin", "https://warehouse.example"}};
    auto preflight = client.Options("/api/goods", allowed);
    require(preflight && preflight->status == 204 &&
                preflight->get_header_value("Access-Control-Allow-Origin") ==
                    "https://warehouse.example",
            "allowed exact origin must receive CORS headers");
    httplib::Headers denied{{"Origin", "https://attacker.example"}};
    auto deniedPreflight = client.Options("/api/goods", denied);
    require(deniedPreflight &&
                deniedPreflight->get_header_value("Access-Control-Allow-Origin").empty(),
            "unlisted origin must not receive CORS headers");

    api.onListGoods = [](warehouse::application::legacy::ListGoodsQuery) {
        return warehouse::test_support::FakeLegacyWarehouseApi::GoodsListResult::failure(
            LegacyApplicationError{LegacyApplicationErrorCode::Unauthenticated});
    };
    auto unauthorized = client.Get("/api/goods", auth);
    require(unauthorized && unauthorized->status == 401,
            "application errors must map to legacy status codes");
}

}  // namespace

int main() {
    const auto publicDirectory =
        std::filesystem::temp_directory_path() / "warehouse-api-foundation-test-public";
    std::error_code cleanupError;
    std::filesystem::remove_all(publicDirectory, cleanupError);
    std::filesystem::create_directories(publicDirectory);
    std::ofstream(publicDirectory / "index.html", std::ios::binary) << "warehouse";
    try {
        exerciseProtocol(publicDirectory);
        std::filesystem::remove_all(publicDirectory, cleanupError);
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(publicDirectory, cleanupError);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
