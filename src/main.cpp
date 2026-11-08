#include "common.h"
#include "version.h"
#include "ui.h"
#include "audio.h"
#include "folder.h"
#include "settings.h"
#include "util.h"
#include "zipfs.h"
#include "lang.h"
#include "tags.h"
#include "eq.h"
#include "vis.h"
#include "explorer.h"
#include <thread>
#include <atomic>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <cmath>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "avrt.lib")

static const UINT_PTR TM_TICK = 1, TM_SORT = 2;   // TM_SORT：タグを読んでいる間、値が埋まった分の並べ替えをまとめて直す
static const UINT WM_TLAP_OPEN = WM_APP + 40;            // WM_COPYDATA で受けたパスを開く（自分への Post）
static const ULONG_PTR kCopyMagic = 0x504C4154;          // WM_COPYDATA の dwData（'TLAP'）
static const wchar_t* g_cls = L"TLAPMain";

static HWND g_hwnd = nullptr, g_tip = nullptr;
static FileList g_fl;                 // 今のフォルダの曲（並び順どおり）
struct Meta {
    Tags tags; bool scanned = false;
    int sid = 0;                            // 裏のタグ読みを始めたときの番号（並べ替えても、読み取り結果をこれで戻せる）
    int no = 0;                             // フォルダ内の並び（「並び順」の順）での番号。# 列
    std::wstring codec; int rate = 0, kbps = 0;   // 形式・サンプルレート・ビットレート（裏のタグ読みで埋まる）
    unsigned long long size = 0, mod = 0;   // ファイルの大きさ・更新日時（FILETIME）。フォルダを読むときに取る
};
static std::vector<Meta> g_meta;      // g_fl.paths と同じ並び（タグ）
static Image g_cover;                 // 今の曲のジャケット（無ければ empty）
static int g_visH = 120;
// ミニ窓・Always on Top・メディアキー
static bool g_mini = false;                       // ミニ窓の表示中
static bool g_topN = false, g_topM = false;       // Always on Top（ふつうの窓 / ミニ窓で別々）
static WINDOWPLACEMENT g_normPlace = { sizeof(WINDOWPLACEMENT) };   // ミニ窓にする前のふつうの窓
static bool g_havePlace = false;
static int g_miniX = INT_MIN, g_miniY = INT_MIN;  // ミニ窓の位置
static bool g_eqWas = false;                      // ミニ窓にする前の EQ パネルの開閉
static bool g_hotOk[4] = {};                      // メディアキーの RegisterHotKey が通ったか              // スペアナの欄の高さ（論理px）
static std::vector<Row> g_rows;       // 一覧の表示用（g_fl.paths と同じ並び）
static int g_cur = -1;                // 今の曲（開いている曲）
static int g_sel = -1;                // 一覧で選んだ行
static int g_scroll = 0;              // 一覧の先頭の行
static int g_repeat = settings::R_STOP;
static bool g_infoOn = false;         // 情報パネルを出している
static int g_infoW = 240;             // その幅（論理px、仕切りを含む。保存値）
static bool g_miniVis = true;         // ミニ窓の下にスペアナを出す（Settings）
static bool g_coverOn = true;         // ジャケットの欄を出している（第5d段）
static bool g_defOn = false;          // 引数なしの起動で必ず g_defFolder を開く（Settings。第5d段）
static std::wstring g_defFolder;
static bool g_openLast = true;        // 起動時に前回の場所を開く（Settings）
static std::wstring g_visLast;        // 最後に使ったシェーダー（V・ボタンでオンに戻すとき）
static ColState g_cols;               // 曲の一覧の列・幅・並べ替え
static int g_colDrag = -1, g_colWGrab = 0;   // 列の幅のドラッグ（その列 / 境目からの掴み位置）
static bool g_hscrollDrag = false; static int g_hGrab = 0;
static bool g_sortDirty = false;      // タグが読めて値が埋まったので、並びを直す必要がある
// 左の列（ジャケット＋エクスプローラ）。幅は論理px（右端の仕切り4pxを含む。g_colW は保存値で、窓が狭いときの表示だけ縮める）
static const int kColMin = 120;
static int g_colW = 200, g_colShown = 200, g_infoColW = 0;   // g_infoColW: 情報パネルの表示幅
static bool g_colSplitDrag = false;
static int g_colGrab = 0;
// エクスプローラ（左の列のジャケットの下）。見ているフォルダ・その中身の行・選択・スクロール
static std::wstring g_expDir;                // 見ているフォルダ（"" = ドライブの一覧）。g_expHave が偽なら、まだ何も見ていない（起動時は空）
static bool g_expHave = false;
static std::vector<ExpRow> g_expRows;
static int g_expSel = -1, g_expScroll = 0;
static bool g_expFocus = false;              // キー操作（↑ ↓ Enter Backspace）がエクスプローラに向いている
static bool g_expScrollDrag = false;
static int g_expThumbGrab = 0;
static int g_msg = 0;                 // 状態欄の知らせ（第5c段）：0 なし / 1 No audio files / 2 Can't open this ZIP。曲を鳴らし直す・一覧を作り直すと消える
static bool g_err = false;            // 「Can't play this file」を出している
static UiState g_ui;
static bool g_tick = false;

// マウス操作
static bool g_seekDrag = false;
static double g_dragT = 0;
static bool g_volDrag = false;
static bool g_scrollDrag = false;
static int g_thumbGrab = 0;
static int g_wheelAcc = 0;            // 1目盛り(120)に満たない分

// ---------------------------------------------------------------- 設定
static bool ValidCur();
static bool OnTopNow() { return g_mini ? g_topM : g_topN; }
static void SaveSettings() {
    WINDOWPLACEMENT wp = { sizeof wp };
    if (g_mini && g_havePlace) wp = g_normPlace; else GetWindowPlacement(g_hwnd, &wp);   // ミニ窓のときは、その前のふつうの窓を保存する
    settings::Data d;
    d.x = wp.rcNormalPosition.left; d.y = wp.rcNormalPosition.top;
    d.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    d.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    d.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    d.volume = audio::Volume(); d.muted = audio::Muted(); d.repeat = g_repeat;
    { eq::Params p = eq::Get(); d.eqOn = p.on; d.eqPreset = p.preset; for (int i = 0; i < eq::kBands; i++) d.eqGain[i] = p.gain[i]; }
    d.visFile = vis::Current(); d.visHeight = g_visH;
    d.infoOn = g_infoOn; d.infoW = g_infoW; d.miniVis = g_miniVis; d.coverW = g_colW; d.openLast = g_openLast; d.visLast = g_visLast; d.coverOn = g_coverOn; d.defOn = g_defOn; d.defFolder = g_defFolder;
    d.lang = (int)lang::Get();
    for (int i = 0; i < LC_COUNT; i++) { d.colOn[i] = g_cols.on[i]; d.colW[i] = g_cols.w[i]; } d.sortCol = g_cols.sortCol; d.sortDesc = g_cols.desc;
    if (ValidCur()) d.lastTrack = g_fl.paths[g_cur]; else if (g_sel >= 0 && g_sel < (int)g_fl.paths.size()) d.lastTrack = g_fl.paths[g_sel];   // 鳴っていた（一時停止中も）曲。無ければ選んでいた曲 d.lang = (int)lang::Get();
    d.mini = g_mini; d.topNormal = g_topN; d.topMini = g_topM;
    if (g_mini) { RECT r; GetWindowRect(g_hwnd, &r); d.miniX = r.left; d.miniY = r.top; } else { d.miniX = g_miniX; d.miniY = g_miniY; }
    settings::Save(d);
}

// ---------------------------------------------------------------- 画面の状態
static bool HaveList() { return !g_rows.empty(); }
static bool ValidCur() { return g_cur >= 0 && g_cur < (int)g_rows.size(); }

static std::wstring FmtRate(const AudioInfo& ai) {
    if (ai.rate <= 0) return L"";
    wchar_t b[48];
    double k = ai.rate / 1000.0;
    if (ai.rate % 1000 == 0) swprintf_s(b, L"%dkHz", ai.rate / 1000);
    else if (ai.rate % 100 == 0) swprintf_s(b, L"%.1fkHz", k);
    else swprintf_s(b, L"%.2fkHz", k);
    std::wstring s = b;
    if (ai.lossless && ai.bits > 0) { swprintf_s(b, L" %dbit", ai.bits); s += b; }   // 不可逆圧縮はビット数を出さない
    return s;
}

static void InvalidateChrome() { InvalidateRect(g_hwnd, nullptr, FALSE); }

static void EnsureVisible(int i) {
    int vis = ui::VisibleRows(), n = (int)g_rows.size();
    if (i < g_scroll) g_scroll = i;
    else if (i >= g_scroll + vis) g_scroll = i - vis + 1;
    g_scroll = std::max(0, std::min(g_scroll, ui::MaxScroll(n)));
}

// 画面の状態を今の曲に合わせる。変わったところがあればバー類を描き直す
static void RefreshUI() {
    if (!g_hwnd) return;
    UiState n = g_ui;
    n.rows = &g_rows; n.cur = g_cur; n.sel = g_sel; n.scroll = g_scroll;
    n.haveList = HaveList();
    n.loaded = audio::Loaded();
    n.playing = audio::Playing();
    n.errorMsg = g_err || g_msg != 0;
    n.mini = g_mini; n.onTop = OnTopNow();
    n.title = (ValidCur() && n.loaded) ? g_rows[g_cur].name : std::wstring();
    n.artist = (ValidCur() && n.loaded) ? g_meta[g_cur].tags.artist : std::wstring();
    n.haveCover = !g_cover.empty(); n.coverOn = g_coverOn;
    n.visOn = vis::Active(); n.visErr = vis::Error();
    n.infoOpen = g_infoOn;
    n.miniVis = g_miniVis;
    n.exp = &g_expRows; n.expSel = g_expSel; n.expScroll = g_expScroll; n.expFocus = g_expFocus && !g_mini;
    n.curPath = (ValidCur() && n.loaded) ? g_fl.paths[g_cur] : std::wstring();
    vis::SetPlaying(n.playing);
    n.repeat = g_repeat;
    n.volume = audio::Volume(); n.muted = audio::Muted();
    n.dur = audio::Duration();
    n.pos = g_seekDrag ? g_dragT : audio::Position();
    n.status = StatusInfo();
    if (g_err) n.status.left = lang::T(lang::ST_CANT);
    else if (g_msg) n.status.left = lang::T(g_msg == 1 ? lang::ST_NOAUDIO : lang::ST_BADZIP);
    else if (n.loaded) { AudioInfo ai = audio::Info(); n.status.left = ai.codec; n.status.rate = FmtRate(ai); }
    if (ValidCur() && n.loaded) { wchar_t b[64]; swprintf_s(b, L"%d / %d", g_cur + 1, (int)g_rows.size()); n.status.index = b; }
    bool changed = !(n.status == g_ui.status) || n.loaded != g_ui.loaded || n.playing != g_ui.playing || n.errorMsg != g_ui.errorMsg || n.haveCover != g_ui.haveCover || n.coverOn != g_ui.coverOn || n.mini != g_ui.mini || n.onTop != g_ui.onTop || n.title != g_ui.title || n.artist != g_ui.artist || n.visOn != g_ui.visOn || n.visErr != g_ui.visErr ||
                   n.repeat != g_ui.repeat || n.infoOpen != g_ui.infoOpen || n.volume != g_ui.volume || n.muted != g_ui.muted || n.haveList != g_ui.haveList ||
                   n.dur != g_ui.dur || n.cur != g_ui.cur || n.sel != g_ui.sel || n.scroll != g_ui.scroll ||
                   (n.pos != g_ui.pos && g_seekDrag) ||
                   n.expSel != g_ui.expSel || n.expScroll != g_ui.expScroll || n.expFocus != g_ui.expFocus || n.curPath != g_ui.curPath ||
                   n.rows != g_ui.rows || g_ui.rows == nullptr;
    bool posChanged = (long long)n.pos != (long long)g_ui.pos;   // 時間の表示・シークバーだけが動いたときは、その矩形だけを描き直す
    // 曲の長さが一覧に入ったときも描き直す
    static double lastDur = -1; if (ValidCur() && g_rows[g_cur].dur != lastDur) { lastDur = g_rows[g_cur].dur; changed = true; }
    int ht = g_ui.hoverTool, pt = g_ui.pressTool, hm = g_ui.hoverMenu, om = g_ui.openMenu;
    g_ui = n; g_ui.hoverTool = ht; g_ui.pressTool = pt; g_ui.hoverMenu = hm; g_ui.openMenu = om;
    if (!n.haveList) g_ui.hoverTool = g_ui.pressTool = -1;
    if (changed || (posChanged && g_mini)) InvalidateChrome();
    else if (posChanged) { RECT cl; GetClientRect(g_hwnd, &cl); RECT sk = ui::Calc(cl.right / g_scale, cl.bottom / g_scale).seek; RECT pr = { sk.left * g_scale, sk.top * g_scale, sk.right * g_scale, sk.bottom * g_scale }; InvalidateRect(g_hwnd, &pr, FALSE); }

    // 再生中（とドラッグ中）だけ、時間表示を進めるタイマーを回す
    bool want = n.playing || g_seekDrag;
    if (want && !g_tick) { SetTimer(g_hwnd, TM_TICK, 100, nullptr); g_tick = true; }
    else if (!want && g_tick) { KillTimer(g_hwnd, TM_TICK); g_tick = false; }

    // 再生中はスリープしない（画面は消えてよい）。停止中は解除する
    static bool awake = false;
    if (n.playing != awake) { SetThreadExecutionState(ES_CONTINUOUS | (n.playing ? ES_SYSTEM_REQUIRED : 0)); awake = n.playing; }

    std::wstring title = L"TLAP";
    if (ValidCur() && n.loaded) { title = g_rows[g_cur].name; const std::wstring& ar = g_meta[g_cur].tags.artist; if (!ar.empty()) title += L" - " + ar; title += L" - TLAP"; }
    static std::wstring lastTitle;
    if (title != lastTitle) { lastTitle = title; SetWindowTextW(g_hwnd, title.c_str()); }
}

// ---------------------------------------------------------------- 曲を開く・移る
// ---------------------------------------------------------------- EQ パネル（eq ボタン・E で開閉する小さなツールウィンドウ）
static HWND g_eqWnd = nullptr, g_eqOn = nullptr, g_eqCombo = nullptr;
static HWND g_eqSl[eq::kBands], g_eqDb[eq::kBands];
static HFONT g_eqFont = nullptr;
static bool g_splitDrag = false;

static void EqToControls(const eq::Params& p) {
    if (!g_eqWnd) return;
    SendMessageW(g_eqOn, BM_SETCHECK, p.on ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_eqCombo, CB_SETCURSEL, p.preset, 0);
    for (int i = 0; i < eq::kBands; i++) {
        SendMessageW(g_eqSl[i], TBM_SETPOS, TRUE, 12 - p.gain[i]);
        wchar_t b[16]; swprintf_s(b, L"%+d", p.gain[i]); SetWindowTextW(g_eqDb[i], b);
    }
}

