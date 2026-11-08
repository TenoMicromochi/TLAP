#include "settings.h"
#include "lang.h"
#include <shlwapi.h>
#include <climits>
#include <cstdio>
#include <algorithm>

namespace settings {

static const wchar_t* kRepeatNames[] = { L"stop", L"all", L"one", L"track" };
static const wchar_t* kSection = L"TLAP";

// exe と同じ場所・同じ名前の .ini
static const std::wstring& IniPath() {
    static const std::wstring p = [] {
        wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
        PathRemoveExtensionW(b);
        return std::wstring(b) + L".ini";
    }();
    return p;
}
static void PutInt(const wchar_t* k, int v) { wchar_t b[32]; swprintf_s(b, L"%d", v); WritePrivateProfileStringW(kSection, k, b, IniPath().c_str()); }
static int GetInt(const wchar_t* k, int d) { return GetPrivateProfileIntW(kSection, k, d, IniPath().c_str()); }

void Load(Data& d) {
    d.volume = std::max(0, std::min(100, GetInt(L"Volume", 30) / 5 * 5));
    d.muted = GetInt(L"Muted", 0) != 0;
    { wchar_t b[32]; GetPrivateProfileStringW(kSection, L"Repeat", L"stop", b, 32, IniPath().c_str());
      d.repeat = R_STOP; for (int i = 0; i < 4; i++) if (!_wcsicmp(b, kRepeatNames[i])) d.repeat = i; }
    d.mini = GetInt(L"Mini", 0) != 0; d.miniX = GetInt(L"MiniX", INT_MIN); d.miniY = GetInt(L"MiniY", INT_MIN);
    d.topNormal = GetInt(L"TopNormal", 0) != 0; d.topMini = GetInt(L"TopMini", 0) != 0;
    d.eqOn = GetInt(L"EqOn", 0) != 0;
    d.eqPreset = std::max(0, std::min(5, GetInt(L"EqPreset", 0)));
    { wchar_t b[128]; GetPrivateProfileStringW(kSection, L"EqGains", L"", b, 128, IniPath().c_str());
      const wchar_t* p = b; for (int i = 0; i < 10; i++) { d.eqGain[i] = std::max(-12, std::min(12, _wtoi(p))); const wchar_t* c = wcschr(p, L','); if (!c) break; p = c + 1; } }
    { wchar_t b[MAX_PATH]; GetPrivateProfileStringW(kSection, L"Visualizer", L"None", b, MAX_PATH, IniPath().c_str());
      d.visFile = _wcsicmp(b, L"None") ? b : L""; }
    d.infoOn = GetInt(L"InfoPanel", 0) != 0; d.infoW = std::max(120, GetInt(L"InfoWidth", 240));
    d.miniVis = GetInt(L"MiniVisualizer", 1) != 0;
    d.openLast = GetInt(L"OpenLast", 1) != 0;
    { wchar_t b[MAX_PATH * 2]; GetPrivateProfileStringW(kSection, L"LastTrack", L"", b, MAX_PATH * 2, IniPath().c_str()); d.lastTrack = b;
      GetPrivateProfileStringW(kSection, L"VisualizerLast", L"", b, MAX_PATH * 2, IniPath().c_str()); d.visLast = b; }
    { wchar_t b[256]; GetPrivateProfileStringW(kSection, L"ColumnsOn", L"", b, 256, IniPath().c_str());
      if (b[0]) { const wchar_t* p = b; for (int i = 0; i < 15; i++) { d.colOn[i] = _wtoi(p) != 0; const wchar_t* c = wcschr(p, L','); if (!c) break; p = c + 1; } }
      d.colOn[2] = true;   // Title は外せない
      GetPrivateProfileStringW(kSection, L"ColumnWidths", L"", b, 256, IniPath().c_str());
      if (b[0]) { const wchar_t* p = b; for (int i = 0; i < 15; i++) { int w = _wtoi(p); if (w > 0) d.colW[i] = std::max(i == 2 ? 80 : 24, std::min(2000, w)); const wchar_t* c = wcschr(p, L','); if (!c) break; p = c + 1; } }
      d.sortCol = std::max(0, std::min(14, GetInt(L"SortColumn", 0))); d.sortDesc = GetInt(L"SortDescending", 0) != 0; }
    { wchar_t lb[32]; GetPrivateProfileStringW(kSection, L"Language", L"en", lb, 32, IniPath().c_str()); d.lang = lang::FromIni(lb); }
    d.coverOn = GetInt(L"Cover", 1) != 0; d.defOn = GetInt(L"DefaultFolderOn", 0) != 0;
    { wchar_t b[MAX_PATH * 2]; GetPrivateProfileStringW(kSection, L"DefaultFolder", L"", b, MAX_PATH * 2, IniPath().c_str()); d.defFolder = b; }
    d.coverW = std::max(120, std::min(2000, GetInt(L"CoverWidth", 200)));
    d.visHeight = std::max(32, std::min(600, GetInt(L"VisHeight", 120)));
    d.maximized = GetInt(L"Maximized", 0) != 0;
    int sx = GetInt(L"X", INT_MIN), sy = GetInt(L"Y", INT_MIN), sw = GetInt(L"Width", 0), sh = GetInt(L"Height", 0);
    if (sx != INT_MIN && sw >= 200 && sh >= 150) {
        RECT r = { sx, sy, sx + sw, sy + sh };
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) { d.x = sx; d.y = sy; d.w = sw; d.h = sh; }
    }
}

