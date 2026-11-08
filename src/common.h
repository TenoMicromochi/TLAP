#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
inline COLORREF RGBc(unsigned v) { return RGB((v >> 16) & 255, (v >> 8) & 255, v & 255); }

extern int g_scale;      // UI の整数倍率 floor(DPI/96)
extern HFONT g_font;     // UI フォント（1倍。バー類は1倍で描いて整数倍に転送する）
