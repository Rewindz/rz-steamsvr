#include <chrono>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <string>
#include <thread>
#include <stop_token>
#include <atomic>
#include <csignal>
#include <unistd.h>

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
#include "process.hpp"

using AppSettingsFile = rz::json::Saveable<AppSettings>;

std::atomic<bool> g_running {true};

void signal_handler(int signal)
{
    if(signal == SIGINT) {
        g_running.store(false, std::memory_order::relaxed);
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
    std::signal(SIGPIPE, SIG_IGN);

    spdlog::set_pattern("[%x] [%X] [%^%l%$] %v");
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

    unlink(std::string(SOCKET_NAME).c_str());

    if(bind(serverFd.get(), reinterpret_cast<sockaddr*>(&addr), addr_len) == -1) {
        spdlog::error("Socket bind failed.");
        return 1;
    }

    if(listen(serverFd.get(), 5) == -1) {
        spdlog::error("Socket listen failed.");
        return 1;
    }

    spdlog::info("Daemon listening on socket: \\0{}", SOCKET_NAME);

    auto& process = ProcessManager::GetInstance();

    auto runAction = [&process, &appConfig](const nlohmann::json& args) -> IPCDispatcher::HandlerReturn {
        // Spawning proccess args
        if(appConfig->launch_path.empty()) {
            return std::unexpected{"App launch path not set."};
        }

        bool force {args.value("restart", false)};

        if(process->isRunning() && !force) {
            return std::unexpected{"App is already running! Please stop first or use the restart flag."};
        }

        if(!args.contains("args") || !args["args"].is_array()) {
            return std::unexpected{"Invalid JSON args."};
        }

        using std::filesystem::path;
        path l{appConfig->launch_path};
        bool res{false};
        auto cmdArgs = args["args"].get<std::vector<std::string>>();
        if(appConfig->install_dir.empty()) {
            if(auto home = rz::fs::GetHomePath(); home.has_value()) {
                res = process->spawn( (*home / "Steam" / "steamapps" / "common" / l).string(), cmdArgs );
            } else {
                return std::unexpected{"Failed to get home directory env variable."};
            }
        } else {
            res = process->spawn((l / appConfig->launch_path).string(), cmdArgs);
        }

        if(res) {
            return args;
        } else {
            return std::unexpected{"Failed to start process"};
        }
    };

    auto stopAction = [&process](const auto& args) -> IPCDispatcher::HandlerReturn {
        if(process->isRunning()) {
            process->terminate();
            return args;
        }
        return std::unexpected{"Process is not running."};
    };

    auto statusAction = [&process](const auto& args) -> IPCDispatcher::HandlerReturn {
        nlohmann::json res = nlohmann::json::object();
        res["running"] = process->isRunning();
        res["executable"] = process->getExectuable();
        res["args"] = process->getArgs();
        return res;
    };

    auto cmdAction = [&L](const nlohmann::json& args) -> IPCDispatcher::HandlerReturn {
        if(!args.contains("func") || !args["func"].is_string()
        || !args.contains("args") || !args["args"].is_array())
        {
            return std::unexpected{"Invalid JSON action format!"};
        }

        int oldTop = lua_gettop(L);

        std::string func = args["func"];

        // check if function exists
        if(lua_getglobal(L, func.c_str()) != LUA_TFUNCTION) {
            lua_settop(L, oldTop);
            return std::unexpected{std::format("Lua function '{}' not found or is not a function", func)};
        }

        // push args to lua stack
        for(auto& arg : args["args"]) {
            if(arg.is_string()) {
                lua_pushstring(L, arg.get<std::string>().c_str());
            } else if (arg.is_boolean()) {
                lua_pushboolean(L, arg.get<bool>());
            } else if (arg.is_number()) {
                lua_pushnumber(L, arg.get<lua_Number>());
            } else {
                lua_settop(L, oldTop);
                return std::unexpected{"Failed to parse JSON arg to a Lua equivalent"};
            }
        }

        if(lua_pcall(L, args["args"].size(), LUA_MULTRET, 0) != LUA_OK) {
            std::string err{lua_tostring(L, -1)};
            lua_settop(L, oldTop);
            return std::unexpected{std::format("Lua execution failed: {}", err)};
        }

        nlohmann::json res = nlohmann::json::object();
        nlohmann::json returns = nlohmann::json::array();

        int numReturns = lua_gettop(L) - oldTop;

        for (int i = 1; i <= numReturns; i++) {
            int idx = oldTop + i;
            if (lua_isstring(L, idx)) {
                returns.push_back(lua_tostring(L, idx));
            } else if (lua_isboolean(L, idx)) {
                returns.push_back(static_cast<bool>(lua_toboolean(L, idx)));
            } else if (lua_isnumber(L, idx)) {
                returns.push_back(lua_tonumber(L, idx));
            } else if (lua_isnil(L, idx) || lua_isnone(L, idx)) {
                returns.push_back(nullptr);
            } else {
                returns.push_back("Unsupported return type");
            }
        }
        res["result"] = returns;
        lua_settop(L, oldTop);
        return res;
    };

    IPCDispatcher dispatcher
    {{
        { IPCAction::RUN, runAction },
        { IPCAction::STOP, stopAction },
        { IPCAction::STATUS, statusAction },
        { IPCAction::CMD, cmdAction },
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