static LRESULT CALLBACK EqProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        eq::Params p = eq::Get();
        if ((HWND)l == g_eqOn && HIWORD(w) == BN_CLICKED) { p.on = SendMessageW(g_eqOn, BM_GETCHECK, 0, 0) == BST_CHECKED; eq::Set(p); }
        else if ((HWND)l == g_eqCombo && HIWORD(w) == CBN_SELCHANGE) {
            p.preset = (int)SendMessageW(g_eqCombo, CB_GETCURSEL, 0, 0);
            if (const int* pg = eq::PresetGains(p.preset)) for (int i = 0; i < eq::kBands; i++) p.gain[i] = pg[i];   // Custom は今の値のまま
            eq::Set(p); EqToControls(p);
        }
        return 0; }
    case WM_VSCROLL: {   // スライダーを触ると Custom になる
        for (int i = 0; i < eq::kBands; i++) if ((HWND)l == g_eqSl[i]) {
            eq::Params p = eq::Get();
            p.gain[i] = 12 - (int)SendMessageW(g_eqSl[i], TBM_GETPOS, 0, 0); p.preset = eq::P_CUSTOM;
            eq::Set(p); EqToControls(p);
        }
        return 0; }
    case WM_CLOSE: ShowWindow(h, SW_HIDE); g_ui.eqOpen = false; InvalidateChrome(); return 0;   // 閉じる＝隠す
    case WM_KEYDOWN: if (w == 'E' || w == VK_ESCAPE) { SendMessageW(h, WM_CLOSE, 0, 0); return 0; } break;
    }
    return DefWindowProcW(h, m, w, l);
}

static void ShowEq(bool on) {
    if (on && !g_eqWnd) {
        WNDCLASSEXW wc = { sizeof wc }; wc.lpfnWndProc = EqProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TLAPEq";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
        int S = g_scale; RECT rc; GetWindowRect(g_hwnd, &rc);
        RECT cr = { 0, 0, 432 * S, 232 * S };
        AdjustWindowRectEx(&cr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
        g_eqWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"TLAPEq", lang::T(lang::EQ_TITLE), WS_POPUP | WS_CAPTION | WS_SYSMENU,
            rc.left + 40 * S, rc.top + 90 * S, cr.right - cr.left, cr.bottom - cr.top, g_hwnd, nullptr, wc.hInstance, nullptr);
        g_eqFont = ui::CreateUiFont(S);
        auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int ww, int hh) {
            HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x * S, y * S, ww * S, hh * S, g_eqWnd, nullptr, wc.hInstance, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)g_eqFont, TRUE); return c; };
        g_eqOn = mk(L"BUTTON", lang::T(lang::EQ_ON), BS_AUTOCHECKBOX | WS_TABSTOP, 12, 8, 56, 20);
        g_eqCombo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 76, 6, 130, 200);
        for (int i = 0; i < eq::kPresetCount; i++) SendMessageW(g_eqCombo, CB_ADDSTRING, 0, (LPARAM)eq::PresetName(i));
        static const wchar_t* hz[eq::kBands] = { L"31", L"62", L"125", L"250", L"500", L"1k", L"2k", L"4k", L"8k", L"16k" };
        for (int i = 0; i < eq::kBands; i++) {
            int x = 12 + i * 41;
            g_eqDb[i] = mk(L"STATIC", L"0", SS_CENTER, x, 38, 40, 14);
            g_eqSl[i] = mk(TRACKBAR_CLASSW, L"", TBS_VERT | TBS_NOTICKS | WS_TABSTOP, x + 5, 54, 30, 150);
            SendMessageW(g_eqSl[i], TBM_SETRANGE, TRUE, MAKELONG(0, 24)); SendMessageW(g_eqSl[i], TBM_SETLINESIZE, 0, 1); SendMessageW(g_eqSl[i], TBM_SETPAGESIZE, 0, 3);
            mk(L"STATIC", hz[i], SS_CENTER, x, 208, 40, 14);
        }
    }
    if (!g_eqWnd) return;
    if (on) { EqToControls(eq::Get()); ShowWindow(g_eqWnd, SW_SHOWNOACTIVATE); }
    else ShowWindow(g_eqWnd, SW_HIDE);
    g_ui.eqOpen = on; InvalidateChrome();
}

// ---------------------------------------------------------------- タグ・ジャケット・情報パネル・次の曲の予約
static unsigned g_listGen = 0;                 // 一覧を作り直すたびに増える（古い読み取り結果を捨てる）
static std::thread g_scanThread;
static std::atomic<bool> g_scanCancel{ false };
static const UINT WM_SCAN = WM_APP + 10, WM_SCANDONE = WM_APP + 11;
struct ScanItem { unsigned gen; int idx; Tags t; double dur; std::wstring codec; int rate = 0; };
static int g_nextIdx = -1;                     // audio に予約してある次の曲（-1 = 無し）
// 情報パネル（ふつうの窓の左の欄。読み取り専用の EDIT）。幅は論理px（仕切り4pxを含む）。g_infoW は保存値で、窓が狭いときの表示だけ縮める
static const int kInfoId = 900, kInfoMin = 120, kInfoViewMin = 200;
static HWND g_infoEdit = nullptr;
static HFONT g_infoFont = nullptr;
static WNDPROC g_infoOldProc = nullptr;
static bool g_infoSplitDrag = false;
static int g_infoGrab = 0;
static std::wstring g_infoShown;
static HWND g_coverWnd = nullptr;
static Image g_coverBig;

static std::wstring TrackName(int i) {
    if (i < 0 || i >= (int)g_meta.size()) return L"";
    return g_meta[i].tags.title.empty() ? StemOf(g_fl.paths[i]) : g_meta[i].tags.title;
}

static std::vector<int> g_sidPos;   // sid → 今の位置
static void RebuildSidPos() { g_sidPos.assign(g_meta.size(), -1); for (size_t i = 0; i < g_meta.size(); i++) if (g_meta[i].sid >= 0 && g_meta[i].sid < (int)g_meta.size()) g_sidPos[g_meta[i].sid] = (int)i; }
// 裏のスレッドで全曲のタグと長さを読み、読めたものから一覧に反映する
static void StartScan() {
    g_scanCancel = true; if (g_scanThread.joinable()) g_scanThread.join();
    g_scanCancel = false;
    std::vector<std::wstring> paths = g_fl.paths; unsigned gen = g_listGen; HWND hw = g_hwnd;
    for (size_t i = 0; i < g_meta.size(); i++) g_meta[i].sid = (int)i;
    RebuildSidPos();
    SetTimer(hw, TM_SORT, 250, nullptr);
    g_scanThread = std::thread([paths, gen, hw] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (size_t i = 0; i < paths.size() && !g_scanCancel; i++) {
            ScanItem* it = new ScanItem(); it->gen = gen; it->idx = (int)i;
            ReadTags(paths[i], it->t, false);
            it->dur = ProbeDuration(paths[i]);
            { ProbeResult pr; if (ProbeInfo(paths[i], pr)) { it->codec = pr.codec; it->rate = pr.rate; if (it->dur <= 0) it->dur = pr.dur; } }
            PostMessageW(hw, WM_SCAN, 0, (LPARAM)it);
        }
        if (!g_scanCancel) PostMessageW(hw, WM_SCANDONE, 0, (LPARAM)gen);
        CoUninitialize();
    });
}

static std::wstring InfoText() {
    if (!ValidCur() || !audio::Loaded()) return L"";
    const std::wstring& path = g_fl.paths[g_cur];
    Tags t = g_meta[g_cur].tags;
    if (!g_meta[g_cur].scanned) ReadTags(path, t, false);
    AudioInfo ai = audio::Info(); double d = audio::Duration();
    std::wstring o;
    auto line = [&](lang::Id k, const std::wstring& v) { o += lang::T(k); o += L": "; o += v; o += L"\r\n"; };
    auto num = [](long long v) { return v > 0 ? std::to_wstring(v) : std::wstring(L"-"); };
    wchar_t b[64];
    line(lang::INF_TITLE, t.title.empty() ? StemOf(path) : t.title);
    line(lang::INF_ARTIST, t.artist); line(lang::INF_ALBUM, t.album); line(lang::INF_ALBUMARTIST, t.albumArtist);
    line(lang::INF_TRACK, num(t.track)); line(lang::INF_DISC, num(t.disc)); line(lang::INF_YEAR, t.year);
    std::wstring ext = PathFindExtensionW(path.c_str()); if (!ext.empty()) ext.erase(0, 1); for (auto& c : ext) c = towupper(c);
    line(lang::INF_FORMAT, (ai.codec.empty() || ai.codec == ext) ? ext : ai.codec + L" (" + ext + L")");
    swprintf_s(b, L"%d Hz", ai.rate); line(lang::INF_RATE, b);
    line(lang::INF_BITS, ai.bits > 0 ? std::to_wstring(ai.bits) : L"-");
    line(lang::INF_CHANNELS, num(ai.channels));
    long long sz = 0; { uint64_t s0 = 0, m0 = 0; if (zipfs::FileStat(path, s0, m0)) sz = (long long)s0; }
    if (d > 0 && sz > 0) { swprintf_s(b, L"%d kbps", (int)std::lround(sz * 8.0 / d / 1000)); line(lang::INF_BITRATE, b); } else line(lang::INF_BITRATE, L"-");
    line(lang::INF_LENGTH, ui::FmtTime(d, d >= 3600));
    line(lang::INF_FILE, NameOf(path)); line(lang::INF_FOLDER, DirOf(path));
    return o;
}
static void DoLayout();
static void SetInfoFont();
// 中身を作り直す（曲が変わったとき）。変わっていなければ触らない（選択とスクロールを保つ）。ふつうの窓で出していないときは何もしない（出すときに作る）
static void RefreshInfo() {
    if (!g_infoEdit || !g_infoOn || g_mini) return;
    std::wstring t = InfoText();
    if (t == g_infoShown) return;
    g_infoShown = t;
    SetWindowTextW(g_infoEdit, t.c_str());
    SendMessageW(g_infoEdit, EM_SETSEL, 0, 0);
    SendMessageW(g_infoEdit, EM_LINESCROLL, 0, -0x7FFFFF);   // スクロールを先頭に戻す
}

// テキストにフォーカスがある間は、単キーを再生の操作にしない（EDIT の中で完結する）。Esc でフォーカスを外す。
// Ctrl が付くキーは、Ctrl+A / Ctrl+C 以外を本体の操作に回す。ホイールは EDIT がそのまま使う
static LRESULT CALLBACK InfoEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { SetFocus(g_hwnd); return 0; }
        if (GetKeyState(VK_CONTROL) < 0) {
            if (w == 'A') { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
            if (w == 'C' || w == VK_INSERT) break;
            SendMessageW(g_hwnd, WM_KEYDOWN, w, l); return 0;   // Ctrl+O / Ctrl+M / Ctrl+T / Ctrl+, など
        }
        break;
    case WM_CHAR: if (GetKeyState(VK_CONTROL) < 0 && w != 3 && w != 1) return 0; break;
    case WM_SETFOCUS: if (g_expFocus) { g_expFocus = false; InvalidateChrome(); } break;   // キー操作はテキストへ（エクスプローラの選択色は薄くなる）
    }
    return CallWindowProcW(g_infoOldProc, h, m, w, l);
}

static void CreateInfoEdit(HWND parent) {
    g_infoEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_CLIPSIBLINGS | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)kInfoId, GetModuleHandleW(nullptr), nullptr);
    if (!g_infoEdit) return;
    SendMessageW(g_infoEdit, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    g_infoOldProc = (WNDPROC)SetWindowLongPtrW(g_infoEdit, GWLP_WNDPROC, (LONG_PTR)InfoEditProc);
    SetInfoFont();
}

static void SetInfoFont() {
    HFONT nf = ui::CreateUiFont(g_scale);
    if (g_infoEdit) SendMessageW(g_infoEdit, WM_SETFONT, (WPARAM)nf, TRUE);
    if (g_infoFont) DeleteObject(g_infoFont);
    g_infoFont = nf;
}

static void ShowInfo(bool on) {
    if (g_mini) return;   // ミニ窓では出さない（ふつうの窓に戻ったとき）
    g_infoOn = on;
    if (!on && g_infoEdit && GetFocus() == g_infoEdit) SetFocus(g_hwnd);
    g_ui.infoOpen = on;
    RefreshInfo();
    DoLayout();
}

static unsigned g_coverGen = 1;   // g_cover が替わるたびに増やす（縮小キャッシュの鍵）
// 縮めたジャケットのキャッシュ。曲が替わったとき・置き先の物理サイズが変わったときだけ作り直す
static struct CoverCache { unsigned gen = 0; int bw = 0, bh = 0, dw = 0, dh = 0; HBITMAP bmp = nullptr; } g_cc;

