#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>
#include <thread>
#include <stop_token>
#include <atomic>
#include <csignal>

#include <poll.h>

#include <rz/rzutils.hpp>

#include <spdlog/spdlog.h>

#include <lua.hpp>

#include <sys/socket.h>
#include <sys/types.h>

#include "dispatcher.hpp"
#include "settings.hpp"
#include "socketfd.hpp"
#include "ipc.hpp"
#include "luapi.hpp"
#include "spdlog/common.h"

using AppSettingsFile = rz::json::Saveable<AppSettings>;

std::atomic<bool> g_running {true};

void signal_handler(int signal)
{
    if(signal == SIGINT) {
        g_running = false;
    }
}

inline void LoadConfig(AppSettingsFile& config)
{
    bool success = config.Load() == rz::STATUS::RZ_SUCCESS;
    if(!success) {
        spdlog::error("Failed to load config!");
        std::quick_exit(1);
    }
}

int main(void)
{

    std::signal(SIGINT, signal_handler);

    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);


    auto appPath = rz::fs::GetAppConfigPath("rzsvr");
    std::filesystem::path configPath{};
    if(appPath) {
        configPath = *appPath / "config.json";
    } else {
        spdlog::error("App path doesn't exist! Have you initialized with the cli?");
        return 1;
    }

    AppSettingsFile appConfig{configPath, APPSETTINGS_INDENT};
    LoadConfig(appConfig);

    LuaWrapper L{};

    RegisterLuaFunctions(L);

    SocketFd serverFd {SocketFd::GetSocket()};

    if(!serverFd.is_valid()) {
        spdlog::error("Failed to create local socket.");
        return 1;
    }

    auto [addr, addr_len] = SocketFd::MakeAddr(SOCKET_NAME);

    if(bind(serverFd.get(), reinterpret_cast<sockaddr*>(&addr), addr_len) == -1) {
        spdlog::error("Socket bind failed.");
        return 1;
    }

    if(listen(serverFd.get(), 5) == -1) {
        spdlog::error("Socket listen failed.");
        return 1;
    }

    spdlog::info("Daemon listening on socket: \\0{}", SOCKET_NAME);

    IPCDispatcher dispatcher
    {{
        { IPCAction::RUN, [](const auto& args){ return std::unexpected{"TODO"}; } },
        { IPCAction::STOP, [](const auto& args){ return std::unexpected{"TODO"}; } },
        { IPCAction::CMD, [](const auto& args){ return std::unexpected{"TODO"}; } },
    }};

    IPCQueue<IPCRequest> ipcQueue;

    std::jthread net_worker ([&serverFd, &ipcQueue](std::stop_token stoken) {
        pollfd pfd {.fd = serverFd.get(), .events = POLLIN, .revents = 0};
        while(!stoken.stop_requested()) {
            int ret = poll(&pfd, 1, 100);
            if(ret > 0 && (pfd.revents & POLLIN)) {
                SocketFd clientFd {accept(serverFd.get(), nullptr, nullptr)};
                if(!clientFd.is_valid()) continue;
                std::array<char, 1024> buf{};
                ssize_t bytesRead = recv(clientFd.get(), buf.data(), buf.size(), 0);
                if(bytesRead > 0) {
                    ipcQueue.push(IPCRequest{
                       .clientFd {std::move(clientFd)},
                       .request {std::string(buf.data(), static_cast<size_t>(bytesRead))},
                    });
                }
            }
        }
        spdlog::debug("Net worker exited.");
    });

    while(g_running.load(std::memory_order::relaxed))
    {
        while(auto req = ipcQueue.try_pop()) {
            spdlog::debug("Processing queued request: {}", req->request);
            nlohmann::json reply = dispatcher.dispatch(req->request);
            std::string r = reply.dump(APPSETTINGS_INDENT);
            send(req->clientFd.get(), r.data(), r.size(), 0);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    spdlog::info("Shutting down..");
    return 0;
}
