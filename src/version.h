// アプリの版。ここだけを直す（res/TLAP.rc、About はこれを読む）。
// .rc からも読まれるので、書いてよいのは #define だけ
#pragma once
#define TLAP_VER_MAJOR 1
#define TLAP_VER_MINOR 0
#define TLAP_VER_PATCH 0
#define TLAP_VER_STR "1.0.0"

#ifndef RC_INVOKED
#define TLAP_WIDEN_(x) L##x
#define TLAP_WIDEN(x) TLAP_WIDEN_(x)
#define TLAP_VER_WSTR TLAP_WIDEN(TLAP_VER_STR)
#endif
