#pragma once

#include <functional>
#include <unordered_map>
#include <string_view>
#include <format>
#include <expected>
#include <queue>
#include <mutex>
#include <utility>
#include <optional>

#include <nlohmann/json.hpp>

#include "ipc.hpp"
#include "socketfd.hpp"

class IPCDispatcher
{
    public:
        using HandlerReturn = std::expected<nlohmann::json, std::string>;
        using Handler = std::function<HandlerReturn(const nlohmann::json&)>;
        using RouteMap = std::unordered_map<IPCAction, Handler>;

        IPCDispatcher(RouteMap&& routes)
            : m_routes(std::move(routes))
        {}

        IPCReply dispatch(std::string_view payload) {
            try {

                auto j = nlohmann::json::parse(payload);
                auto msg = j.get<IPCMessage>();

                if(msg.action == IPCAction::INVALID) {
                    return make_error("Invalid, unknown, or missing action");
                }

                if(auto it = m_routes.find(msg.action); it != m_routes.end()) {
                    auto result = it->second(msg.args);
                    if(result.has_value()) {
                        return make_success(result.value());
                    } else {
                        return make_error(result.error());
                    }
                }

                return make_error("Action has no handler");

            } catch (const nlohmann::json::exception& e) {
                return make_error(std::format("JSON error: {}", e.what()));
            }
        }

    private:

        IPCReply make_success(const nlohmann::json& data)
        {
            IPCReply reply {
                .status = IPCStatus::SUCCESS,
                .data = data
            };
            return reply;
        }

        IPCReply make_error(const nlohmann::json& data)
        {
            IPCReply reply {
                .status = IPCStatus::ERROR,
                .data = data
            };
            return reply;
        }


         RouteMap m_routes;
};

struct IPCRequest
{
    SocketFd clientFd;
    std::string request;
};

template <typename T>
class IPCQueue
{
    public:

        void push(T item)
        {
            std::scoped_lock lock(m_mutex);
            m_queue.push(std::move(item));
        }

        std::optional<T> try_pop()
        {
            std::scoped_lock lock(m_mutex);
            if(m_queue.empty())
                return std::nullopt;
            T item = std::move(m_queue.front());
            m_queue.pop();
            return item;
        }

    private:
        std::queue<T> m_queue;
        mutable std::mutex m_mutex;
};
