#include "platform.h"
#include "util.h"

#include <stdio.h>
#include <stdarg.h>

#include <memory>
#include <sstream>

using namespace std;

LARGE_INTEGER qi_freq_ = {};

uint64_t time_now()
{
	if (!qi_freq_.HighPart && !qi_freq_.LowPart) {
		QueryPerformanceFrequency(&qi_freq_);
	}
	LARGE_INTEGER t = {};
	QueryPerformanceCounter(&t);
	return static_cast<uint64_t>(
		(t.QuadPart / double(qi_freq_.QuadPart)) * 1000000);
}

void log_message(const char* msg, ...)
{
	// old-school, printf style logging
	if (msg) 
	{
		char buff[512];
		va_list args;
		va_start(args, msg);
		vsprintf(buff, msg, args);
		OutputDebugStringA(buff);
	}
}

string to_utf8(wstring const& utf16)
{
	return to_utf8(utf16.c_str());
}

//
// quick and dirty conversion from utf-16 (wide-char) string to 
// utf8 string for Windows
//
string to_utf8(const wchar_t* utf16)
{
	if (!utf16) {
		return string();
	}

	auto const cch = static_cast<int>(wcslen(utf16));
	std::shared_ptr<char> utf8;
	auto const cb = WideCharToMultiByte(CP_UTF8, 0, utf16, cch,
		nullptr, 0, nullptr, nullptr);
	if (cb > 0)
	{
		utf8 = shared_ptr<char>(reinterpret_cast<char*>(malloc(cb + 1)), free);
		WideCharToMultiByte(CP_UTF8, 0, utf16, cch, utf8.get(), cb, nullptr, nullptr);
		*(utf8.get() + cch) = '\0';
	}
	if (!utf8) {
		return string();
	}
	return string(utf8.get(), cb);
}

std::wstring to_utf16(string const& utf8)
{
	return to_utf16(utf8.c_str());
}

//
// quick and dirty conversion from UTF-8 to wide-char string for Windows
//
wstring to_utf16(const char* utf8)
{
	if (!utf8) {
		return wstring();
	}

	auto const cb = static_cast<int>(strlen(utf8));
	std::shared_ptr<WCHAR> utf16;
	auto const cch = MultiByteToWideChar(CP_UTF8, 0, utf8, cb, nullptr, 0);
	if (cch > 0)
	{
		utf16 = shared_ptr<WCHAR>(reinterpret_cast<WCHAR*>(
				malloc(sizeof(WCHAR) * (cch + 1))), free);
		MultiByteToWideChar(CP_UTF8, 0, utf8, cb, utf16.get(), cch);
		*(utf16.get() + cch) = L'\0';
	}
	if (!utf16) {
		return wstring();
	}
	return wstring(utf16.get(), cch);
}

int to_int(std::string s, int default_val)
{
	int n;
	istringstream in(s);
	in >> n;
	if (!in.fail()) {
		return n;
	}
	return default_val;
}


//
// simply resolve a filename to an absolute path using the application
// directory as the base
//
std::shared_ptr<std::string> locate_media(std::string const& filespec)
{
	WCHAR basedir[MAX_PATH + 1];
	GetModuleFileName(nullptr, basedir, MAX_PATH);
	PathRemoveFileSpec(basedir);

	WCHAR filename[MAX_PATH + 1];
	auto const utf16 = to_utf16(filespec.c_str());
	PathCombine(filename, basedir, utf16.c_str());

	return make_shared<string>(to_utf8(filename));
}
