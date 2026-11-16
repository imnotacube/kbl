#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>

#include <string>

// UTF-8 <-> UTF-16
std::wstring utf8ToWide(const std::string& s);
std::string wideToUtf8(const std::wstring& s);

// number of UTF-8 code points
size_t utf8Length(const std::string& s);
