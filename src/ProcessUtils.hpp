#ifndef TEMPER_PROCESS_UTILS_HPP
#define TEMPER_PROCESS_UTILS_HPP

#include <string>
#include <vector>
#include <chrono>

namespace temper {

struct ProcessResult {
    int exitCode;
    std::string stdOut;
    std::string stdErr;
};

/**
 * Executes a command safely without a shell (avoids injection).
 * @param args The command and its arguments.
 * @param timeoutSec Optional timeout in seconds.
 * @return ProcessResult containing exit code and output.
 */
ProcessResult executeSafe(const std::vector<std::string>& args, int timeoutSec = 30);

} // namespace temper

#endif // TEMPER_PROCESS_UTILS_HPP
