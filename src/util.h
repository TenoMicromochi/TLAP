#pragma once
#include "common.h"

// パスの最後の名前（区切りが無ければ全体）と、そのフォルダ（区切りが無ければ "."）
std::wstring NameOf(const std::wstring& path);
std::wstring DirOf(const std::wstring& path);
// 名前から最後の拡張子を除いたもの（"a.b.mp3" → "a.b"）
std::wstring StemOf(const std::wstring& path);

// 動作の記録（build.bat trace でビルドしたときだけ exe の隣の TLAP_trace.log に書く。普通のビルドでは何もしない）
#ifdef TLAP_TRACE
void Trace(const wchar_t* fmt, ...);
#else
inline void Trace(const wchar_t*, ...) {}
#endif
