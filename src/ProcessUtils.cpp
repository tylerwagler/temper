#include "ProcessUtils.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <array>
#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <chrono>

namespace temper {

ProcessResult executeSafe(const std::vector<std::string>& args, int timeoutSec) {
    ProcessResult result = { -1, "", "" };
    
    if (args.empty()) return result;

    int pipe_out[2];
    int pipe_err[2];

    if (pipe(pipe_out) == -1 || pipe(pipe_err) == -1) {
        return result;
    }

    pid_t pid = fork();

    if (pid == -1) {
        close(pipe_out[0]); close(pipe_out[1]);
        close(pipe_err[0]); close(pipe_err[1]);
        return result;
    }

    if (pid == 0) {
        // Child process
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_err[1], STDERR_FILENO);
        close(pipe_out[0]); close(pipe_out[1]);
        close(pipe_err[0]); close(pipe_err[1]);

        // Make copies of strings to ensure they stay valid
        std::vector<std::vector<char>> arg_storage;
        std::vector<char*> c_args;

        for (const auto& arg : args) {
            std::vector<char> arg_copy(arg.begin(), arg.end());
            arg_copy.push_back('\0');
            arg_storage.push_back(std::move(arg_copy));
        }

        for (auto& stored_arg : arg_storage) {
            c_args.push_back(stored_arg.data());
        }
        c_args.push_back(nullptr);

        execvp(c_args[0], c_args.data());
        // If execvp fails
        _exit(127);
    }

    // Parent process
    close(pipe_out[1]);
    close(pipe_err[1]);

    // Set non-blocking
    fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_err[0], F_SETFL, O_NONBLOCK);

    auto start_time = std::chrono::steady_clock::now();
    bool finished = false;

    while (!finished) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(pipe_out[0], &read_fds);
        FD_SET(pipe_err[0], &read_fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(std::max(pipe_out[0], pipe_err[0]) + 1, &read_fds, NULL, NULL, &tv);

        if (ret > 0) {
            std::array<char, 1024> buffer;
            if (FD_ISSET(pipe_out[0], &read_fds)) {
                ssize_t bytes = read(pipe_out[0], buffer.data(), buffer.size());
                if (bytes > 0) result.stdOut.append(buffer.data(), bytes);
            }
            if (FD_ISSET(pipe_err[0], &read_fds)) {
                ssize_t bytes = read(pipe_err[0], buffer.data(), buffer.size());
                if (bytes > 0) result.stdErr.append(buffer.data(), bytes);
            }
        }

        int status;
        pid_t wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result == pid) {
            if (WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
            finished = true;
        } else if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(timeoutSec)) {
            // Timeout
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result.exitCode = -1;
            finished = true;
        }
        
        // Final read after process exit might be needed if process exited but data still in pipe
        if (finished) {
            std::array<char, 1024> buffer;
            ssize_t bytes;
            while ((bytes = read(pipe_out[0], buffer.data(), buffer.size())) > 0) result.stdOut.append(buffer.data(), bytes);
            while ((bytes = read(pipe_err[0], buffer.data(), buffer.size())) > 0) result.stdErr.append(buffer.data(), bytes);
        }
    }

    close(pipe_out[0]);
    close(pipe_err[0]);

    return result;
}

} // namespace temper
