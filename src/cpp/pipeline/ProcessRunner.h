#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mvs {

struct CommandResult {
  int exitCode = 0;
  std::string stdoutText;
  std::string stderrText;
};

CommandResult runCommand(const std::vector<std::string>& args,
                         const std::filesystem::path& cwd,
                         const std::filesystem::path& logFile);

std::string shellQuote(const std::string& value);

}  // namespace mvs
