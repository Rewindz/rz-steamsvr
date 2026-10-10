
#include <exception>
#include <iostream>

#include <spdlog/spdlog.h>

#include <argparse/argparse.hpp>

#include <rz/rzutils.hpp>

#include "rz/defs/defs.hpp"
#include "settings.hpp"
#include "steamcmd.hpp"
#include "ipc.hpp"
#include "socketfd.hpp"

int main(int argc, char** argv)
{
    argparse::ArgumentParser program("rzsvr", "1.0.0");

    spdlog::set_pattern("[%^%l%$] %v");

    spdlog::set_level(spdlog::level::debug);


    program.add_argument("-i", "--init")
        .flag()
        .help("initialize the configuration");

    program.add_argument("--launch-path")
        .flag()
        .help("set the app's launch path (relative to the install dir)");

    program.add_argument("--config")
        .flag()
        .help("print out the path for this programs config file");

    program.add_argument("-u", "--update")
        .flag()
        .help("update/install the app (with appid from the config)");

    program.add_argument("--stop")
        .flag()
        .help("stop the app");

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& e) {
        spdlog::error("{}\n{}", e.what(), program.help().str());
        return 1;
    }

    auto appPath = rz::fs::GetAppConfigPath("rzsvr");
    std::filesystem::path configPath{};
    if(appPath) {
        configPath = *appPath / "config.json";
    } else {
        spdlog::error("Failed to get app config path!");
        return 1;
    }
    rz::json::Saveable<AppSettings> appSettings{configPath, APPSETTINGS_INDENT};

    /* ------------------------------------- BEFORE LOADING CONFIG ------------------------------------- */

    if(program["--init"] == true) {
        std::printf("Steam Server AppID: ");
        std::getline(std::cin, appSettings->app_id);
        std::printf("Directory to force install app (leave empty for default install directory): ");
        std::getline(std::cin, appSettings->install_dir);
        std::printf("Directory to install SteamCMD (leave empty for default): ");
        std::getline(std::cin, appSettings->steam_dir);

        if(appSettings->steam_dir.empty()) {
            auto homePath = rz::fs::GetHomePath();
            if(homePath) {
                std::filesystem::path steamPath = *homePath / "steamcmd";
                appSettings->steam_dir = steamPath.string();
            } else {
                spdlog::error("Failed to get home directory path.");
                return 1;
            }
        }

        spdlog::info("AppID: {}\tInstall Dir: {}\tSteam Dir: {}", appSettings->app_id,
            (appSettings->install_dir.empty() ? "default" : appSettings->install_dir),
            appSettings->steam_dir);

        std::printf("Confirm? [y/n]: ");
        std::string confirm{};
        std::getline(std::cin, confirm);
        if(!confirm.starts_with('y')) {
            spdlog::info("Canceling initialization....");
            return 1;
        }

        auto res = appSettings.Save();
        if(res == rz::STATUS::RZ_ERROR) {
            spdlog::error("Failed to save config! {}", configPath.string());
            return 1;
        }

        spdlog::info("Saved config to: {}", configPath.string());

        SteamCmdManager steam{appSettings->steam_dir};
        if(steam.isInstalled()) {
            spdlog::warn("steamcmd is already installed at {}! skipping steamcmd install process..", appSettings->steam_dir);
        } else {
            spdlog::info("Downloading steamcmd....");
            if(!steam.setup()) {
                spdlog::error("Failed to download/initialize steamcmd...");
                return 1;
            }
            spdlog::info("steamcmd initialization success! Install/update the server app with --update");
        }

        spdlog::warn("Please remember to set the launch path if not using custom lua actions.");
        spdlog::warn("Use --launch-path to set the launch path");

        return 0;
    }

    if(!rz::fs::ExistsAndIsRegularFile(configPath)) {
        spdlog::warn("rzsvr config does not exist! Create with {} --init\n{}", argv[0], program.help().str());
        return 1;
    }

    {
        auto res = appSettings.Load();
        if(res == rz::STATUS::RZ_ERROR) {
            spdlog::error("Failed to load config! {}", configPath.string());
            return 1;
        }
    }

    /* ------------------------------------- AFTER LOADING CONFIG ------------------------------------- */

    if(program["--config"] == true) {
        spdlog::info("rzsvr config path:\n{}", configPath.string());
    }

    if(program["--update"] == true) {
        SteamCmdManager steam{appSettings->steam_dir};

        if(!steam.isInstalled()) {
            spdlog::error("steamcmd not install at directory {}!", appSettings->steam_dir);
            return 1;
        }

        bool success{false};
        if(appSettings->install_dir.empty())
            success = steam.updateApp(appSettings->app_id);
        else{
            std::filesystem::path install = appSettings->install_dir;
            success = steam.updateApp(appSettings->app_id, install);
        }

        if(success) {
            spdlog::info("Successfully updated {}!", appSettings->app_id);
            return 0;
        } else {
            spdlog::error("Failed to update {}!", appSettings->app_id);
            return 1;
        }
    }

    if(program["--launch-path"] == true) {
        std::printf("Launch path: ");
        std::getline(std::cin, appSettings->launch_path);
        using std::filesystem::path;
        path i{appSettings->install_dir};
        path l{appSettings->launch_path};
        spdlog::info("Launch path: {}", (i / l).string());

        if(appSettings.Save() == rz::STATUS::RZ_ERROR) {
            spdlog::error("Failed to save config with new launch path");
            return 1;
        } else {
            spdlog::info("Launch path saved successfully");
            return 0;
        }
    }

    if(program["--stop"] == true) {
        SocketFd client {SocketFd::GetSocket()};
        if(!client.is_valid()) {
            spdlog::error("Failed to create socket for IPC");
            return 1;
        }
        auto [addr, addr_len] = SocketFd::MakeAddr(SOCKET_NAME);

        if(connect(client.get(), reinterpret_cast<sockaddr*>(&addr), addr_len) == -1) {
            spdlog::error("Failed to connect. Is the daemon running?");
            return 1;
        }

        IPCMessage msg {
          .action = IPCAction::STOP,
        };
        std::string cmd = nlohmann::json(msg).dump();

        if(send(client.get(), cmd.data(), cmd.size(), 0) == -1) {
            spdlog::error("Failed to send command!");
            spdlog::debug("Failed command: {}", cmd);
            return 1;
        }

        // read reply
        std::array<char, 1024> buffer;
        ssize_t bytesRead = recv(client.get(), buffer.data(), buffer.size(), 0);
        if(bytesRead <= 0) {
            spdlog::error("Daemon did not reply.");
            return 1;
        }

        try {
            std::string_view bufView{buffer.data(), static_cast<size_t>(bytesRead)};
            IPCReply reply = nlohmann::json::parse(bufView).get<IPCReply>();
            if(reply.status == IPCStatus::ERROR) {
                spdlog::error("Daemon returned an error:\n {}", reply.data.dump(4));
                return 1;
            } else {
                spdlog::info("Success!");
                spdlog::debug("Daemon returned:\n {}", reply.data.dump(4));
                return 0;
            }
        } catch (const nlohmann::json::exception& e) {
            spdlog::error("Failed to parse reply: {}", e.what());
            return 1;
        }
    }

    return 0;
}