// 画像を枠 box（物理px）に収めて描く。枠より大きいときは高品質で縮小、小さいときは整数倍・補間なし
static void DrawImageFit(HDC hdc, const Image& im, RECT box) {
    if (im.empty()) return;
    int bw = box.right - box.left, bh = box.bottom - box.top;
    double f = std::min((double)bw / im.w, (double)bh / im.h);
    int dw, dh, mode;
    if (f < 1) { dw = std::max(1, (int)std::lround(im.w * f)); dh = std::max(1, (int)std::lround(im.h * f)); mode = HALFTONE; }
    else { int mul = (int)f; dw = im.w * mul; dh = im.h * mul; mode = COLORONCOLOR; }
    int x = box.left + (bw - dw) / 2, y = box.top + (bh - dh) / 2;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = im.w; bi.bmiHeader.biHeight = -im.h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    SetStretchBltMode(hdc, mode); if (mode == HALFTONE) SetBrushOrgEx(hdc, 0, 0, nullptr);
    StretchDIBits(hdc, x, y, dw, dh, 0, 0, im.w, im.h, im.px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

// ジャケットを枠 box（物理px）の中央に出す。画像の拡大縮小はキャッシュした絵を使う（縮小は高品質、拡大は整数倍・補間なし）
static void DrawCoverCached(HDC hdc, RECT box) {
    if (g_cover.empty()) return;
    int bw = box.right - box.left, bh = box.bottom - box.top;
    if (bw <= 0 || bh <= 0) return;
    if (g_cc.gen != g_coverGen || g_cc.bw != bw || g_cc.bh != bh || !g_cc.bmp) {
        if (g_cc.bmp) { DeleteObject(g_cc.bmp); g_cc.bmp = nullptr; }
        double f = std::min((double)bw / g_cover.w, (double)bh / g_cover.h);
        int dw, dh;
        if (f < 1) { dw = std::max(1, (int)std::lround(g_cover.w * f)); dh = std::max(1, (int)std::lround(g_cover.h * f)); }
        else { int mul = (int)f; dw = g_cover.w * mul; dh = g_cover.h * mul; }
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, dw, dh);
        HGDIOBJ old = SelectObject(mem, bmp);
        DrawImageFit(mem, g_cover, RECT{ 0, 0, dw, dh });
        SelectObject(mem, old); DeleteDC(mem);
        g_cc.gen = g_coverGen; g_cc.bw = bw; g_cc.bh = bh; g_cc.dw = dw; g_cc.dh = dh; g_cc.bmp = bmp;
    }
    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, g_cc.bmp);
    BitBlt(hdc, box.left + (bw - g_cc.dw) / 2, box.top + (bh - g_cc.dh) / 2, g_cc.dw, g_cc.dh, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteDC(mem);
}

// ジャケットをクリックしたときの大きい表示（クリックか Esc で閉じる）
static LRESULT CALLBACK CoverProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); DrawImageFit(dc, g_coverBig, rc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: DestroyWindow(h); return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) { DestroyWindow(h); return 0; } break;
    case WM_DESTROY: g_coverWnd = nullptr; SetForegroundWindow(g_hwnd); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void ShowBigCover() {
    if (g_cover.empty() || g_coverWnd) return;
    g_coverBig = g_cover;
    WNDCLASSEXW wc = { sizeof wc }; wc.lpfnWndProc = CoverProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TLAPCover";
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    RegisterClassExW(&wc);
    MONITORINFO mi = { sizeof mi }; GetMonitorInfoW(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    int aw = (mi.rcWork.right - mi.rcWork.left) * 9 / 10, ah = (mi.rcWork.bottom - mi.rcWork.top) * 9 / 10;
    double f = std::min((double)aw / g_coverBig.w, (double)ah / g_coverBig.h);
    int dw, dh;
    if (f < 1) { dw = std::max(1, (int)std::lround(g_coverBig.w * f)); dh = std::max(1, (int)std::lround(g_coverBig.h * f)); }
    else { int mul = (int)f; dw = g_coverBig.w * mul; dh = g_coverBig.h * mul; }
    int x = (mi.rcWork.left + mi.rcWork.right - dw) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - dh) / 2;
    g_coverWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"TLAPCover", L"", WS_POPUP | WS_BORDER, x, y, dw + 2, dh + 2, g_hwnd, nullptr, wc.hInstance, nullptr);
    if (g_coverWnd) { RECT cr; GetClientRect(g_coverWnd, &cr); SetWindowPos(g_coverWnd, nullptr, x, y, dw + (dw - cr.right), dh + (dh - cr.bottom), SWP_NOZORDER); ShowWindow(g_coverWnd, SW_SHOW); SetForegroundWindow(g_coverWnd); SetFocus(g_coverWnd); }
}

// 今の曲の次に続ける曲を audio に予約する（ギャップレス）。リピートの種類と、再生できない曲の灰色を見て決める
static void PrepareNext() {
    audio::ClearNext(); g_nextIdx = -1;
    int n = (int)g_rows.size();
    if (!audio::Loaded() || !ValidCur() || n == 0) return;
    if (g_repeat == settings::R_TRACK) return;   // Stop after track：次の曲を先に開かない（曲が終わったら止まる）
    if (g_repeat == settings::R_ONE) { if (audio::QueueNext(g_fl.paths[g_cur])) g_nextIdx = g_cur; return; }
    int idx = g_cur + 1;
    for (int tries = 0; tries < n; tries++) {
        if (idx >= n) { if (g_repeat == settings::R_ALL) idx = 0; else return; }
        if (g_rows[idx].bad) { idx++; continue; }
        if (audio::QueueNext(g_fl.paths[idx])) { g_nextIdx = idx; return; }
        g_rows[idx].bad = true; idx++;
    }
}

// ジャケットの読み込み（タグの画像の取り出し・WIC のデコード）は裏のスレッドで行う。曲が替わったら古い要求は捨てて、新しい曲のものだけ反映する。
// 結果が届くまでは、前の絵を出したままにする（同じアルバムの曲で絵がちらつかない）
static const UINT WM_COVER = WM_APP + 12;
struct CoverResult { unsigned id; Image im; };
static std::atomic<unsigned> g_coverId{ 0 };
static std::atomic<int> g_coverActive{ 0 };
static void LoadCoverForCur() {
    if (!g_coverOn) { g_coverId++; g_cover = Image(); g_coverGen++; return; }   // ジャケットの欄を消している間は読み込まない
    if (!ValidCur()) { g_coverId++; g_cover = Image(); g_coverGen++; InvalidateChrome(); return; }
    unsigned id = ++g_coverId; std::wstring path = g_fl.paths[g_cur]; HWND hw = g_hwnd;
    g_coverActive++;
    std::thread([id, path, hw] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CoverResult* r = new CoverResult(); r->id = id;
        if (g_coverId == id) LoadCover(path, r->im);      // もう古い要求なら読まない
        PostMessageW(hw, WM_COVER, 0, (LPARAM)r);
        CoUninitialize();
        g_coverActive--;
    }).detach();
}

// 曲が替わったとき（開いた・継ぎ目で移った）
static void OnTrackStarted() {
    LoadCoverForCur();
    PrepareNext();
    RefreshInfo();
}

// ---------------------------------------------------------------- 曲の一覧の列と並べ替え（第5b段）
// 配列（g_fl.paths / g_rows / g_meta）はいつも「表示の順」。再生の順（prev / next・先読み・Repeat folder・Stop at folder end）もこの順
static std::wstring FmtSizeText(unsigned long long b) {
    wchar_t t[32];
    if (b < 1024ull * 1024) swprintf_s(t, L"%llu KB", (b + 512) / 1024);
    else swprintf_s(t, L"%.1f MB", b / 1048576.0);
    return t;
}
static std::wstring FmtModText(unsigned long long ft) {
    if (!ft) return L"";
    FILETIME f; f.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFF); f.dwHighDateTime = (DWORD)(ft >> 32);
    FILETIME lf; SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(&f, &lf) || !FileTimeToSystemTime(&lf, &st)) return L"";
    wchar_t t[32]; swprintf_s(t, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return t;
}
static std::wstring FmtKhz(int rate) {
    if (rate <= 0) return L"";
    wchar_t b[32];
    if (rate % 1000 == 0) swprintf_s(b, L"%dkHz", rate / 1000);
    else if (rate % 100 == 0) swprintf_s(b, L"%.1fkHz", rate / 1000.0);
    else swprintf_s(b, L"%.2fkHz", rate / 1000.0);
    return b;
}
// i 行目の各列の文字を作る
static void UpdateCells(int i) {
    Row& r = g_rows[i]; const Meta& m = g_meta[i]; const Tags& t = m.tags;
    wchar_t b[48];
    r.cell[LC_NO] = std::to_wstring(m.no);
    if (t.track > 0) { if (t.disc > 0) swprintf_s(b, L"%d-%02d", t.disc, t.track); else swprintf_s(b, L"%02d", t.track); r.cell[LC_TRACK] = b; } else r.cell[LC_TRACK].clear();
    r.cell[LC_TITLE] = r.name; r.cell[LC_ARTIST] = t.artist; r.cell[LC_ALBUM] = t.album; r.cell[LC_ALBUMARTIST] = t.albumArtist;
    r.cell[LC_YEAR] = t.year; r.cell[LC_GENRE] = t.genre;
    r.cell[LC_LEN] = r.dur > 0 ? ui::FmtTime(r.dur, r.dur >= 3600) : std::wstring();
    r.cell[LC_FORMAT] = m.codec;
    if (m.kbps > 0) { swprintf_s(b, L"%d kbps", m.kbps); r.cell[LC_BITRATE] = b; } else r.cell[LC_BITRATE].clear();
    r.cell[LC_RATE] = FmtKhz(m.rate);
    r.cell[LC_SIZE] = m.size ? FmtSizeText(m.size) : std::wstring();
    r.cell[LC_FILE] = NameOf(g_fl.paths[i]);
    r.cell[LC_MODIFIED] = FmtModText(m.mod);
}
// 並べ替えの値。数の列は NumVal、文字の列は TextVal。値が無ければ Missing（昇順でも降順でも最後に置く）
static double NumVal(int i, int col) {
    const Meta& m = g_meta[i];
    switch (col) {
    case LC_NO: return m.no;
    case LC_TRACK: return m.tags.track > 0 ? (m.tags.disc > 0 ? m.tags.disc : 1) * 100000.0 + m.tags.track : 0;
    case LC_YEAR: return _wtoi(m.tags.year.c_str());
    case LC_LEN: return g_rows[i].dur;
    case LC_BITRATE: return m.kbps;
    case LC_RATE: return m.rate;
    case LC_SIZE: return (double)m.size;
    case LC_MODIFIED: return (double)m.mod;
    }
    return 0;
}
static std::wstring TextVal(int i, int col) {
    const Meta& m = g_meta[i];
    switch (col) {
    case LC_TITLE: return g_rows[i].name;
    case LC_ARTIST: return m.tags.artist; case LC_ALBUM: return m.tags.album; case LC_ALBUMARTIST: return m.tags.albumArtist;
    case LC_GENRE: return m.tags.genre; case LC_FORMAT: return m.codec;
    case LC_FILE: return NameOf(g_fl.paths[i]);
    }
    return L"";
}
static bool ColNumeric(int col) { return ColIsNumeric(col) || col == LC_MODIFIED; }
static bool Missing(int i, int col) { return ColNumeric(col) ? (col == LC_NO ? false : NumVal(i, col) <= 0) : (col == LC_TITLE || col == LC_FILE ? false : TextVal(i, col).empty()); }
static int CmpCol(int a, int b, int col) {
    if (ColNumeric(col)) { double x = NumVal(a, col), y = NumVal(b, col); return x < y ? -1 : (x > y ? 1 : 0); }
    std::wstring x = TextVal(a, col), y = TextVal(b, col);
    if (col == LC_TITLE || col == LC_FILE) return StrCmpLogicalW(x.c_str(), y.c_str());
    int r = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, x.c_str(), (int)x.size(), y.c_str(), (int)y.size());
    return r - 2;   // CSTR_LESS_THAN 1 / EQUAL 2 / GREATER_THAN 3
}
// 全曲にトラック番号があれば、ディスク番号 → トラック番号の順が「フォルダ内の並び」。欠けていればファイル名の自然順
static bool AllHaveTrack() { for (auto& m : g_meta) if (m.tags.track <= 0) return false; return !g_meta.empty(); }

// フォルダ内の並びの番号（# 列）を付け、並べ替えの列と向きに従って配列を並べ直す。選んでいる曲・鳴っている曲は変えない。変わったら true
static bool ResortAll() {
    int n = (int)g_rows.size();
    if (!n) return false;
    bool allTrack = AllHaveTrack();
    std::vector<int> ord(n); for (int i = 0; i < n; i++) ord[i] = i;
    std::stable_sort(ord.begin(), ord.end(), [&](int a, int b) {
        if (allTrack) {
            int da = g_meta[a].tags.disc ? g_meta[a].tags.disc : 1, db = g_meta[b].tags.disc ? g_meta[b].tags.disc : 1;
            if (da != db) return da < db;
            if (g_meta[a].tags.track != g_meta[b].tags.track) return g_meta[a].tags.track < g_meta[b].tags.track;
        }
        return StrCmpLogicalW(NameOf(g_fl.paths[a]).c_str(), NameOf(g_fl.paths[b]).c_str()) < 0;
    });
    for (int r = 0; r < n; r++) { Meta& m = g_meta[ord[r]]; if (m.no != r + 1) { m.no = r + 1; g_rows[ord[r]].cell[LC_NO] = std::to_wstring(m.no); } }
    int col = g_cols.sortCol; bool desc = g_cols.desc;
    std::vector<int> perm(n); for (int i = 0; i < n; i++) perm[i] = i;
    std::vector<char> miss(n); for (int i = 0; i < n; i++) miss[i] = Missing(i, col);
    std::stable_sort(perm.begin(), perm.end(), [&](int a, int b) {
        if (miss[a] != miss[b]) return !miss[a];                   // 値が無い曲は最後（向きに関わらず）
        if (!miss[a]) { int c = CmpCol(a, b, col); if (c != 0) return desc ? c > 0 : c < 0; }
        return g_meta[a].no < g_meta[b].no;                        // 同じ値どうしは # の順
    });
    bool same = true; for (int i = 0; i < n; i++) if (perm[i] != i) same = false;
    if (same) return false;
    std::vector<std::wstring> np(n); std::vector<Row> nr(n); std::vector<Meta> nm(n); std::vector<int> inv(n);
    for (int i = 0; i < n; i++) { np[i] = std::move(g_fl.paths[perm[i]]); nr[i] = std::move(g_rows[perm[i]]); nm[i] = std::move(g_meta[perm[i]]); inv[perm[i]] = i; }
    g_fl.paths.swap(np); g_rows.swap(nr); g_meta.swap(nm);
    if (g_cur >= 0) g_cur = inv[g_cur];
    if (g_sel >= 0) g_sel = inv[g_sel];
    RebuildSidPos();
    g_scroll = std::max(0, std::min(g_scroll, ui::MaxScroll(n)));
    Trace(L"sort: column %d %s (play order follows)", col, desc ? L"desc" : L"asc");
    if (audio::Loaded()) PrepareNext();   // 先読みしてある次の曲が変わったら、やり直す
    return true;
}
// フォルダを開いたときは、先頭の曲を決める前に、タグだけ先に読んで並べる（曲を開いたときは裏で読んで後から並べ替える）
static void QuickReadTagsAndSort() {
    for (size_t i = 0; i < g_fl.paths.size(); i++) { ReadTags(g_fl.paths[i], g_meta[i].tags, false); g_meta[i].scanned = false; g_rows[i].name = TrackName((int)i); UpdateCells((int)i); }
    ResortAll();
}

static void OnScan(ScanItem* raw) {
    std::unique_ptr<ScanItem> it(raw);
    if (it->gen != g_listGen || it->idx < 0 || it->idx >= (int)g_sidPos.size()) return;
    int pos = g_sidPos[it->idx];   // 並べ替えで位置が変わっていても、始めたときの番号から今の位置に戻す
    if (pos < 0 || pos >= (int)g_meta.size()) return;
    Meta& m = g_meta[pos];
    m.tags = it->t; m.scanned = true; m.codec = it->codec; m.rate = it->rate;
    g_rows[pos].name = TrackName(pos);
    if (it->dur > 0 && g_rows[pos].dur <= 0) g_rows[pos].dur = it->dur;
    if (g_rows[pos].dur > 0 && m.size > 0) m.kbps = (int)std::lround(m.size * 8.0 / g_rows[pos].dur / 1000);
    UpdateCells(pos);
    g_sortDirty = true;
    InvalidateChrome();
    if (pos == g_cur) RefreshInfo();
}
static void OnScanDone(unsigned gen) {
    if (gen != g_listGen) return;
    KillTimer(g_hwnd, TM_SORT); g_sortDirty = false;
    for (size_t i = 0; i < g_rows.size(); i++) { g_rows[i].name = TrackName((int)i); UpdateCells((int)i); }
    ResortAll();
    PrepareNext();
    RefreshInfo(); InvalidateChrome();
}

