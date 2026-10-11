#include "message.hpp"

#include "socketfd.hpp"

#include <spdlog/spdlog.h>

int sendMessage(const IPCMessage& request)
{
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

    std::string cmd = nlohmann::json(request).dump();

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
