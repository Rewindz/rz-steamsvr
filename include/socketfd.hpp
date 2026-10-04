#pragma once

#include <sys/un.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string_view>
#include <utility>

constexpr std::string_view SOCKET_NAME = "rzsvr";

class SocketFd
{
    public:
        SocketFd() = default;
        explicit SocketFd(int fd)
            : m_fd(fd)
        {}

        ~SocketFd()
        {
            if(m_fd != -1)
                close(m_fd);
        }

        SocketFd(SocketFd&& other) noexcept
            : m_fd(std::exchange(other.m_fd, -1))
        {}
        SocketFd& operator=(SocketFd&& other) noexcept
        {
            if(this != &other) {
                if(m_fd != -1)
                    close(m_fd);
                m_fd = std::exchange(other.m_fd, -1);
            }
            return *this;
        }

        SocketFd(const SocketFd&) = delete;
        SocketFd& operator=(const SocketFd&) = delete;

        inline int get() const { return m_fd; }
        inline bool is_valid() const { return m_fd != -1; }

        inline static auto MakeAddr(std::string_view name) -> std::pair<sockaddr_un, socklen_t>
        {
            sockaddr_un addr{};
            addr.sun_family = AF_UNIX;

            addr.sun_path[0] = '\0';
            name.copy(addr.sun_path + 1, sizeof(addr.sun_path) - 2);
            socklen_t len = offsetof(sockaddr_un, sun_path) + 1 + name.length();
            return {addr, len};
        }

        inline static auto GetSocket()
        {
            return socket(AF_UNIX, SOCK_SEQPACKET, 0);
        }


    private:
        int m_fd{-1};
};
