#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace warehouse::application::legacy {
class LegacyWarehouseApi;
}

namespace warehouse::api {

struct HttpServerOptions {
    std::filesystem::path publicDirectory = "public";
    std::vector<std::string> allowedOrigins;
    std::size_t workerThreads = 1;
    std::size_t maxQueuedRequests = 16;
    std::size_t maxHeaderBytes = 16 * 1024;
    std::size_t maxJsonBodyBytes = 1024 * 1024;
    std::size_t maxPayloadBytes = 21 * 1024 * 1024;
    std::size_t maxAttachmentBytes = 20 * 1024 * 1024;
    int readTimeoutSeconds = 15;
    int writeTimeoutSeconds = 15;
    int idleIntervalSeconds = 1;
};

class LegacyHttpServer final {
public:
    LegacyHttpServer(application::legacy::LegacyWarehouseApi& api, HttpServerOptions options);
    ~LegacyHttpServer();

    LegacyHttpServer(const LegacyHttpServer&) = delete;
    LegacyHttpServer& operator=(const LegacyHttpServer&) = delete;
    LegacyHttpServer(LegacyHttpServer&&) noexcept;
    LegacyHttpServer& operator=(LegacyHttpServer&&) noexcept;

    bool listen(const std::string& host, int port);
    bool bind(const std::string& host, int port);
    int bindToAnyPort(const std::string& host);
    bool listenAfterBind();
    void stop();
    bool isRunning() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace warehouse::api
