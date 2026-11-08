#include "util.h"
#include <cstdio>
#include <cstdarg>

std::wstring NameOf(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1); }
std::wstring DirOf(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? L"." : p.substr(0, i); }
std::wstring StemOf(const std::wstring& p) {
    std::wstring n = NameOf(p);
    size_t d = n.find_last_of(L'.');
    return (d == std::wstring::npos || d == 0) ? n : n.substr(0, d);
}

#ifdef TLAP_TRACE
void Trace(const wchar_t* fmt, ...) {
    static std::wstring path = [] {
        wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
        std::wstring s = b; size_t i = s.find_last_of(L'\\');
        return s.substr(0, i + 1) + L"TLAP_trace.log";
    }();
    wchar_t line[512]; va_list ap; va_start(ap, fmt); _vsnwprintf_s(line, _TRUNCATE, fmt, ap); va_end(ap);
    FILE* f = nullptr; if (_wfopen_s(&f, path.c_str(), L"ab") || !f) return;
    char u[1600]; int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u, sizeof u - 2, nullptr, nullptr);
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    if (n > 0) fwrite(u, 1, n - 1, f);
    fwrite("\r\n", 1, 2, f); fclose(f);
}
#endif
