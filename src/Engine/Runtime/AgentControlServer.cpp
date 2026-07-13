#if WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/AgentControlServer.hpp"

#include "Engine/Runtime/Platform/PlatformCommon.hpp"

#include <future>
#if !WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Runtime::Agent
{
    namespace
    {
#if WIN32
        using Socket = SOCKET;
        constexpr Socket invalidSocket = INVALID_SOCKET;
        void CloseSocket(Socket socket) { if (socket != invalidSocket) closesocket(socket); }
#else
        using Socket = int;
        constexpr Socket invalidSocket = -1;
        void CloseSocket(Socket socket) { if (socket != invalidSocket) close(socket); }
#endif
    }

    struct FAgentControlServer::FImpl
    {
        struct FRequest { nlohmann::json frame; std::promise<nlohmann::json> promise; };
        Socket listener = invalidSocket;
        Socket client = invalidSocket;
        std::string token;
        std::jthread thread;
        std::atomic<bool> running = false;
        std::mutex mutex;
        std::vector<std::shared_ptr<FRequest>> requests;

        void Run(std::stop_token stopToken)
        {
            sockaddr_in address{};
#if WIN32
            int length = sizeof(address);
#else
            socklen_t length = sizeof(address);
#endif
            client = accept(listener, reinterpret_cast<sockaddr*>(&address), &length);
            if (client == invalidSocket) { running = false; return; }
            std::string line;
            char value = 0;
            while (!stopToken.stop_requested() && running)
            {
                const int count = recv(client, &value, 1, 0);
                if (count <= 0) break;
                if (value != '\n') { if (value != '\r' && line.size() < 4 * 1024 * 1024) line.push_back(value); continue; }
                try
                {
                    auto request = std::make_shared<FRequest>(); request->frame = nlohmann::json::parse(line); line.clear();
                    nlohmann::json response{{"id", request->frame.value("id", "")}};
                    if (request->frame.value("token", "") != token)
                    {
                        response["error"] = "unauthorized";
                    }
                    else
                    {
                        auto future = request->promise.get_future();
                        { std::lock_guard lock(mutex); requests.push_back(request); }
                        response = future.get(); response["id"] = request->frame.value("id", "");
                    }
                    const std::string encoded = response.dump() + "\n";
                    send(client, encoded.data(), static_cast<int>(encoded.size()), 0);
                }
                catch (const std::exception& exception)
                {
                    const std::string encoded = nlohmann::json{{"error", exception.what()}}.dump() + "\n";
                    send(client, encoded.data(), static_cast<int>(encoded.size()), 0); line.clear();
                }
            }
            running = false;
        }
    };

    FAgentControlServer::FAgentControlServer() : impl_(std::make_unique<FImpl>()) {}
    FAgentControlServer::~FAgentControlServer() { Stop(); }
    bool FAgentControlServer::Start(const std::string& endpoint, std::string token, std::string& error)
    {
        Stop();
        const size_t separator = endpoint.rfind(':');
        if (separator == std::string::npos) { error = "agent control endpoint must be host:port"; return false; }
        const std::string host = endpoint.substr(0, separator);
        const int port = std::stoi(endpoint.substr(separator + 1));
#if WIN32
        WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { error = "WSAStartup failed"; return false; }
#endif
        impl_->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (impl_->listener == invalidSocket) { error = "socket failed"; return false; }
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
            bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(impl_->listener, 1) != 0)
        { error = "bind/listen failed"; Stop(); return false; }
        impl_->token = std::move(token); impl_->running = true;
        impl_->thread = std::jthread([this](std::stop_token token) { impl_->Run(token); });
        return true;
    }
    void FAgentControlServer::Stop()
    {
        if (!impl_) return; impl_->running = false; if (impl_->thread.joinable()) impl_->thread.request_stop();
        CloseSocket(impl_->client); impl_->client = invalidSocket; CloseSocket(impl_->listener); impl_->listener = invalidSocket;
        if (impl_->thread.joinable()) impl_->thread.join();
#if WIN32
        WSACleanup();
#endif
    }
    void FAgentControlServer::Pump(const Handler& handler)
    {
        std::vector<std::shared_ptr<FImpl::FRequest>> requests;
        { std::lock_guard lock(impl_->mutex); requests.swap(impl_->requests); }
        for (auto& request : requests)
        {
            try
            {
                const auto result = handler(request->frame.value("method", ""), request->frame.value("params", nlohmann::json::object()));
                request->promise.set_value({{"result", result}});
            }
            catch (const std::exception& exception) { request->promise.set_value({{"error", exception.what()}}); }
        }
    }
    bool FAgentControlServer::IsRunning() const { return impl_->running; }
}
