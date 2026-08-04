#include "pipeline/ProcessRunner.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace mvs {
namespace {

std::string joinCommand(const std::vector<std::string>& args) {
  std::string command;
  for (const auto& arg : args) {
    if (!command.empty()) {
      command += " ";
    }
    command += shellQuote(arg);
  }
  return command;
}

double timevalSeconds(const timeval& value) {
  return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_usec) / 1000000.0;
}

long peakResidentSetSizeKb(const rusage& usage) {
#if defined(__APPLE__)
  return usage.ru_maxrss / 1024;
#else
  return usage.ru_maxrss;
#endif
}

void appendTail(std::string& tail, const char* data, std::size_t size) {
  constexpr std::size_t maxTailBytes = 64 * 1024;
  tail.append(data, size);
  if (tail.size() > maxTailBytes) {
    tail.erase(0, tail.size() - maxTailBytes);
  }
}

}  // namespace

std::string shellQuote(const std::string& value) {
  if (value.empty()) {
    return "''";
  }

  std::string quoted = "'";
  for (const char ch : value) {
    if (ch == '\'') {
      quoted += "'\\''";
    } else {
      quoted += ch;
    }
  }
  quoted += "'";
  return quoted;
}

CommandResult runCommand(const std::vector<std::string>& args,
                         const std::filesystem::path& cwd,
                         const std::filesystem::path& logFile) {
  if (args.empty()) {
    throw std::invalid_argument("cannot run an empty command");
  }

  std::filesystem::create_directories(cwd);
  std::filesystem::create_directories(logFile.parent_path());

  std::ofstream log(logFile);
  if (!log) {
    throw std::runtime_error("cannot write command log: " + logFile.string());
  }
  log << "cwd: " << cwd.string() << "\n";
  log << "command: " << joinCommand(args) << "\n";
  log << "stdout_stderr:\n";

  int pipeFds[2];
  if (pipe(pipeFds) != 0) {
    throw std::runtime_error("failed to create command pipe: " + std::string(std::strerror(errno)));
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipeFds[0]);
    close(pipeFds[1]);
    throw std::runtime_error("failed to fork command: " + std::string(std::strerror(errno)));
  }

  if (pid == 0) {
    close(pipeFds[0]);
    dup2(pipeFds[1], STDOUT_FILENO);
    dup2(pipeFds[1], STDERR_FILENO);
    close(pipeFds[1]);
    if (chdir(cwd.c_str()) != 0) {
      _exit(127);
    }

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(pipeFds[1]);
  std::array<char, 4096> buffer{};
  std::string outputTail;
  while (true) {
    const ssize_t readSize = read(pipeFds[0], buffer.data(), buffer.size());
    if (readSize > 0) {
      log.write(buffer.data(), readSize);
      log.flush();
      appendTail(outputTail, buffer.data(), static_cast<std::size_t>(readSize));
      continue;
    }
    if (readSize == 0) {
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    close(pipeFds[0]);
    throw std::runtime_error("failed to read command output: " + std::string(std::strerror(errno)));
  }
  close(pipeFds[0]);

  int status = 0;
  rusage usage{};
  while (wait4(pid, &status, 0, &usage) < 0) {
    if (errno == EINTR) {
      continue;
    }
    throw std::runtime_error("failed to wait for command: " + std::string(std::strerror(errno)));
  }

  int exitCode = status;
  if (WIFEXITED(status)) {
    exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exitCode = 128 + WTERMSIG(status);
  }

  log << "exit_code: " << exitCode << "\n";
  log << "peak_resident_set_size_kb: " << peakResidentSetSizeKb(usage) << "\n";
  log << "user_cpu_seconds: " << timevalSeconds(usage.ru_utime) << "\n";
  log << "system_cpu_seconds: " << timevalSeconds(usage.ru_stime) << "\n";

  CommandResult result;
  result.exitCode = exitCode;
  result.stdoutText = outputTail;
  result.stderrText = "";
  result.peakResidentSetSizeKb = peakResidentSetSizeKb(usage);
  result.userCpuSeconds = timevalSeconds(usage.ru_utime);
  result.systemCpuSeconds = timevalSeconds(usage.ru_stime);
  return result;
}

}  // namespace mvs
