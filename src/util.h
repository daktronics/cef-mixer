#pragma once

#include <stdint.h>
#include <string>
#include <memory>

uint64_t time_now();

void log_message(const char*, ...);

std::string to_utf8(const wchar_t*);
std::string to_utf8(std::wstring const&);
std::wstring to_utf16(const char*);
std::wstring to_utf16(std::string const&);

int to_int(std::string, int default_val);

std::shared_ptr<std::string> locate_media(std::string const&);

// 
// simple method to wrap a raw COM pointer in a shared_ptr
// for auto Release()
//
template<class T>
std::shared_ptr<T> to_com_ptr(T* obj)
{
	return std::shared_ptr<T>(obj, [](T* p) { if (p) p->Release(); });
}
