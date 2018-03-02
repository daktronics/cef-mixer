#pragma once

#include <stdint.h>
#include <string>


uint64_t time_now();

void log_message(const char*, ...);

std::string to_utf8(const wchar_t*);
std::wstring to_utf16(const char*);

int to_int(std::string, int default_val);
