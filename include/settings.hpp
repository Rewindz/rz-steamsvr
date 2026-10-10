#pragma once


#include <nlohmann/json.hpp>

constexpr int APPSETTINGS_INDENT = 4;

struct AppSettings
{
    std::string app_id{};
    std::string install_dir{};
    std::string steam_dir{};
    std::string launch_path{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppSettings, app_id, install_dir, steam_dir, launch_path);
