#pragma once

#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <fcntl.h>

#include <utility>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <memory>


class ChildProcess
{
    public:

        ChildProcess() = default;

        ~ChildProcess()
        {
            terminate();
            if (m_stdin_fd != -1) close(m_stdin_fd);
            if (m_stdout_fd != -1) close(m_stdout_fd);
        }

        ChildProcess(const ChildProcess&) = delete;
        ChildProcess& operator=(const ChildProcess&) = delete;

        ChildProcess(ChildProcess&& other) noexcept
            : m_pid(std::exchange(other.m_pid, -1)),
              m_stdin_fd(std::exchange(other.m_stdin_fd, -1)),
              m_stdout_fd(std::exchange(other.m_stdout_fd, -1))
        {}

        ChildProcess& operator=(ChildProcess&& other) noexcept
        {
            if(this != &other) {
                if(m_stdin_fd != -1) close(m_stdin_fd);
                if(m_stdout_fd != -1) close (m_stdout_fd);
                m_pid = std::exchange(other.m_pid, -1);
                m_stdin_fd = std::exchange(other.m_stdout_fd, -1);
                m_stdout_fd = std::exchange(other.m_stdout_fd, -1);
            }
            return *this;
        }

        bool spawn(const std::string& executable, const std::vector<std::string>& args)
        {
            m_exec = executable;
            m_args = args;
            int pipeIn[2];
            int pipeOut[2];
            if(pipe(pipeIn) < 0 || pipe(pipeOut) < 0)
                return false;

            m_pid = fork();
            if(m_pid == -1) {
                return false;
            } else if (m_pid == 0) {
                close(pipeIn[1]);
                close(pipeOut[0]);

                dup2(pipeIn[0], STDIN_FILENO);
                dup2(pipeOut[1], STDOUT_FILENO);
                dup2(pipeOut[1], STDERR_FILENO);

                close(pipeIn[0]);
                close(pipeOut[1]);

                std::vector<char*> c_args;
                c_args.push_back(const_cast<char*>(executable.c_str()));
                for(const auto& arg : args)
                    c_args.push_back(const_cast<char*>(arg.c_str()));
                c_args.push_back(nullptr);

                execv(executable.c_str(), c_args.data());

                std::quick_exit(1);
            } else {
                close(pipeIn[0]);
                close(pipeOut[1]);
                m_stdin_fd = pipeIn[1];
                m_stdout_fd = pipeOut[0];
                set_nonblocking(m_stdin_fd);
                set_nonblocking(m_stdout_fd);
                return true;
            }
        }

        std::string readOutput()
        {
            if (m_stdout_fd == -1) return "";
            std::array<char, 4096> buf{};
            ssize_t bytes = read(m_stdout_fd, buf.data(), buf.size());
            if(bytes > 0)
                return std::string(buf.data(), static_cast<size_t>(bytes));
            return "";
        }

        void writeInput(std::string_view cmd)
        {
            if(m_stdin_fd != -1) {
                write(m_stdin_fd, cmd.data(), cmd.size());
                write(m_stdin_fd, "\n", 1);
            }
        }

        bool isRunning()
        {
            if(m_pid <= 0) return false;
            int status;
            pid_t result = waitpid(m_pid, &status, WNOHANG);
            if (result == 0) return true;
            m_pid = -1;
            return false;
        }

        void terminate()
        {
            if (m_pid > 0) {
                kill(m_pid, SIGTERM);
                int status{0};
                waitpid(m_pid, &status, 0);
                m_pid = -1;
            }
        }

        auto getExectuable() const
        {
            return m_exec;
        }

        auto getArgs() const
        {
            return m_args;
        }

    private:

        pid_t m_pid {-1};
        int m_stdin_fd {-1};
        int m_stdout_fd {-1};

        std::string m_exec{};
        std::vector<std::string> m_args{};

        static void set_nonblocking(int fd)
        {
            int flags = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
};


class ProcessManager
{
    public:

        static ProcessManager& GetInstance()
        {
            static ProcessManager res{};
            return res;
        }

        ChildProcess* operator->()
        {
            return m_process.get();
        }

        ChildProcess* GetProcess()
        {
            return m_process.get();
        }

        ChildProcess* ResetProcess()
        {
            m_process = std::make_unique<ChildProcess>();
            return m_process.get();
        }

    private:

        std::unique_ptr<ChildProcess> m_process;

        ProcessManager()
            : m_process(std::make_unique<ChildProcess>())
        {};

};