enum PlayResult { PR_OK, PR_OFF_END, PR_ALL_BAD };

static bool AllBad() { for (auto& r : g_rows) if (!r.bad) return false; return !g_rows.empty(); }

// idx の曲を開いて鳴らす（play = false なら頭で止めて待つ）。開けなければ灰色にして dir の向きに次へ進む。
// 端を越えたら（リピート無しのとき）PR_OFF_END。一覧のすべてが開けなければ PR_ALL_BAD（停止して知らせる）
static PlayResult PlayIndex(int idx, bool play, int dir) {
    int n = (int)g_rows.size();
    for (int tries = 0; tries < n; tries++) {
        if (idx < 0 || idx >= n) break;
        if (audio::Open(g_fl.paths[idx], play)) {
            g_rows[idx].bad = false;
            g_rows[idx].dur = audio::Duration();
            g_cur = g_sel = idx; g_err = false; g_msg = 0;
            EnsureVisible(idx);
            Trace(L"play: #%d %s", idx + 1, NameOf(g_fl.paths[idx]).c_str());
            OnTrackStarted();
            RefreshUI();
            return PR_OK;
        }
        g_rows[idx].bad = true;
        Trace(L"play: #%d can't play, skip", idx + 1);
        int nx = idx + dir;
        if ((nx < 0 || nx >= n) && g_repeat == settings::R_ALL) nx = (nx % n + n) % n;
        idx = nx;
    }
    if (AllBad()) {
        audio::Close(); g_cur = -1; g_err = true;
        Trace(L"play: all files can't be played");
        RefreshUI();
        return PR_ALL_BAD;
    }
    RefreshUI();
    return PR_OFF_END;
}

// 最後まで行って止まる：一覧の先頭の曲を選んだ状態（頭で停止）に戻す
static void StopAtEnd() {
    int f = 0;
    for (int i = 0; i < (int)g_rows.size(); i++) if (!g_rows[i].bad) { f = i; break; }
    PlayIndex(f, false, +1);
}

static void OnEnded(unsigned gen) {
    if (gen != audio::Generation() || !ValidCur()) return;   // 古い知らせ
    int n = (int)g_rows.size();
    Trace(L"ended: #%d repeat=%d", g_cur + 1, g_repeat);
    if (g_repeat == settings::R_ONE) { PlayIndex(g_cur, true, +1); return; }
    if (g_repeat == settings::R_TRACK) { PlayIndex(g_cur, false, +1); return; }   // Stop after track：選んだ曲のまま頭に戻って止まる
    int nx = g_cur + 1;
    if (nx >= n) {
        if (g_repeat == settings::R_ALL) nx = 0;
        else { StopAtEnd(); return; }
    }
    if (PlayIndex(nx, true, +1) == PR_OFF_END) StopAtEnd();
}

static void DoNext() {
    int n = (int)g_rows.size();
    if (!n) return;
    int nx = g_cur + 1;
    if (nx >= n) { if (g_repeat == settings::R_ALL) nx = 0; else return; }
    PlayIndex(nx, true, +1);
}

// 今の曲の 3 秒より後なら頭に戻る。3 秒以内なら前の曲へ（先頭ならリピートなしは頭出し、フォルダのリピートは最後の曲へ）
static void DoPrev() {
    int n = (int)g_rows.size();
    if (!n) return;
    bool loaded = audio::Loaded();
    int px = g_cur - 1;
    if (loaded && audio::Position() > 3.0) { Trace(L"prev: back to head (pos %.2f)", audio::Position()); audio::Seek(0); RefreshUI(); return; }
    if (px < 0) {
        if (g_repeat == settings::R_ALL) px = n - 1;
        else { Trace(L"prev: first track, back to head"); if (loaded) audio::Seek(0); RefreshUI(); return; }
    }
    Trace(L"prev: to track #%d", px + 1);
    PlayIndex(px, true, -1);
}

static void SetList(const FileList& fl) {
    g_fl = fl;
    g_rows.assign(fl.paths.size(), Row());
    for (size_t i = 0; i < fl.paths.size(); i++) g_rows[i].name = StemOf(fl.paths[i]);
    g_cur = g_sel = -1; g_scroll = 0; g_err = false; g_msg = 0;
    g_meta.assign(fl.paths.size(), Meta()); g_listGen++; g_nextIdx = -1; g_cover = Image(); g_coverGen++;
    for (size_t i = 0; i < fl.paths.size(); i++) {   // 大きさと更新日時は、フォルダを読むときにファイルの属性から取る
        g_meta[i].no = (int)i + 1; g_meta[i].sid = (int)i;
        uint64_t sz0 = 0, md0 = 0;
        if (zipfs::FileStat(fl.paths[i], sz0, md0)) { g_meta[i].size = sz0; g_meta[i].mod = md0; }   // ZIP の中なら、展開後の大きさと ZIP の中の日時
    }
    RebuildSidPos();
    for (size_t i = 0; i < fl.paths.size(); i++) UpdateCells((int)i);
}

// 曲かフォルダを開く。曲ならそのフォルダの一覧を作ってその曲から、フォルダなら先頭の曲から自動で鳴らす
static void ExpShowFolder(const std::wstring& dir, const std::wstring& select);
static void OpenPath(const std::wstring& path, bool autoplay = true) {
    DWORD need = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!need) return;
    std::vector<wchar_t> buf(need);
    DWORD got = GetFullPathNameW(path.c_str(), need, buf.data(), nullptr);
    if (!got || got >= need) return;
    std::wstring full(buf.data(), got);
    bool isDir;
    std::wstring zipFile, zipInner;
    bool inZip = zipfs::SplitPath(full, zipFile, zipInner);   // ZIP そのもの、または ZIP の中（第5c段）
    if (inZip) {
        auto ar = zipfs::Open(zipFile);
        if (!ar) { g_msg = 2; Trace(L"open: bad zip %s", zipFile.c_str()); RefreshUI(); InvalidateChrome(); return; }
        int kind = ar->Kind(zipInner);
        if (!kind) return;
        isDir = kind == 2;
        if (zipInner.empty()) {   // ZIP を開いた：曲が直下にあるいちばん浅いフォルダを開く
            std::wstring d;
            if (ar->ShallowestAudioDir(d)) full = d.empty() ? zipFile : zipFile + L"\\" + d;
            else {   // 曲が1つも無い：空の一覧と No audio files
                ExpShowFolder(zipFile, std::wstring());
                audio::Close(); FileList empty; empty.dir = zipFile; SetList(empty); StartScan();
                g_msg = 1; RefreshUI(); InvalidateChrome();
                return;
            }
        }
    } else {
        DWORD attr = GetFileAttributesW(full.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES) return;
        isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    std::wstring dir = isDir ? full : DirOf(full);
    while (dir.size() > 3 && (dir.back() == L'\\' || dir.back() == L'/')) dir.pop_back();
    if (!isDir && dir.size() == 2 && dir[1] == L':') dir += L'\\';
    ExpShowFolder(dir, isDir ? std::wstring() : full);   // エクスプローラも、開いたフォルダに移る
    FileList fl; BuildList(dir, fl);
    int start = 0;
    if (!isDir) {
        start = IndexOf(fl, full);
        if (start < 0) { fl.paths.push_back(full); start = (int)fl.paths.size() - 1; }   // 対応形式でない拡張子の曲も、指されたら入れる
    }
    audio::Close();
    SetList(fl);
    if (isDir) QuickReadTagsAndSort();   // フォルダは、先頭の曲を決める前にタグだけ先に読んで並べる
    else { ResortAll(); start = std::max(0, IndexOf(g_fl, full)); }   // 並べ替えの設定（前回の列）に従った順にしてから、その曲の位置を探す
    StartScan();
    Trace(L"open: %s (%d tracks, start #%d)", full.c_str(), (int)fl.paths.size(), start + 1);
    if (fl.paths.empty()) { g_err = false; if (inZip) g_msg = 1; RefreshUI(); InvalidateChrome(); return; }
    PlayIndex(start, autoplay, +1);   // 起動時に前回の場所を開くときは autoplay = false（鳴らさずに選ぶだけ）
}

// 起動時に前回の場所を開く：その曲のフォルダを開いて曲を選ぶ。鳴らさない。曲が消えていればフォルダの先頭、フォルダも無ければ何もしない（空で起動）
static void OpenLastPlace(const std::wstring& path) {
    {   // ZIP の中の曲：ZIP の中のそのフォルダに戻る。曲が無ければ中のフォルダの先頭。そのフォルダも無ければ空で起動
        std::wstring zf, zi;
        if (zipfs::SplitPath(path, zf, zi)) {
            auto ar = zipfs::Open(zf);
            if (!ar || zi.empty()) return;
            if (ar->Kind(zi) == 1) { OpenPath(path, false); return; }
            std::wstring dir = DirOf(path);
            std::wstring z2, di; zipfs::SplitPath(dir, z2, di);
            if (ar->Kind(di) == 2 && !di.empty()) OpenPath(dir, false);
            else if (!zi.empty() && di.empty() && ar->Kind(di) == 2) OpenPath(zf, false);   // 根にあった曲が消えた：ZIP を開き直す
            return;
        }
    }
    DWORD a = GetFileAttributesW(path.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) { OpenPath(path, false); return; }
    std::wstring dir = DirOf(path);
    DWORD d = GetFileAttributesW(dir.c_str());
    if (d != INVALID_FILE_ATTRIBUTES && (d & FILE_ATTRIBUTE_DIRECTORY)) OpenPath(dir, false);
}

void OnFilesDropped(HDROP h) {
    UINT n = DragQueryFileW(h, 0, nullptr, 0);   // 文字数（終端を除く）を先に聞く。最初の1つだけ使う
    if (n) {
        std::vector<wchar_t> p((size_t)n + 1);
        UINT got = DragQueryFileW(h, 0, p.data(), n + 1);
        if (got) OpenPath(std::wstring(p.data(), got));
    }
    DragFinish(h);
}

static void DoOpenDialog() {
    wchar_t buf[MAX_PATH * 2] = L"";
    OPENFILENAMEW o = { sizeof o };
    o.hwndOwner = g_hwnd;
    std::wstring filter = OpenFileFilter();
    o.lpstrFilter = filter.c_str();
    o.lpstrFile = buf; o.nMaxFile = MAX_PATH * 2; o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&o)) OpenPath(buf);
}

static void DoOpenFolderDialog() {
    ComPtr<IFileOpenDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return;
    DWORD opt = 0; d->GetOptions(&opt);
    d->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (FAILED(d->Show(g_hwnd))) return;
    ComPtr<IShellItem> it;
    if (FAILED(d->GetResult(&it)) || !it) return;
    PWSTR p = nullptr;
    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { OpenPath(p); CoTaskMemFree(p); }
}

// ---------------------------------------------------------------- エクスプローラ（第4d段）
// フォルダの中は裏のスレッドで読み、できたら WM_EXPLIST で受ける（遅いネットワークドライブで固まらない）。古い要求の結果は捨てる
static const UINT WM_EXPLIST = WM_APP + 13;
struct ExpResult { unsigned gen; explorer::Listing l; std::wstring select; };
static unsigned g_expGen = 0;

static void ExpChanged() { RefreshUI(); InvalidateChrome(); }

static void ExpEnsureVisible(int i) {
    int vis = ui::ExpVisibleRows(), n = (int)g_expRows.size();
    if (i <= 1) g_expScroll = 0;   // いちばん上の近くなら、見出しまで見せる
    else if (i < g_expScroll) g_expScroll = i;
    else if (i >= g_expScroll + vis) g_expScroll = i - vis + 1;
    g_expScroll = std::max(0, std::min(g_expScroll, ui::ExpMaxScroll(n)));
}
static void ExpScrollTo(int s) {
    s = std::max(0, std::min(s, ui::ExpMaxScroll((int)g_expRows.size())));
    if (s != g_expScroll) { g_expScroll = s; ExpChanged(); }
}
static bool ExpSelectable(int i) { return i >= 1 && i < (int)g_expRows.size() && g_expRows[i].kind != 4; }

// 中身の行を作る。いちばん上に今のフォルダ、その下に ..（ドライブの一覧のときは無し）、フォルダ、曲。select の行があればそれを選ぶ（無ければ最初の枝）
static void ExpBuild(const explorer::Listing& l, const std::wstring& select) {
    g_expRows.clear();
    ExpRow h; h.kind = 0; h.name = l.dir.empty() ? lang::T(lang::EXP_DRIVES) : explorer::TitleOf(l.dir); h.path = l.dir; g_expRows.push_back(h);
    if (!l.dir.empty()) { ExpRow u; u.kind = 1; u.name = L".."; u.path = explorer::ParentOf(l.dir); g_expRows.push_back(u); }
    if (l.failed) { ExpRow m; m.kind = 4; m.name = lang::T(lang::EXP_CANT); g_expRows.push_back(m); }
    for (auto& it : l.items) { ExpRow r; r.kind = (it.folder || it.zip) ? 2 : 3; r.zip = it.zip; r.name = it.name; r.path = it.path; g_expRows.push_back(r); }
    g_expRows.back().last = true;
    int sel = -1;
    if (!select.empty()) for (int i = 1; i < (int)g_expRows.size(); i++) if (g_expRows[i].kind != 4 && SamePath(g_expRows[i].path, select)) { sel = i; break; }
    if (sel < 0) { int first = l.dir.empty() ? 1 : 2; sel = ExpSelectable(first) ? first : (ExpSelectable(1) ? 1 : -1); }
    g_expSel = sel; g_expScroll = 0;
    if (sel >= 0) ExpEnsureVisible(sel);
}

static void ExpRequest(const std::wstring& dir, const std::wstring& select) {
    unsigned gen = ++g_expGen; HWND hw = g_hwnd;
    g_expDir = dir; g_expHave = true;
    std::thread([dir, select, gen, hw] {
        ExpResult* r = new ExpResult(); r->gen = gen; r->select = select;
        explorer::Read(dir, r->l);
        if (!PostMessageW(hw, WM_EXPLIST, 0, (LPARAM)r)) delete r;
    }).detach();
}

// 曲を開いたとき（OpenPath）に、そのフォルダへ移る。すでにそのフォルダを見ていれば読み直さず、曲の行を選ぶだけ
static void ExpShowFolder(const std::wstring& dir, const std::wstring& select) {
    if (g_expHave && !g_expRows.empty() && SamePath(dir, g_expDir)) {
        for (int i = 1; i < (int)g_expRows.size(); i++) if (g_expRows[i].kind == 3 && !select.empty() && SamePath(g_expRows[i].path, select)) { g_expSel = i; ExpEnsureVisible(i); break; }
        ExpChanged(); return;
    }
    ExpRequest(dir, select);
}

