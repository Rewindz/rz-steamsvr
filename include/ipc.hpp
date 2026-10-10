#pragma once

#include <nlohmann/json.hpp>

/*
 *
 * The arguments for the actions are as follows
 *   Stop: No args
 *    Run: Process launch arguments as a JSON array and a force restart flag { "args": ["a", "b", "c"], "restart": true }
 * Status: No args
 *    CMD: The Lua function name to call and its optional args { "func": "print", "args": ["a", 2, "c" ] }
 *
 */

enum class IPCAction
{
    STOP,
    RUN,
    STATUS,
    CMD,
    INVALID=-1,
};

NLOHMANN_JSON_SERIALIZE_ENUM(IPCAction, {
    {IPCAction::INVALID, nullptr},
    {IPCAction::STOP, "stop"},
    {IPCAction::RUN, "run"},
    {IPCAction::STATUS, "status"},
    {IPCAction::CMD, "cmd"},
});

struct IPCMessage
{
    IPCAction action{IPCAction::INVALID};
    nlohmann::json args{nlohmann::json::object()};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(IPCMessage, action, args);

enum class IPCStatus
{
    ERROR,
    SUCCESS
};

NLOHMANN_JSON_SERIALIZE_ENUM(IPCStatus, {
   {IPCStatus::ERROR, "error"},
   {IPCStatus::SUCCESS, "success"},
});

struct IPCReply
{
    IPCStatus status;
    nlohmann::json data;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(IPCReply, status, data);
