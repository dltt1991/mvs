#include "pipeline/ProcessRunner.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
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

  const auto command = "cd " + shellQuote(cwd.string()) + " && " + joinCommand(args) + " 2>&1";
  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    throw std::runtime_error("failed to start command: " + joinCommand(args));
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  int exitCode = status;
  if (WIFEXITED(status)) {
    exitCode = WEXITSTATUS(status);
  }

  std::ofstream log(logFile);
  if (!log) {
    throw std::runtime_error("cannot write command log: " + logFile.string());
  }
  log << "cwd: " << cwd.string() << "\n";
  log << "command: " << joinCommand(args) << "\n";
  log << "exit_code: " << exitCode << "\n";
  log << "stdout:\n" << output << "\n";
  log << "stderr:\n";

  CommandResult result;
  result.exitCode = exitCode;
  result.stdoutText = output;
  result.stderrText = "";
  return result;
}

}  // namespace mvs