static void OnExpList(ExpResult* raw) {
    std::unique_ptr<ExpResult> r(raw);
    if (r->gen != g_expGen) return;   // 古い要求の結果
    ExpBuild(r->l, r->select);
    Trace(L"explorer: %s (%d rows%s)", r->l.dir.empty() ? L"<drives>" : r->l.dir.c_str(), (int)g_expRows.size(), r->l.failed ? L", cannot open" : L"");
    ExpChanged();
}

static void ExpUp() {
    if (!g_expHave || g_expDir.empty()) return;   // ドライブの一覧より上は無い
    ExpRequest(explorer::ParentOf(g_expDir), g_expDir);   // 上がったら、今いたフォルダを選んでおく
}
// Enter / ダブルクリック：.. は上へ、フォルダは中に入るだけ（鳴っている曲と一覧は変えない）、曲はそのフォルダを開いてその曲から再生
static void ExpActivate() {
    if (!ExpSelectable(g_expSel)) return;
    ExpRow r = g_expRows[g_expSel];   // OpenPath が行を作り直すので、コピーを使う
    switch (r.kind) {
    case 1: ExpUp(); break;
    case 2: Trace(L"explorer: enter %s", r.path.c_str()); ExpRequest(r.path, L""); break;
    case 3: OpenPath(r.path); break;
    }
}
static void ExpMove(int d) {
    int n = (int)g_expRows.size(), i = std::max(g_expSel, 0);
    for (i += d; i >= 1 && i < n; i += d) if (g_expRows[i].kind != 4) break;
    if (i < 1 || i >= n) return;
    g_expSel = i; ExpEnsureVisible(i); ExpChanged();
}

// ---------------------------------------------------------------- 音量・シーク
static void ChangeVolume(int delta) {
    int v = std::max(0, std::min(100, audio::Volume() + delta));
    audio::SetVolume(v);
    if (delta > 0) audio::SetMuted(false);   // 上げたらミュート解除
    RefreshUI();
}
static void SeekRel(double sec) {
    if (!audio::Loaded()) return;
    double d = audio::Duration();
    double to = std::max(0.0, audio::Position() + sec);
    if (d > 0) to = std::min(to, d);
    audio::Seek(to);
    RefreshUI();
}

static int NextRepeat(int r) {
    switch (r) {
    case settings::R_ONE: return settings::R_ALL;
    case settings::R_ALL: return settings::R_STOP;
    case settings::R_STOP: return settings::R_TRACK;
    default: return settings::R_ONE;
    }
}
static void SetRepeat(int m) { Trace(L"repeat: %d", m); g_repeat = m; PrepareNext(); RefreshUI(); }

static void DoPlayPause() {
    if (!audio::Loaded()) {
        if (HaveList()) PlayIndex(g_sel >= 0 ? g_sel : 0, true, +1);
        return;
    }
    Trace(L"play/pause: now %s", audio::Playing() ? L"pausing" : L"playing");
    if (audio::Playing()) audio::Pause(); else audio::Play();
    RefreshUI();
}

static void DoSettings();
static void SetMiniMode(bool on);
static void ClampHScroll() { ui::SetHScroll(std::min(ui::HScroll(), ui::MaxHScroll())); }
// visualizer ボタンの左クリックと V：オン／オフだけ。オンにすると最後に使ったシェーダーに戻る（無ければ一覧の先頭。1本も無ければ何もしない）
static void ToggleVis() {
    if (vis::Active()) { g_visLast = vis::Current(); vis::Select(L"", false); return; }
    vis::Rescan();
    std::wstring target;
    for (int i = 0; i < vis::Count(); i++) if (!g_visLast.empty() && !_wcsicmp(vis::FileAt(i).c_str(), g_visLast.c_str())) target = vis::FileAt(i);
    if (target.empty() && vis::Count() > 0) target = vis::FileAt(0);
    if (!target.empty()) vis::Select(target, true);
}
static void ApplyTop();
static void ShowKeys();
static void Command(int id) {
    switch (id) {
    case ID_OPEN: DoOpenDialog(); break;
    case ID_OPENFOLDER: DoOpenFolderDialog(); break;
    case ID_SHOWEXP:
        if (ValidCur() || g_sel >= 0) {
            int i = ValidCur() ? g_cur : g_sel;
            std::wstring zp, zi, shown = g_fl.paths[i];
            if (zipfs::SplitPath(shown, zp, zi)) shown = zp;   // ZIP の中の曲は、ZIP ファイルを選んで見せる
            std::wstring a = L"/select,\"" + shown + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", a.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case ID_EXIT: SendMessageW(g_hwnd, WM_CLOSE, 0, 0); break;   // 設定を保存してから閉じる
    case ID_PLAY: DoPlayPause(); break;
    case ID_PREV: DoPrev(); break;
    case ID_NEXT: DoNext(); break;
    case ID_MUTE: audio::SetMuted(!audio::Muted()); RefreshUI(); break;
    case ID_SETTINGS: DoSettings(); break;
    case ID_MINI: SetMiniMode(!g_mini); break;
    case ID_TOP: if (g_mini) g_topM = !g_topM; else g_topN = !g_topN; ApplyTop(); break;
    case ID_KEYS: ShowKeys(); break;
    case ID_ABOUT: MessageBoxW(g_hwnd, lang::Sub(lang::ABOUT_BODY, TLAP_VER_WSTR).c_str(), lang::T(lang::ABOUT_TITLE), MB_OK); break;
    case ID_VIS_NONE: vis::Select(L"", true); break;
    case ID_VIS_RELOAD: vis::Rescan(); break;
    case ID_SHOWVIS: g_miniVis = !g_miniVis; DoLayout(); RefreshUI(); SaveSettings(); break;   // ミニ窓の下のスペアナ（Settings のチェックと同じ値）
    case ID_EQ: ShowEq(!(g_eqWnd && IsWindowVisible(g_eqWnd))); break;
    case ID_VIS: ToggleVis(); break;
    case ID_COL_RESET: g_cols.Reset(); ClampHScroll(); ResortAll(); DoLayout(); InvalidateChrome(); break;
    case ID_INFO: ShowInfo(!g_infoOn); break;
    case ID_COVER:   // ジャケットの欄の表示／非表示（ミニ窓には関係しない）
        g_coverOn = !g_coverOn; g_ui.coverOn = g_coverOn;
        if (g_coverOn) LoadCoverForCur(); else { g_coverId++; g_cover = Image(); g_coverGen++; }
        DoLayout(); RefreshUI(); InvalidateChrome();
        break;
    case ID_REPEAT: SetRepeat(NextRepeat(g_repeat)); break;   // Repeat one → Repeat folder → Stop at folder end → Stop after track → …
    case ID_REP_TRACK: SetRepeat(settings::R_TRACK); break;
    case ID_REP_STOP: SetRepeat(settings::R_STOP); break;
    case ID_REP_ALL: SetRepeat(settings::R_ALL); break;
    case ID_REP_ONE: SetRepeat(settings::R_ONE); break;
    default:
        if (id >= ID_COL_BASE && id < ID_COL_BASE + LC_COUNT) {   // 見出しの右クリック：列の出し入れ（Title は外せない）
            int c = id - ID_COL_BASE;
            if (c != LC_TITLE) { g_cols.on[c] = !g_cols.on[c]; ClampHScroll(); DoLayout(); InvalidateChrome(); }
            break;
        }
        if (id >= ID_VIS_BASE && id < ID_VIS_BASE + 1000) vis::Select(vis::FileAt(id - ID_VIS_BASE), true);   // View ▸ Visualizer ▸ 一覧（V と同じく名前を2秒出す）
        break;
    }
}

// ---------------------------------------------------------------- マウス（シークバー・音量・一覧）
static void SeekToX(int x) {
    double d = audio::Duration();
    if (!(d > 0)) return;
    g_dragT = ui::SeekFracAt(x) * d;
    RefreshUI();
}
static void EndSeekDrag(int x) {
    if (!g_seekDrag) return;
    SeekToX(x);
    g_seekDrag = false;
    ReleaseCapture();
    audio::Seek(g_dragT);   // 離したところへ
    RefreshUI();
}
static void ApplyVolumeSlider(int x) {
    int v = ui::VolumeAtX(x);
    audio::SetVolume(v);
    if (v > 0) audio::SetMuted(false);
    RefreshUI();
}
static void ScrollTo(int s) {
    s = std::max(0, std::min(s, ui::MaxScroll((int)g_rows.size())));
    if (s != g_scroll) { g_scroll = s; RefreshUI(); InvalidateChrome(); }
}

// ---------------------------------------------------------------- 窓
static void OpenMenu(int i) {
    RECT r = ui::MenuItemRect(i);
    POINT p = { r.left * g_scale, r.bottom * g_scale }; ClientToScreen(g_hwnd, &p);
    HMENU m = ui::BuildMenu(i, g_ui);
    g_ui.openMenu = i; InvalidateChrome(); UpdateWindow(g_hwnd);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    g_ui.openMenu = -1; g_ui.hoverMenu = -1; InvalidateChrome();
}

static HFONT g_tipFont = nullptr; static bool g_tipFontDirty = false;   // ツールチップの書体（言語を変えたら作り直す）
static void UpdateTips() {
    if (!g_tip) return;
    for (int i = 0; i < ui::ToolCount(); i++) {
        TOOLINFOW ti = { TTTOOLINFOW_V1_SIZE };   /* comctl32 v5 は V1 のサイズしか受けない */ ti.hwnd = g_hwnd; ti.uId = i;
        RECT r = ui::ToolRect(i);
        r = { r.left * g_scale, r.top * g_scale, r.right * g_scale, r.bottom * g_scale };
        if (!SendMessageW(g_tip, TTM_GETTOOLINFOW, 0, (LPARAM)&ti)) {
            ti.uFlags = TTF_SUBCLASS; ti.rect = r; ti.lpszText = (LPWSTR)ui::ToolTip(i);
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        } else { ti.rect = r; SendMessageW(g_tip, TTM_NEWTOOLRECTW, 0, (LPARAM)&ti); ti.lpszText = (LPWSTR)ui::ToolTip(i); SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti); }
    }
    // ツールチップも UI と同じ書体（倍率が変わったときだけ作り直す）
    static int tipScale = 0;
    if (!g_tipFont || tipScale != g_scale || g_tipFontDirty) {
        g_tipFontDirty = false;
        HFONT nf = ui::CreateUiFont(g_scale);
        SendMessageW(g_tip, WM_SETFONT, (WPARAM)nf, 0);
        if (g_tipFont) DeleteObject(g_tipFont);
        g_tipFont = nf; tipScale = g_scale;
    }
}

static bool MiniVisWanted() { return g_mini && g_miniVis && vis::Active(); }   // ミニ窓の下にスペアナを出すか（設定オンで、スペアナが None でないとき）

