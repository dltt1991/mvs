#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mvs {

struct CommandResult {
  int exitCode = 0;
  std::string stdoutText;
  std::string stderrText;
  long peakResidentSetSizeKb = 0;
  double userCpuSeconds = 0.0;
  double systemCpuSeconds = 0.0;
};

CommandResult runCommand(const std::vector<std::string>& args,
                         const std::filesystem::path& cwd,
                         const std::filesystem::path& logFile);

std::string shellQuote(const std::string& value);

}  // namespace mvs
