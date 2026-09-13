#pragma once

#include <string>

namespace mwlog {

// Open (truncate) the log file at `path`. Safe to call more than once.
void Open(const std::wstring& path);

// Flush and close the log file.
void Close();

// printf-style logging. Writes to the file (if open) and to the debugger.
void Line(const char* fmt, ...);

} // namespace mwlog