static void DoLayout() {
    RECT rc; GetClientRect(g_hwnd, &rc);
    int lw = rc.right / g_scale, lh = rc.bottom / g_scale;   // バー類は1倍の論理座標で扱う
    ui::SetMini(g_mini);
    if (g_mini) {   // ミニ窓：ツールチップ・情報パネルは出さない。スペアナは下に（設定オンで、スペアナが None でないとき）
        ui::SetMiniVis(MiniVisWanted());
        ui::SetPanel(false, 0);
        ShowWindow(g_infoEdit, SW_HIDE);
        int mw, mh; ui::MiniSize(mw, mh);
        if (rc.right != mw * g_scale || rc.bottom != mh * g_scale) {   // スペアナを出す・消すときは、窓の高さを変える（位置はそのまま）
            SetWindowPos(g_hwnd, nullptr, 0, 0, mw * g_scale, mh * g_scale, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            GetClientRect(g_hwnd, &rc); lw = rc.right / g_scale; lh = rc.bottom / g_scale;
        }
        ui::SetSize(lw, lh);
        RECT vr = ui::MiniVisRect();
        if (vr.right > vr.left) {
            vis::SetScale(g_scale);
            vis::Move(vr.left * g_scale, vr.top * g_scale, (vr.right - vr.left) * g_scale, (vr.bottom - vr.top) * g_scale);
        }
        vis::SetPaneVisible(vr.right > vr.left && !IsIconic(g_hwnd));
        if (g_tip) SendMessageW(g_tip, TTM_ACTIVATE, FALSE, 0);
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }
    if (g_tip) SendMessageW(g_tip, TTM_ACTIVATE, TRUE, 0);
    bool visShown = vis::Active() || !vis::Error().empty();
    ui::SetVis(visShown ? g_visH : 0, !vis::Error().empty());
    {   // 左の列と情報パネルの幅。保存値は変えず、窓が狭いときだけ表示を縮める（一覧の最小 200 を残し、先に左の列を最小 120 まで、次に情報パネルを縮める）
        int room = lw - kInfoViewMin, infoWanted = g_infoOn ? std::max(g_infoW, kInfoMin) : 0;
        g_colShown = std::max(kColMin, std::min(std::max(g_colW, kColMin), room - infoWanted));
        g_infoColW = g_infoOn ? std::max(1, std::min(infoWanted, room - g_colShown)) : 0;
        ui::SetCoverShown(g_coverOn); ui::SetCoverCol(g_colShown);
        ui::SetPanel(g_infoOn, std::max(1, g_infoColW));
    }
    ui::SetSize(lw, lh);
    {   // 情報パネルの EDIT：枠の内側（2px）に置く
        ui::Layout P = ui::Calc(lw, lh);
        if (g_infoOn && P.panel.right > P.panel.left) {
            SetWindowPos(g_infoEdit, nullptr, (P.panel.left + 2) * g_scale, (P.panel.top + 2) * g_scale,
                std::max(1, (int)(P.panel.right - P.panel.left - 4) * g_scale), std::max(1, (int)(P.panel.bottom - P.panel.top - 4) * g_scale), SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(g_infoEdit, SW_SHOWNA);
            RefreshInfo();
        } else ShowWindow(g_infoEdit, SW_HIDE);
    }
    {   // スペアナの欄（中身は子窓。物理px）。見えていないとき・最小化中は描かない
        ui::Layout L = ui::Calc(lw, lh);
        vis::SetScale(g_scale);
        if (L.vis.right > L.vis.left) vis::Move(L.vis.left * g_scale, L.vis.top * g_scale, (L.vis.right - L.vis.left) * g_scale, (L.vis.bottom - L.vis.top) * g_scale);
        vis::SetPaneVisible(L.vis.right > L.vis.left && vis::Active() && !IsIconic(g_hwnd));
    }
    g_scroll = std::max(0, std::min(g_scroll, ui::MaxScroll((int)g_rows.size())));
    g_expScroll = std::max(0, std::min(g_expScroll, ui::ExpMaxScroll((int)g_expRows.size())));
    ClampHScroll();
    UpdateTips();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

// マウス座標（クライアント）を、バーの論理座標へ
static POINT Lp(LPARAM l) { return POINT{ GET_X_LPARAM(l) / g_scale, GET_Y_LPARAM(l) / g_scale }; }

static void OnLButtonDown(LPARAM l) {
    int x = Lp(l).x, y = Lp(l).y;
    SetFocus(g_hwnd);
    g_expFocus = false;   // エクスプローラの枠をクリックしたときだけ、下で戻す
    if (g_mini) {   // ミニ窓：シークバー・prev play next だけ（ほかはドラッグで窓が動く）
        if (ui::HitSeek(x, y) && g_ui.loaded && g_ui.dur > 0) { g_seekDrag = true; SetCapture(g_hwnd); SeekToX(x); return; }
        if (ui::HitVolume(x, y)) { g_volDrag = true; SetCapture(g_hwnd); ApplyVolumeSlider(x); return; }
        int b = ui::HitMiniBtn(x, y);
        if (b >= 0 && (b >= 3 || g_ui.haveList)) { g_ui.miniPress = b; g_ui.miniHover = b; SetCapture(g_hwnd); InvalidateChrome(); }
        return;
    }
    if (ui::HitPanelSplit(x, y)) { g_infoSplitDrag = true; g_infoGrab = x - ui::PanelSplitLeft(); SetCapture(g_hwnd); return; }
    if (ui::HitCoverSplit(x, y)) { g_colSplitDrag = true; g_colGrab = x - ui::CoverSplitLeft(); SetCapture(g_hwnd); return; }
    int mn = ui::HitMenu(x, y);
    if (mn >= 0) { OpenMenu(mn); return; }
    if (ui::HitSeek(x, y) && g_ui.loaded && g_ui.dur > 0) {
        g_seekDrag = true;
        SetCapture(g_hwnd);
        SeekToX(x);
        return;
    }
    if (ui::HitVolume(x, y)) { g_volDrag = true; SetCapture(g_hwnd); ApplyVolumeSlider(x); return; }
    int t = ui::HitTool(x, y);
    if (t >= 0 && ui::ToolEnabled(t, g_ui)) { g_ui.pressTool = t; SetCapture(g_hwnd); InvalidateChrome(); return; }
    if (ui::HitSplit(x, y)) { g_splitDrag = true; SetCapture(g_hwnd); return; }
    if (ui::HitCover(x, y)) { ShowBigCover(); return; }
    if (ui::HitExp(x, y)) {   // エクスプローラ：クリックで選択、キー操作がこちらに向く
        g_expFocus = true;
        int en = (int)g_expRows.size(), esb = ui::HitExpScrollbar(x, y, g_expScroll, en);
        if (esb == 1) { g_expScrollDrag = true; g_expThumbGrab = y - ui::ExpThumbRect(g_expScroll, en).top; SetCapture(g_hwnd); }
        else if (esb == 2) ExpScrollTo(g_expScroll - ui::ExpVisibleRows());
        else if (esb == 3) ExpScrollTo(g_expScroll + ui::ExpVisibleRows());
        else { int er = ui::HitExpRow(x, y, g_expScroll, en); if (ExpSelectable(er)) g_expSel = er; }
        ExpChanged(); return;
    }
    {   // 曲の一覧の見出し：列の境目のドラッグで幅、クリックで並べ替え（もう一度で降順）。下の横スクロールバー
        int ce = ui::HitColEdge(x, y);
        if (ce >= 0) { g_colDrag = ce; g_colWGrab = x - (ui::ColLeft(ce) + g_cols.w[ce]); SetCapture(g_hwnd); return; }
        int hc = ui::HitHeaderCol(x, y);
        if (hc >= 0) {
            if (g_cols.sortCol == hc) g_cols.desc = !g_cols.desc; else { g_cols.sortCol = hc; g_cols.desc = false; }
            ResortAll(); EnsureVisible(std::max(0, g_cur)); RefreshUI(); InvalidateChrome(); return;
        }
        if (ui::HitHeader(x, y)) return;
        int hs = ui::HitHScrollbar(x, y);
        if (hs == 1) { g_hscrollDrag = true; g_hGrab = x - ui::HThumbRect().left; SetCapture(g_hwnd); return; }
        if (hs == 2) { ui::SetHScroll(ui::HScroll() - 120); ClampHScroll(); InvalidateChrome(); return; }
        if (hs == 3) { ui::SetHScroll(ui::HScroll() + 120); ClampHScroll(); InvalidateChrome(); return; }
    }
    int n = (int)g_rows.size();
    int sb = ui::HitScrollbar(x, y, g_scroll, n);
    if (sb == 1) { g_scrollDrag = true; g_thumbGrab = y - ui::ThumbRect(g_scroll, n).top; SetCapture(g_hwnd); return; }
    if (sb == 2) { ScrollTo(g_scroll - ui::VisibleRows()); return; }
    if (sb == 3) { ScrollTo(g_scroll + ui::VisibleRows()); return; }
    int row = ui::HitRow(x, y, g_scroll, n);
    if (row >= 0) { g_sel = row; RefreshUI(); InvalidateChrome(); }
}

// ---------------------------------------------------------------- キー（修飾キーの状態は引数で受ける。WM_KEYDOWN からは GetKeyState で渡す）
static void HandleKey(UINT vk, bool ctrl, bool shift, bool fresh) {
    if (ctrl) {
        if (!fresh) return;
        switch (vk) {
        case 'O': Command(shift ? ID_OPENFOLDER : ID_OPEN); break;
        case 'M': Command(ID_MINI); break;
        case 'T': Command(ID_TOP); break;
        case VK_OEM_COMMA: Command(ID_SETTINGS); break;
        }
        return;
    }
    if (g_expFocus && !g_mini) {   // エクスプローラにフォーカスがある間：↑ ↓ は選択の移動、Enter は開く、Backspace は ..（ほかのキーは今までどおり）
        switch (vk) {
        case VK_UP: ExpMove(-1); return;
        case VK_DOWN: ExpMove(+1); return;
        case VK_RETURN: if (fresh) ExpActivate(); return;
        case VK_BACK: if (fresh) ExpUp(); return;
        }
    }
    switch (vk) {
    case VK_SPACE: if (fresh) Command(ID_PLAY); break;
    case VK_RIGHT: if (shift) SeekRel(5); else if (fresh) DoNext(); break;
    case VK_LEFT: if (shift) SeekRel(-5); else if (fresh) DoPrev(); break;
    case VK_UP: ChangeVolume(5); break;
    case VK_DOWN: ChangeVolume(-5); break;
    case 'M': if (fresh) Command(ID_MUTE); break;
    case 'R': if (fresh) Command(ID_REPEAT); break;
    case 'I': if (fresh) Command(ID_INFO); break;
    case 'C': if (fresh) Command(ID_COVER); break;
    case 'E': if (fresh) Command(ID_EQ); break;
    case 'V': if (fresh) Command(ID_VIS); break;
#ifdef TLAP_TRACE
    case VK_F12: vis::DebugLoseDevice(); break;   // 確認用：デバイス消失からの復帰処理を手動で呼ぶ（GPU は固めない）
#endif
    case VK_RETURN: if (fresh && g_sel >= 0 && g_sel < (int)g_rows.size()) PlayIndex(g_sel, true, +1); break;
    }
}

// ---------------------------------------------------------------- メディアキー（RegisterHotKey。窓が後ろにあっても効く）
// id: 1 再生/一時停止, 2 次, 3 前, 4 停止（一時停止して頭に戻す）
static void DoMediaKey(int id) {
    Trace(L"media key: %d", id);
    switch (id) {
    case 1: Command(ID_PLAY); break;
    case 2: DoNext(); break;
    case 3: DoPrev(); break;
    case 4: if (audio::Loaded()) { audio::Pause(); audio::Seek(0); RefreshUI(); } break;
    }
}

// ---------------------------------------------------------------- Always on Top・ミニ窓
static void ApplyTop() {
    SetWindowPos(g_hwnd, OnTopNow() ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    g_ui.onTop = OnTopNow(); Trace(L"always on top: %d (%s)", (int)OnTopNow(), g_mini ? L"mini" : L"normal"); RefreshUI(); InvalidateChrome();
}

// ふつうの窓 <-> ミニ窓。位置は別々に覚える。ミニ窓のあいだは、スペアナ・EQ パネル・情報パネルを隠し、戻ったら元の開閉に戻す
static void SetMiniMode(bool on) {
    if (on == g_mini) return;
    if (on) {
        g_normPlace.length = sizeof g_normPlace; GetWindowPlacement(g_hwnd, &g_normPlace); g_havePlace = true;
        g_eqWas = g_eqWnd && IsWindowVisible(g_eqWnd);
        if (g_eqWas) ShowEq(false);
        if (g_infoEdit && GetFocus() == g_infoEdit) SetFocus(g_hwnd);
        if (g_coverWnd) DestroyWindow(g_coverWnd);
        g_mini = true; ui::SetMini(true); ui::SetMiniVis(MiniVisWanted());
        int mw, mh; ui::MiniSize(mw, mh); int pw = mw * g_scale, ph = mh * g_scale;
        int x = g_miniX, y = g_miniY; RECT r = { x, y, x + pw, y + ph };
        if (x == INT_MIN || !MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) { x = g_normPlace.rcNormalPosition.left + 20 * g_scale; y = g_normPlace.rcNormalPosition.top + 60 * g_scale; }
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
        SetWindowPos(g_hwnd, g_topM ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, pw, ph, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        RECT r; GetWindowRect(g_hwnd, &r); g_miniX = r.left; g_miniY = r.top;
        g_mini = false; ui::SetMini(false);
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        if (g_havePlace) SetWindowPlacement(g_hwnd, &g_normPlace);
        SetWindowPos(g_hwnd, g_topN ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
        if (g_eqWas) ShowEq(true);
    }
    g_ui.mini = g_mini; g_ui.onTop = OnTopNow();
    Trace(L"mini window: %d", (int)g_mini);
    DoLayout(); RefreshUI(); InvalidateChrome();
}

// ---------------------------------------------------------------- Settings（Language：この段は English だけ）
static bool g_setDone = false, g_setOk = false;
static bool PickFolder(HWND owner, std::wstring& out) {
    ComPtr<IFileOpenDialog> d;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
    DWORD opt = 0; d->GetOptions(&opt);
    d->SetOptions(opt | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (FAILED(d->Show(owner))) return false;
    ComPtr<IShellItem> it; PWSTR p = nullptr;
    if (FAILED(d->GetResult(&it)) || !it || FAILED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) || !p) return false;
    out = p; CoTaskMemFree(p); return true;
}
static LRESULT CALLBACK SetProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == 104) {   // Browse...：Default folder を選ぶ
            std::wstring p; if (PickFolder(h, p)) SetWindowTextW(GetDlgItem(h, 103), p.c_str());
            return 0;
        }
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) { g_setOk = LOWORD(w) == IDOK; g_setDone = true; return 0; } break;
    case WM_CLOSE: g_setDone = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void DestroyKeys();
// 表示言語を切り替える：書体を替え、メニュー・ツールチップ・EQ パネル・Keys をその場で作り直す（再起動しない）
static void ApplyLanguage(lang::Lang l) {
    if (l == lang::Get()) return;
    lang::Set(l);
    ui::RebuildFont(); SetInfoFont(); g_tipFontDirty = true;
    DestroyKeys();
    if (g_eqWnd) {   // EQ パネルは文字も書体も変わるので作り直す（開いていたら同じ位置にまた開く）
        bool wasOpen = IsWindowVisible(g_eqWnd) != 0; RECT r; GetWindowRect(g_eqWnd, &r);
        DestroyWindow(g_eqWnd); g_eqWnd = nullptr; if (g_eqFont) { DeleteObject(g_eqFont); g_eqFont = nullptr; }
        g_ui.eqOpen = false;
        if (wasOpen) { ShowEq(true); if (g_eqWnd) SetWindowPos(g_eqWnd, nullptr, r.left, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
    }
    ui::SetVisName(vis::CurrentName());
    g_infoShown.clear(); RefreshInfo();
    Trace(L"language: %s", lang::IniCode(l));
    DoLayout(); RefreshUI(); InvalidateChrome();
}

static void DoSettings() {
    WNDCLASSEXW wc = { sizeof wc }; wc.lpfnWndProc = SetProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TLAPSettings";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    int S = g_scale; RECT rc; GetWindowRect(g_hwnd, &rc);
    RECT cr = { 0, 0, 280 * S, 184 * S };
    AdjustWindowRectEx(&cr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME, L"TLAPSettings", lang::T(lang::SET_TITLE), WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rc.left + 60 * S, rc.top + 80 * S, cr.right - cr.left, cr.bottom - cr.top, g_hwnd, nullptr, wc.hInstance, nullptr);
    HFONT f = ui::CreateUiFont(S);
    auto mk = [&](const wchar_t* cls, const wchar_t* txt, DWORD style, int x, int y, int ww, int hh, int id) {
        HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x * S, y * S, ww * S, hh * S, d, (HMENU)(INT_PTR)id, wc.hInstance, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE); return c; };
    mk(L"STATIC", lang::T(lang::SET_LANGUAGE), 0, 14, 18, 80, 16, -1);
    HWND combo = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 100, 14, 160, 100, 100);
    for (int i = 0; i < lang::kCount; i++) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lang::NativeName((lang::Lang)i));
    SendMessageW(combo, CB_SETCURSEL, (int)lang::Get(), 0);
    HWND miniChk = mk(L"BUTTON", lang::T(lang::SET_MINIVIS), BS_AUTOCHECKBOX | WS_TABSTOP, 14, 46, 256, 20, 101);
    SendMessageW(miniChk, BM_SETCHECK, g_miniVis ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND lastChk = mk(L"BUTTON", lang::T(lang::SET_OPENLAST), BS_AUTOCHECKBOX | WS_TABSTOP, 14, 70, 256, 20, 102);
    SendMessageW(lastChk, BM_SETCHECK, g_openLast ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND defChk = mk(L"BUTTON", lang::T(lang::SET_DEFON), BS_AUTOCHECKBOX | WS_TABSTOP, 14, 94, 256, 20, 105);
    SendMessageW(defChk, BM_SETCHECK, g_defOn ? BST_CHECKED : BST_UNCHECKED, 0);
    HWND defEdit = mk(L"EDIT", g_defFolder.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 14, 118, 176, 20, 103);
    mk(L"BUTTON", lang::T(lang::SET_BROWSE), BS_PUSHBUTTON | WS_TABSTOP, 196, 117, 70, 22, 104);
    mk(L"BUTTON", lang::T(lang::BTN_OK), BS_DEFPUSHBUTTON | WS_TABSTOP, 100, 150, 76, 24, IDOK);
    mk(L"BUTTON", lang::T(lang::BTN_CANCEL), BS_PUSHBUTTON | WS_TABSTOP, 184, 150, 76, 24, IDCANCEL);
    g_setDone = false; g_setOk = false;
    EnableWindow(g_hwnd, FALSE);
    ShowWindow(d, SW_SHOW); SetFocus(combo);
    MSG msg;
    while (!g_setDone && GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!IsDialogMessage(d, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    bool newMiniVis = SendMessageW(miniChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int newLang = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    bool newOpenLast = SendMessageW(lastChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool newDefOn = SendMessageW(defChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring newDef; { int n = GetWindowTextLengthW(defEdit); newDef.resize((size_t)n + 1); n = GetWindowTextW(defEdit, &newDef[0], n + 1); newDef.resize((size_t)n); }
    while (!newDef.empty() && newDef.front() == L' ') newDef.erase(0, 1);
    while (!newDef.empty() && newDef.back() == L' ') newDef.pop_back();
    EnableWindow(g_hwnd, TRUE);
    DestroyWindow(d); DeleteObject(f);
    SetForegroundWindow(g_hwnd);
    if (g_setOk) { g_miniVis = newMiniVis; g_openLast = newOpenLast; g_defOn = newDefOn; g_defFolder = newDef; if (newLang >= 0 && newLang < lang::kCount) ApplyLanguage((lang::Lang)newLang); DoLayout(); RefreshUI(); SaveSettings(); }   // OK ですぐ反映し、ini に保存
}

// ---------------------------------------------------------------- Help ▸ Keys（操作の表を出す小さな窓）
static HWND g_keysWnd = nullptr;
static LRESULT CALLBACK KeysProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: { HWND e = GetWindow(h, GW_CHILD); if (e) MoveWindow(e, 0, 0, LOWORD(l), HIWORD(l), TRUE); return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_keysWnd = nullptr; return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) { DestroyWindow(h); return 0; } break;
    }
    return DefWindowProcW(h, m, w, l);
}
static void DestroyKeys() { if (g_keysWnd) DestroyWindow(g_keysWnd); }
static void ShowKeys() {
    if (g_keysWnd) { SetForegroundWindow(g_keysWnd); return; }
    WNDCLASSEXW wc = { sizeof wc }; wc.lpfnWndProc = KeysProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TLAPKeys";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);
    int S = g_scale; RECT rc; GetWindowRect(g_hwnd, &rc);
    RECT cr = { 0, 0, 380 * S, 290 * S };
    AdjustWindowRectEx(&cr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    g_keysWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"TLAPKeys", lang::T(lang::KEYS_TITLE), WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rc.left + 50 * S, rc.top + 70 * S, cr.right - cr.left, cr.bottom - cr.top, g_hwnd, nullptr, wc.hInstance, nullptr);
    HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY, 0, 0, 10, 10, g_keysWnd, nullptr, wc.hInstance, nullptr);
    static HFONT f = nullptr; if (f) DeleteObject(f); f = ui::CreateUiFont(S);
    SendMessageW(e, WM_SETFONT, (WPARAM)f, TRUE);
    UINT tab = 110; SendMessageW(e, EM_SETTABSTOPS, 1, (LPARAM)&tab);   // 表の列をそろえる（ダイアログ単位）
    std::wstring body = L"\r\n"; for (const wchar_t* p = lang::T(lang::KEYS_BODY); *p; p++) { if (*p == L'\n') body += L"\r\n"; else body += *p; }
    SetWindowTextW(e, body.c_str());
    RECT c2; GetClientRect(g_keysWnd, &c2); MoveWindow(e, 0, 0, c2.right, c2.bottom, TRUE);
    ShowWindow(g_keysWnd, SW_SHOW);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_hwnd = h;
        g_scale = std::max(1, (int)(GetDpiForWindow(h) / 96));
        ui::RebuildFont();
        if (!audio::Init(h)) return -1;
        CreateInfoEdit(h);
        vis::Create(h);
        {   // メディアキー：RegisterHotKey（窓が後ろにあっても効く）。通らなかったキーは WM_APPCOMMAND で受ける
            static const UINT vks[4] = { VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK, VK_MEDIA_STOP };
            static const wchar_t* names[4] = { L"play/pause", L"next", L"previous", L"stop" };
            for (int i = 0; i < 4; i++) { g_hotOk[i] = RegisterHotKey(h, i + 1, MOD_NOREPEAT, vks[i]) != 0; Trace(L"hotkey %s: %s", names[i], g_hotOk[i] ? L"registered" : L"FAILED (WM_APPCOMMAND fallback)"); }
        }
        g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, h, nullptr, GetModuleHandleW(nullptr), nullptr);
        return 0;
    case WM_SIZE:
        vis::SetMinimized(w == SIZE_MINIMIZED);
        if (w != SIZE_MINIMIZED) DoLayout();
        RefreshUI();
        return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO* mi = (MINMAXINFO*)l;
        if (g_mini) { int mw, mh; ui::MiniSize(mw, mh); mi->ptMinTrackSize = mi->ptMaxTrackSize = POINT{ mw * g_scale, mh * g_scale }; return 0; }
        {
            RECT r = { 0, 0, ui::MinClientWidth() * g_scale, 0 };
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_ACCEPTFILES, GetDpiForWindow(h));
            mi->ptMinTrackSize.x = r.right - r.left;
        }
        mi->ptMinTrackSize.y = 240 * g_scale; return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int lw = rc.right / g_scale, lh = rc.bottom / g_scale;
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, lw, lh);
        HGDIOBJ old = SelectObject(mem, bmp);
        ui::Paint(mem, lw, lh, g_ui);
        // 物理解像度の裏バッファに、1倍の絵を最近傍で整数倍に拡大（文字も滲まない）→ ジャケットを重ねる → 画面へ1回だけ転送
        HDC back = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldb = SelectObject(back, bb);
        if (g_scale == 1) BitBlt(back, 0, 0, lw, lh, mem, 0, 0, SRCCOPY);
        else { SetStretchBltMode(back, COLORONCOLOR); StretchBlt(back, 0, 0, lw * g_scale, lh * g_scale, mem, 0, 0, lw, lh, SRCCOPY); }
        // 割り切れない端数の余白は面の色で塗る
        HBRUSH fb = CreateSolidBrush(RGB(0xc0, 0xc0, 0xc0));
        RECT e1 = { lw * g_scale, 0, rc.right, rc.bottom }, e2 = { 0, lh * g_scale, rc.right, rc.bottom };
        FillRect(back, &e1, fb); FillRect(back, &e2, fb); DeleteObject(fb);
        {   // ジャケットは物理ピクセルで描く
            RECT cr = g_mini ? ui::MiniCoverRect() : ui::CoverRect();
            if (cr.right > cr.left) { RECT pr = { cr.left * g_scale, cr.top * g_scale, cr.right * g_scale, cr.bottom * g_scale }; DrawCoverCached(back, pr); }
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, back, 0, 0, SRCCOPY);
        SelectObject(back, oldb); DeleteObject(bb); DeleteDC(back);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DPICHANGED: {
        g_scale = std::max(1, (int)(HIWORD(w) / 96));
        ui::RebuildFont(); SetInfoFont();
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        DoLayout();
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = Lp(l).x, y = Lp(l).y;
        if (g_mini && g_volDrag) { ApplyVolumeSlider(x); return 0; }
        if (g_mini && !g_seekDrag) {
            int b = ui::HitMiniBtn(x, y); if (b >= 0 && b < 3 && !g_ui.haveList) b = -1;
            if (b != g_ui.miniHover) { g_ui.miniHover = b; InvalidateChrome(); }
            TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
            return 0;
        }
        if (g_infoSplitDrag) {   // 情報パネルの仕切り：右端（仕切りの右）が マウス x - 掴んだ位置 + 4 になる幅
            RECT rc; GetClientRect(h, &rc); int lw = rc.right / g_scale;
            int nw = (lw - 2) - (x - g_infoGrab);   // 右端（余白2）から仕切りの左端まで
            nw = std::max(kInfoMin, std::min(nw, lw - kInfoViewMin - kColMin));   // 広げると左の列が先に縮む
            if (nw != g_infoW) { g_infoW = nw; DoLayout(); }
            return 0;
        }
        if (g_colSplitDrag) {   // 左の列の仕切り：仕切りの右端が マウス x - 掴んだ位置 + 4（左の余白 2 を引く）
            RECT rc; GetClientRect(h, &rc); int lw = rc.right / g_scale;
            int nw = (x - g_colGrab) + 4 - 2;
            nw = std::max(kColMin, std::min(nw, std::max(kColMin, lw - kInfoViewMin - g_infoColW)));
            if (nw != g_colW) { g_colW = nw; DoLayout(); }
            return 0;
        }
        if (g_expScrollDrag) { ExpScrollTo(ui::ExpScrollFromThumbTop(y - g_expThumbGrab, (int)g_expRows.size())); return 0; }
        if (g_colDrag >= 0) {   // 列の幅（最小 24、Title は 80）
            int c = g_colDrag, nw = x - g_colWGrab - ui::ColLeft(c);
            nw = std::max(ColMinW(c), std::min(nw, 2000));
            if (nw != g_cols.w[c]) { g_cols.w[c] = nw; ClampHScroll(); InvalidateChrome(); }
            return 0;
        }
        if (g_hscrollDrag) { ui::SetHScroll(ui::HScrollFromThumbLeft(x - g_hGrab)); InvalidateChrome(); return 0; }
        if (g_splitDrag) {   // 一覧とスペアナの境目をドラッグして、欄の高さを変える
            RECT rc; GetClientRect(h, &rc);
            ui::Layout L0 = ui::Calc(rc.right / g_scale, rc.bottom / g_scale);
            int bottom = L0.seek.top - 2, errH = vis::Error().empty() ? 0 : ui::ErrH();
            g_visH = std::max(32, std::min(600, bottom - y - 4 - 4 - errH));
            DoLayout(); return 0;
        }
        if (g_seekDrag) { SeekToX(x); return 0; }
        if (g_volDrag) { ApplyVolumeSlider(x); return 0; }
        if (g_scrollDrag) { ScrollTo(ui::ScrollFromThumbTop(y - g_thumbGrab, (int)g_rows.size())); return 0; }
        int t = ui::HitTool(x, y), mn = ui::HitMenu(x, y);
        if (t >= 0 && !ui::ToolEnabled(t, g_ui)) t = -1;
        if (t != g_ui.hoverTool || mn != g_ui.hoverMenu) { g_ui.hoverTool = t; g_ui.hoverMenu = mn; InvalidateChrome(); }
        TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 }; TrackMouseEvent(&te);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g_ui.miniHover != -1) { g_ui.miniHover = -1; InvalidateChrome(); }
        if (g_ui.hoverTool != -1 || g_ui.hoverMenu != -1) { g_ui.hoverTool = g_ui.hoverMenu = -1; InvalidateChrome(); }
        return 0;
    case WM_LBUTTONDOWN: OnLButtonDown(l); return 0;
    case WM_LBUTTONDBLCLK: {
        if (g_mini) { OnLButtonDown(l); return 0; }
        int x = Lp(l).x, y = Lp(l).y;
        { int er = ui::HitExpRow(x, y, g_expScroll, (int)g_expRows.size());
          if (er >= 0) { g_expFocus = true; if (ExpSelectable(er)) { g_expSel = er; ExpActivate(); } ExpChanged(); return 0; } }   // エクスプローラの行：フォルダなら中に入るだけ、曲ならそのフォルダの曲から再生
        int row = ui::HitRow(x, y, g_scroll, (int)g_rows.size());
        if (row >= 0) { g_sel = row; PlayIndex(row, true, +1); }   // 行をダブルクリック：その曲を再生
        else OnLButtonDown(l);
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_seekDrag) { EndSeekDrag(Lp(l).x); return 0; }
        if (g_mini && g_ui.miniPress >= 0) {
            int b = ui::HitMiniBtn(Lp(l).x, Lp(l).y), p = g_ui.miniPress;
            g_ui.miniPress = -1; ReleaseCapture(); InvalidateChrome();
            if (b == p) { switch (p) { case 0: DoPrev(); break; case 1: Command(ID_PLAY); break; case 2: DoNext(); break; case 3: Command(ID_REPEAT); break; case 4: Command(ID_MUTE); break; case 5: Command(ID_MINI); break; } }
            return 0;
        }
        if (g_infoSplitDrag) { g_infoSplitDrag = false; ReleaseCapture(); return 0; }
        if (g_colSplitDrag) { g_colSplitDrag = false; ReleaseCapture(); return 0; }
        if (g_expScrollDrag) { g_expScrollDrag = false; ReleaseCapture(); return 0; }
        if (g_colDrag >= 0) { g_colDrag = -1; ReleaseCapture(); DoLayout(); return 0; }
        if (g_hscrollDrag) { g_hscrollDrag = false; ReleaseCapture(); return 0; }
        if (g_splitDrag) { g_splitDrag = false; ReleaseCapture(); return 0; }
        if (g_volDrag) { ApplyVolumeSlider(Lp(l).x); g_volDrag = false; ReleaseCapture(); return 0; }
        if (g_scrollDrag) { g_scrollDrag = false; ReleaseCapture(); return 0; }
        if (g_ui.pressTool >= 0) {
            int t = ui::HitTool(Lp(l).x, Lp(l).y), p = g_ui.pressTool;
            g_ui.pressTool = -1; ReleaseCapture(); InvalidateChrome();
            if (t == p) Command(ui::ToolCmd(p));
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_ui.miniPress = -1;
        g_splitDrag = false; g_infoSplitDrag = false; g_colSplitDrag = false; g_volDrag = false; g_scrollDrag = false; g_expScrollDrag = false; g_colDrag = -1; g_hscrollDrag = false;
        if (g_ui.pressTool >= 0) { g_ui.pressTool = -1; InvalidateChrome(); }
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_KEYMENU) {
            // Alt+下線の文字でメニューを開く。Alt 単独は無視
            if (l) for (int i = 0; i < ui::MenuCount(); i++) if (towupper((wchar_t)l) == ui::MenuLetter(i)) { OpenMenu(i); break; }
            return 0;
        }
        break;
    case WM_NCHITTEST:   // ミニ窓：ボタンとシークバーの上以外は、どこをドラッグしても窓が動く
        if (g_mini) {
            LRESULT r = DefWindowProcW(h, m, w, l);
            if (r == HTCLIENT) { POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &p); int x = p.x / g_scale, y = p.y / g_scale; if (ui::HitMiniBtn(x, y) < 0 && !ui::HitSeek(x, y) && !ui::HitVolume(x, y)) return HTCAPTION; }
            return r;
        }
        break;
    case WM_NCLBUTTONDBLCLK: if (g_mini && w == HTCAPTION) { SetMiniMode(false); return 0; } break;   // ダブルクリックでふつうの窓に戻る
    case WM_NCRBUTTONUP: case WM_CONTEXTMENU:   // 右クリックで小さなメニュー：Normal Window / Always on Top / Mute / Exit
        if (g_mini) {
            POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            HMENU mn = ui::BuildMenu(100, g_ui);
            SetForegroundWindow(h);
            TrackPopupMenu(mn, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, h, nullptr);
            DestroyMenu(mn);
            return 0;
        }
        if (m == WM_CONTEXTMENU) {   // ふつうの窓：見出しの右クリックで列の一覧、visualizer ボタンの右クリックでシェーダーの一覧
            POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; POINT c = p; ScreenToClient(h, &c);
            int lx = c.x / g_scale, ly = c.y / g_scale;
            HMENU mn = nullptr;
            if (ui::HitHeader(lx, ly)) mn = ui::BuildColMenu(g_cols);
            else { int t = ui::HitTool(lx, ly); if (t >= 0 && ui::ToolCmd(t) == ID_VIS) mn = ui::BuildVisPopup(); }
            if (mn) { SetForegroundWindow(h); TrackPopupMenu(mn, TPM_LEFTALIGN | TPM_TOPALIGN, p.x, p.y, 0, h, nullptr); DestroyMenu(mn); return 0; }
        }
        break;
    case WM_CTLCOLORSTATIC:   // 読み取り専用の EDIT は既定で灰色になるので、白地に黒字にする
        if ((HWND)l == g_infoEdit) { SetTextColor((HDC)w, RGB(0, 0, 0)); SetBkColor((HDC)w, RGB(255, 255, 255)); return (LRESULT)GetStockObject(WHITE_BRUSH); }
        break;
    case WM_COPYDATA: {   // 2つ目の起動からの引き継ぎ（曲・フォルダのパス、または前に出すだけ）。重い仕事は自分のメッセージに回して、送った側をすぐ返す
        COPYDATASTRUCT* cd = (COPYDATASTRUCT*)l;
        if (!cd || cd->dwData != kCopyMagic) break;
        std::wstring* path = new std::wstring();
        if (cd->cbData >= sizeof(wchar_t) && cd->lpData) path->assign((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t) - 1);
        if (!PostMessageW(h, WM_TLAP_OPEN, 0, (LPARAM)path)) delete path;
        return TRUE; }
    case WM_TLAP_OPEN: {
        std::unique_ptr<std::wstring> path((std::wstring*)l);
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);   // 最小化なら元に戻す（ミニ窓はミニ窓のまま前に出す）
        SetForegroundWindow(h);
        if (!path->empty()) OpenPath(*path);
        return 0; }
    case WM_HOTKEY: DoMediaKey((int)w); return 0;
    case WM_APPCOMMAND: {   // RegisterHotKey が通らなかったキーだけ、窓が前にあるときに受ける
        int cmd = GET_APPCOMMAND_LPARAM(l), k = cmd == APPCOMMAND_MEDIA_PLAY_PAUSE ? 0 : cmd == APPCOMMAND_MEDIA_NEXTTRACK ? 1 : cmd == APPCOMMAND_MEDIA_PREVIOUSTRACK ? 2 : cmd == APPCOMMAND_MEDIA_STOP ? 3 : -1;
        if (k >= 0 && !g_hotOk[k]) { DoMediaKey(k + 1); return TRUE; }
        break; }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT) { POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
            if (!g_mini && (g_infoSplitDrag || ui::HitPanelSplit(p.x / g_scale, p.y / g_scale) || g_colSplitDrag || ui::HitCoverSplit(p.x / g_scale, p.y / g_scale))) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
            if (!g_mini && (g_colDrag >= 0 || ui::HitColEdge(p.x / g_scale, p.y / g_scale) >= 0)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
            if (g_splitDrag || ui::HitSplit(p.x / g_scale, p.y / g_scale)) { SetCursor(LoadCursorW(nullptr, IDC_SIZENS)); return TRUE; } }
        break;
    case vis::WM_VIS: ui::SetVisName(vis::CurrentName()); if (vis::Active()) g_visLast = vis::Current(); DoLayout(); RefreshUI();
#ifdef TLAP_TRACE
        for (int i = 0; i < ui::ToolCount(); i++) if (ui::ToolCmd(i) == ID_VIS) Trace(L"tooltip: %s", ui::ToolTip(i));
#endif
        return 0;
    case WM_COMMAND: if (!l) Command(LOWORD(w)); return 0;
    case WM_KEYDOWN:
        HandleKey((UINT)w, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_SHIFT) < 0, !(l & (1 << 30)));
        return 0;
#ifdef TLAP_TRACE
    case WM_APP + 30: HandleKey((UINT)w, (l & 1) != 0, (l & 2) != 0, true); return 0;   // 確認用：修飾キーの状態を引数で渡して、キー処理を呼ぶ（PostMessage では Shift / Ctrl が届かないため）
#endif
    case audio::WM_AUDIO:
        if (w == audio::AE_ENDED) OnEnded((unsigned)l);
        else if (w == audio::AE_SWITCHED && (unsigned)l == audio::Generation() && g_nextIdx >= 0 && g_nextIdx < (int)g_rows.size()) {
            g_cur = g_sel = g_nextIdx; g_nextIdx = -1; g_err = false; g_msg = 0;
            g_rows[g_cur].dur = audio::Duration(); g_rows[g_cur].bad = false;
            EnsureVisible(g_cur);
            Trace(L"switched: #%d %s", g_cur + 1, NameOf(g_fl.paths[g_cur]).c_str());
            OnTrackStarted();
        }
        RefreshUI();
        return 0;
    case WM_COVER: {
        std::unique_ptr<CoverResult> r((CoverResult*)l);
        if (r->id == g_coverId) { g_cover = std::move(r->im); g_coverGen++; RefreshUI(); InvalidateChrome(); }   // 古い要求の結果は捨てる
        return 0; }
    case WM_EXPLIST: OnExpList((ExpResult*)l); return 0;
    case WM_SCAN: OnScan((ScanItem*)l); return 0;
    case WM_SCANDONE: OnScanDone((unsigned)l); return 0;
    case WM_MOUSEWHEEL: {
        POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) }; ScreenToClient(h, &p);
        int x = p.x / g_scale, y = p.y / g_scale;
        Trace(L"wheel: client (%d,%d) delta %d scroll %d list=%d", x, y, (int)GET_WHEEL_DELTA_WPARAM(w), g_scroll, (int)ui::HitList(x, y));
        g_wheelAcc += GET_WHEEL_DELTA_WPARAM(w);
        while (std::abs(g_wheelAcc) >= WHEEL_DELTA) {   // 高精度ホイールはたまった量が 120 ごとに1目盛り
            int dir = g_wheelAcc > 0 ? 1 : -1;
            g_wheelAcc -= dir * WHEEL_DELTA;
            if (ui::HitSeek(x, y)) SeekRel(5.0 * dir);                      // シークバーの上：±5 秒
            else if (ui::HitVolume(x, y)) ChangeVolume(5 * dir);            // 音量スライダーの上：±5%
            else if (ui::HitExp(x, y)) ExpScrollTo(g_expScroll - 3 * dir);  // エクスプローラの上：スクロール
            else if (ui::HitList(x, y)) ScrollTo(g_scroll - 3 * dir);       // 一覧の上：スクロール
        }
        return 0;
    }
    case WM_TIMER:
        if (w == TM_TICK) RefreshUI();
        else if (w == TM_SORT && g_sortDirty) { g_sortDirty = false; ResortAll(); InvalidateChrome(); }   // 値が埋まった分の並び直し（選んでいる曲・鳴っている曲は変えない）
        return 0;
    case WM_DROPFILES: OnFilesDropped((HDROP)w); return 0;
    case WM_CLOSE: SaveSettings(); DestroyWindow(h); return 0;
    case WM_DESTROY: for (int i = 1; i <= 4; i++) UnregisterHotKey(h, i); SetThreadExecutionState(ES_CONTINUOUS); g_coverId++; for (int i = 0; i < 300 && g_coverActive > 0; i++) Sleep(10); g_scanCancel = true; if (g_scanThread.joinable()) g_scanThread.join(); vis::Shutdown(); audio::Shutdown(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// 1つしか起動しない。ユーザーごとの名前付きミューテックスで2つ目を見分け、2つ目は引数（曲・フォルダのパス）を
// WM_COPYDATA で1つ目の窓に送ってすぐ終わる。引数なしなら前に出すだけ。
// 確認用：環境変数 TLAP_INSTANCE があれば、その文字列を名前に足す（本物の TLAP と混ざらないように。普段は使わない）
static std::wstring InstanceSuffix() {
    wchar_t b[64] = L""; DWORD n = GetEnvironmentVariableW(L"TLAP_INSTANCE", b, 64);
    return (n > 0 && n < 64) ? std::wstring(b) : std::wstring();
}
static HANDLE g_singleMutex = nullptr;
// 続けてよければ true。すでに起動していて引き継ぎを済ませたら false
static bool ClaimSingleInstance(const std::wstring& arg) {
    std::wstring suffix = InstanceSuffix();
    wchar_t user[128] = L"user"; DWORD un = 128; GetUserNameW(user, &un);
    std::wstring who = std::wstring(user) + suffix;
    for (auto& c : who) if (c == 0x5C) c = L'_';   // 名前に区切り文字は使えない
    std::wstring name = std::wstring(L"Local") + wchar_t(0x5C) + L"TLAP_Single_" + who;
    static std::wstring cls; cls = L"TLAPMain" + suffix; g_cls = cls.c_str();
    g_singleMutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (g_singleMutex && GetLastError() != ERROR_ALREADY_EXISTS) return true;   // 最初の1つ
    if (!g_singleMutex) return true;   // ミューテックスを作れなければ、制御なしで普通に起動する
    // 1つ目の窓を探す（起動した直後でまだ窓が無いこともあるので、少し待つ）
    HWND target = nullptr;
    for (int i = 0; i < 100 && !target; i++) { target = FindWindowW(g_cls, nullptr); if (!target) Sleep(50); }
    if (!target) return true;   // 1つ目が見つからない（落ちた直後など）：自分が最初の1つとして続ける
    DWORD pid = 0; GetWindowThreadProcessId(target, &pid);
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT cd = {}; cd.dwData = kCopyMagic;
    std::wstring full = arg;
    if (!arg.empty()) { wchar_t fb[MAX_PATH * 2]; DWORD got = GetFullPathNameW(arg.c_str(), MAX_PATH * 2, fb, nullptr); if (got > 0 && got < MAX_PATH * 2) full.assign(fb, got); }   // こちらの作業フォルダ基準で絶対パスにしてから渡す
    cd.cbData = arg.empty() ? 0 : (DWORD)((full.size() + 1) * sizeof(wchar_t)); cd.lpData = arg.empty() ? nullptr : (PVOID)full.c_str();
    DWORD_PTR res = 0;
    SendMessageTimeoutW(target, WM_COPYDATA, (WPARAM)nullptr, (LPARAM)&cd, SMTO_ABORTIFHUNG, 5000, &res);
    return false;
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int show) {
    {
        int argc0 = 0; LPWSTR* argv0 = CommandLineToArgvW(GetCommandLineW(), &argc0);
        std::wstring a0 = (argv0 && argc0 >= 2) ? argv0[1] : L"";
        if (argv0) LocalFree(argv0);
        if (!ClaimSingleInstance(a0)) return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic = { sizeof ic, ICC_BAR_CLASSES | ICC_WIN95_CLASSES }; InitCommonControlsEx(&ic);

    settings::Data cfg;
    settings::Load(cfg);
    lang::Set((lang::Lang)cfg.lang);   // 表示言語（書体も決まるので ui::Init の前）
    ui::Init();
    audio::SetVolume(cfg.volume); audio::SetMuted(cfg.muted);
    g_repeat = cfg.repeat;
    { eq::Params p; p.on = cfg.eqOn; p.preset = cfg.eqPreset; for (int i = 0; i < eq::kBands; i++) p.gain[i] = cfg.eqGain[i];
      if (const int* pg = eq::PresetGains(p.preset)) for (int i = 0; i < eq::kBands; i++) p.gain[i] = pg[i];
      eq::Set(p); }
    g_visH = cfg.visHeight; g_infoOn = cfg.infoOn; g_infoW = cfg.infoW; g_miniVis = cfg.miniVis; g_colW = cfg.coverW;
    g_coverOn = cfg.coverOn; g_defOn = cfg.defOn; g_defFolder = cfg.defFolder; ui::SetCoverShown(g_coverOn); g_ui.coverOn = g_coverOn;
    g_openLast = cfg.openLast; g_visLast = !cfg.visFile.empty() ? cfg.visFile : cfg.visLast;
    for (int i = 0; i < LC_COUNT; i++) { g_cols.on[i] = cfg.colOn[i]; g_cols.w[i] = cfg.colW[i]; } g_cols.sortCol = cfg.sortCol; g_cols.desc = cfg.sortDesc;
    ui::SetListModel(&g_rows, &g_cols);
    g_topN = cfg.topNormal; g_topM = cfg.topMini; g_miniX = cfg.miniX; g_miniY = cfg.miniY;

    WNDCLASSEXW wc = { sizeof wc };
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = g_cls;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);

    g_hwnd = nullptr;
    HWND hw = CreateWindowExW(WS_EX_ACCEPTFILES, g_cls, L"TLAP", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, cfg.x, cfg.y, cfg.w, cfg.h, nullptr, nullptr, hi, nullptr);
    if (!hw) return 1;
    {   // スペアナ：前回のシェーダー。ファイルが無ければ None（ミニ窓の高さを先に決めるため、窓を出す前に選ぶ）
        bool have = false; for (int i = 0; i < vis::Count(); i++) if (!_wcsicmp(vis::FileAt(i).c_str(), cfg.visFile.c_str())) have = true;
        if (!cfg.visFile.empty() && have) vis::Select(cfg.visFile);
    }
    if (cfg.mini) {   // 終了時にミニ窓だったら、次もミニ窓で起動する
        g_normPlace.length = sizeof g_normPlace; GetWindowPlacement(hw, &g_normPlace); g_havePlace = true;
        g_normPlace.showCmd = cfg.maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL; g_normPlace.rcNormalPosition = RECT{ cfg.x, cfg.y, cfg.x + cfg.w, cfg.y + cfg.h };
        if (cfg.x == CW_USEDEFAULT) g_havePlace = false;
        g_mini = false; SetMiniMode(true);
    } else {
        ShowWindow(hw, cfg.maximized ? SW_SHOWMAXIMIZED : show);
        if (g_topN) ApplyTop();
    }
    UpdateWindow(hw);
    RefreshUI();

    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) OpenPath(argv[1]);
    else if (g_defOn && !g_defFolder.empty()) {   // Default folder：必ずこのフォルダを開く（前回の場所より優先）。鳴らさない。フォルダが無ければ空で起動
        std::wstring zf, zi; DWORD a = GetFileAttributesW(g_defFolder.c_str());
        if ((a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) || zipfs::SplitPath(g_defFolder, zf, zi)) OpenPath(g_defFolder, false);
    }
    else if (g_defOn) {}   // オンでフォルダが空：空で起動
    else if (g_openLast && !cfg.lastTrack.empty()) OpenLastPlace(cfg.lastTrack);   // 引数が無ければ前回の場所を開く（鳴らさない）
    if (argv) LocalFree(argv);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return (int)msg.wParam;
}