void Save(const Data& d) {
    PutInt(L"X", d.x); PutInt(L"Y", d.y);
    PutInt(L"Width", d.w); PutInt(L"Height", d.h);
    PutInt(L"Maximized", d.maximized ? 1 : 0);
    PutInt(L"Mini", d.mini ? 1 : 0); if (d.miniX != INT_MIN) { PutInt(L"MiniX", d.miniX); PutInt(L"MiniY", d.miniY); }
    PutInt(L"TopNormal", d.topNormal ? 1 : 0); PutInt(L"TopMini", d.topMini ? 1 : 0);
    WritePrivateProfileStringW(kSection, L"Language", lang::IniCode((lang::Lang)d.lang), IniPath().c_str());
    PutInt(L"EqOn", d.eqOn ? 1 : 0); PutInt(L"EqPreset", d.eqPreset);
    { wchar_t b[128] = L""; for (int i = 0; i < 10; i++) { wchar_t t[16]; swprintf_s(t, L"%s%d", i ? L"," : L"", d.eqGain[i]); wcscat_s(b, t); } WritePrivateProfileStringW(kSection, L"EqGains", b, IniPath().c_str()); }
    WritePrivateProfileStringW(kSection, L"Visualizer", d.visFile.empty() ? L"None" : d.visFile.c_str(), IniPath().c_str());
    PutInt(L"VisHeight", d.visHeight);
    PutInt(L"InfoPanel", d.infoOn ? 1 : 0); PutInt(L"InfoWidth", d.infoW); PutInt(L"MiniVisualizer", d.miniVis ? 1 : 0); PutInt(L"CoverWidth", d.coverW);
    PutInt(L"OpenLast", d.openLast ? 1 : 0); PutInt(L"Cover", d.coverOn ? 1 : 0); PutInt(L"DefaultFolderOn", d.defOn ? 1 : 0);
    WritePrivateProfileStringW(kSection, L"DefaultFolder", d.defFolder.empty() ? nullptr : d.defFolder.c_str(), IniPath().c_str());
    if (d.openLast || d.defOn) WritePrivateProfileStringW(kSection, L"LastTrack", d.lastTrack.empty() ? nullptr : d.lastTrack.c_str(), IniPath().c_str());   // 一覧が空なら前の値を消す。オフなら保存もしない
    WritePrivateProfileStringW(kSection, L"VisualizerLast", d.visLast.empty() ? nullptr : d.visLast.c_str(), IniPath().c_str());
    { wchar_t on[64] = L"", ww[128] = L""; for (int i = 0; i < 15; i++) { wchar_t t[16]; swprintf_s(t, L"%s%d", i ? L"," : L"", d.colOn[i] ? 1 : 0); wcscat_s(on, t); swprintf_s(t, L"%s%d", i ? L"," : L"", d.colW[i]); wcscat_s(ww, t); }
      WritePrivateProfileStringW(kSection, L"ColumnsOn", on, IniPath().c_str()); WritePrivateProfileStringW(kSection, L"ColumnWidths", ww, IniPath().c_str());
      PutInt(L"SortColumn", d.sortCol); PutInt(L"SortDescending", d.sortDesc ? 1 : 0); }
    PutInt(L"Volume", d.volume); PutInt(L"Muted", d.muted ? 1 : 0);
    WritePrivateProfileStringW(kSection, L"Repeat", kRepeatNames[d.repeat], IniPath().c_str());
}

}  // namespace settings
