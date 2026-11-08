#include "ui.h"
#include "lang.h"
#include "settings.h"
#include "vis.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>
#include <iterator>
#include <cmath>

HFONT g_font = nullptr;
int g_scale = 1;
static bool g_mini = false;   // ミニ窓の表示（ui:: の中だけで使う）

namespace ui {

// 立体枠の5色（TLIV・TLVV と同じ）
static const COLORREF FACE = RGB(0xc0, 0xc0, 0xc0), HI = RGB(255, 255, 255), LT = RGB(0xdf, 0xdf, 0xdf), SH = RGB(0x80, 0x80, 0x80), DK = RGB(0, 0, 0);
static const COLORREF TXT = RGB(0, 0, 0), NAVY = RGB(0, 0, 0x80), GRAYTXT = RGB(0x80, 0x80, 0x80);

enum Kind { K_BTN, K_SEP, K_VOL };   // K_VOL は スライダー＋数字
struct Tool { Kind kind; const char* icon; int cmd; lang::Id tip; };   // tip: ツールチップの文字列 ID（無いものは kIdCount）
// open | prev play/pause next | repeat | mute 音量スライダー＋%   （eq / visualizer / info は第3・第2段で足す）
static const Tool kTools[] = {
    { K_BTN, "open", ID_OPEN, lang::TIP_OPEN },
    { K_SEP, nullptr, 0, lang::kIdCount },
    { K_BTN, "prev", ID_PREV, lang::TIP_PREV }, { K_BTN, "play", ID_PLAY, lang::TIP_PLAY }, { K_BTN, "next", ID_NEXT, lang::TIP_NEXT },
    { K_SEP, nullptr, 0, lang::kIdCount },
    { K_BTN, "repeat_off", ID_REPEAT, lang::TIP_REPEAT },
    { K_SEP, nullptr, 0, lang::kIdCount },
    { K_BTN, "mute_off", ID_MUTE, lang::TIP_MUTE }, { K_VOL, nullptr, 0, lang::kIdCount },
    { K_SEP, nullptr, 0, lang::kIdCount },
    { K_BTN, "cover", ID_COVER, lang::TIP_COVER }, { K_BTN, "eq", ID_EQ, lang::TIP_EQ }, { K_BTN, "visualizer", ID_VIS, lang::TIP_VIS }, { K_BTN, "info", ID_INFO, lang::TIP_INFO },
};
static const int kVolSliderW = 64, kKnobSW = 7;
static int g_volLabW = 28;   // RebuildFont で測る
static const lang::Id kMenus[] = { lang::M_FILE, lang::M_PLAYBACK, lang::M_VIEW, lang::M_HELP };
static const wchar_t kMenuLetters[] = L"FPVH";   // Alt+この文字
int MenuLetter(int i) { return kMenuLetters[i]; }
static const int kNumMenus = (int)std::size(kMenus);
int MenuCount() { return kNumMenus; }

struct Icon { std::vector<uint32_t> px; };
static const char* kIconNames[] = { "play", "pause", "prev", "next", "open", "mute_off", "mute_on", "info", "eq", "visualizer", "repeat_off", "repeat_off_folder", "repeat_all", "repeat_one", "fullscreen", "cover" };
static Icon g_icons[std::size(kIconNames)];
static int g_textH = 12, g_timeW = 40, g_noW = 30, g_lenW = 50;

static Icon* FindIcon(const char* n) {
    for (size_t i = 0; i < std::size(kIconNames); i++) if (!strcmp(kIconNames[i], n)) return &g_icons[i];
    return nullptr;
}

// 埋め込みの PNG を ARGB(プリマルチ) の 16x16 にする
static bool DecodePng(const void* data, size_t len, std::vector<uint32_t>& px) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IStream> s; s.Attach(SHCreateMemStream((const BYTE*)data, (UINT)len));
    if (!s) return false;
    ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr; ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &fr))) return false;
    UINT w = 0, h = 0; fr->GetSize(&w, &h);
    if (w != 16 || h != 16) return false;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    px.assign(256, 0);
    return SUCCEEDED(conv->CopyPixels(nullptr, 64, 1024, (BYTE*)px.data()));
}

void Init() {
    for (size_t i = 0; i < std::size(kIconNames); i++) {
        wchar_t name[32]; swprintf_s(name, L"ICON_%S", kIconNames[i]);
        for (wchar_t* c = name; *c; c++) *c = towupper(*c);
        HRSRC r = FindResourceW(nullptr, name, RT_RCDATA);
        if (!r) continue;
        HGLOBAL g = LoadResource(nullptr, r);
        DecodePng(LockResource(g), SizeofResource(nullptr, r), g_icons[i].px);
    }
    RebuildFont();
}

static HFONT MakeFont(const wchar_t* face, int h) {
    return CreateFontW(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FIXED_PITCH | FF_DONTCARE, face);
}

// 書体を決める。候補を先頭から試し、実際に選ばれた書体名が候補と一致したものを使う（最後の候補は必ず使う）
static wchar_t g_face[LF_FACESIZE] = L"MS UI Gothic";
static int g_facePx = 12;
static void ResolveFace() {
    int n; const lang::FaceCand* c = lang::FaceCands(n);
    HDC dc = GetDC(nullptr);
    for (int i = 0; i < n; i++) {
        bool ok = i == n - 1;
        if (!ok) {
            HFONT f = MakeFont(c[i].face, -c[i].px);
            HGDIOBJ o = SelectObject(dc, f);
            wchar_t got[LF_FACESIZE] = L""; GetTextFaceW(dc, LF_FACESIZE, got);
            SelectObject(dc, o); DeleteObject(f);
            ok = _wcsicmp(got, c[i].face) == 0;
        }
        if (ok) { wcscpy_s(g_face, c[i].face); g_facePx = c[i].px; break; }
    }
    ReleaseDC(nullptr, dc);
}

HFONT CreateUiFont(int scale) { return MakeFont(g_face, -g_facePx * scale); }

void RebuildFont() {
    ResolveFace();
    if (g_font) DeleteObject(g_font);
    g_font = CreateUiFont(1);
    HDC dc = GetDC(nullptr);
    HGDIOBJ o = SelectObject(dc, g_font);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    g_textH = tm.tmHeight;
    SIZE sz; GetTextExtentPoint32W(dc, L"0:00:00", 7, &sz);   // 時間の欄の幅（h:mm:ss が入る）
    g_timeW = sz.cx + 2; g_lenW = sz.cx + 14;
    GetTextExtentPoint32W(dc, L"100%", 4, &sz); g_volLabW = sz.cx + 2;
    GetTextExtentPoint32W(dc, L"0000", 4, &sz); g_noW = sz.cx + 16;
    SelectObject(dc, o); ReleaseDC(nullptr, dc);
}

// ---------------------------------------------------------------- 部品
static void Fill(HDC dc, int x, int y, int w, int h, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    RECT r = { x, y, x + w, y + h };
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, nullptr, 0, nullptr);
}
// 太さ 1px の枠。左上 tl 色、右下 br 色
static void Ring(HDC dc, int x, int y, int w, int h, COLORREF tl, COLORREF br) {
    Fill(dc, x, y + h - 1, w, 1, br); Fill(dc, x + w - 1, y, 1, h, br);
    Fill(dc, x, y, w, 1, tl); Fill(dc, x, y, 1, h, tl);
}
static void Text(HDC dc, const std::wstring& s, const RECT& box, COLORREF c, bool right = false) {
    if (s.empty()) return;
    SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int x = right ? std::max((int)box.left, (int)box.right - (int)sz.cx) : box.left;
    int y = box.top + ((box.bottom - box.top) - g_textH) / 2;
    ExtTextOutW(dc, x, y, ETO_CLIPPED, &box, s.c_str(), (UINT)s.size(), nullptr);
}
static int TextW(HDC dc, const std::wstring& s) { SIZE sz = {}; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); return sz.cx; }
// 欄に入らない文字は … で切る
static void TextEll(HDC dc, const std::wstring& s, const RECT& box, COLORREF c, bool right) {
    int avail = (int)(box.right - box.left);
    if (s.empty() || avail <= 0) return;
    if (TextW(dc, s) <= avail) { Text(dc, s, box, c, right); return; }
    size_t n = s.size();
    while (n > 0 && TextW(dc, s.substr(0, n) + L"\u2026") > avail) n--;
    Text(dc, s.substr(0, n) + L"\u2026", box, c, false);
}

// disabled：不透明な画素ごとに明るさ L を出し、灰色 0x80 + L*63/255 で描く（形と模様は残す）
static void DrawIcon(HDC dc, const Icon* ic, int x, int y, bool disabled = false) {
    if (!ic || ic->px.size() != 256) return;
    for (int py = 0; py < 16; py++) for (int px = 0; px < 16; px++) {
        uint32_t v = ic->px[py * 16 + px];
        if ((v >> 24) != 255) continue;
        if (disabled) {
            int L = (int)(((v >> 16) & 255) * 299 + ((v >> 8) & 255) * 587 + (v & 255) * 114) / 1000;
            int g = 0x80 + L * 63 / 255;
            Fill(dc, x + px, y + py, 1, 1, RGB(g, g, g));
        } else Fill(dc, x + px, y + py, 1, 1, RGB((v >> 16) & 255, (v >> 8) & 255, v & 255));
    }
}

static const char* RepeatIcon(int r) {
    switch (r) {
    case settings::R_ALL: return "repeat_all";
    case settings::R_ONE: return "repeat_one";
    case settings::R_TRACK: return "repeat_off";            // Stop after track
    default: return "repeat_off_folder";                     // Stop at folder end
    }
}

// 音量スライダー（溝 w 幅、中心の高さ cy、左端 x）。ツールバーとミニ窓で共通
static void DrawVolSlider(HDC dc, int x, int cy, int w, int volume) {
    RECT tr = { x, cy - 3, x + w, cy + 3 };
    Ring(dc, tr.left, tr.top, w, 6, SH, HI);
    Ring(dc, tr.left + 1, tr.top + 1, w - 2, 4, DK, LT);
    Fill(dc, tr.left + 2, tr.top + 2, w - 4, 2, RGB(255, 255, 255));
    int lo = tr.left + kKnobSW / 2, hi = tr.right - 1 - kKnobSW / 2;
    int kx = lo + (int)std::lround(volume / 100.0 * (hi - lo));
    Fill(dc, tr.left + 2, tr.top + 2, std::max(0, kx - (int)tr.left - 2), 2, NAVY);
    int kt = cy - 7, kb = cy + 7;
    Fill(dc, kx - kKnobSW / 2, kt, kKnobSW, kb - kt, FACE);
    Ring(dc, kx - kKnobSW / 2, kt, kKnobSW, kb - kt, HI, DK);
    Ring(dc, kx - kKnobSW / 2 + 1, kt + 1, kKnobSW - 2, kb - kt - 2, LT, SH);
}

// ---------------------------------------------------------------- 配置
static const int kPad = 2, kMenuH = 18, kSeekH = 20, kToolH = 26, kStatusH = 20, kKnobW = 9;
static const int kSplitH = 4;
static int g_visInner = 0; static bool g_errOn = false;
static int g_panelW = 0;   // 情報パネルの幅（論理px、仕切りを含む。0 = 出さない）
static bool g_coverShown = true;
static int g_colW = 0;     // 左の列（ジャケット＋エクスプローラ）の幅（論理px、右端の仕切り4pxを含む。0 = 出さない）
static const int kExpMinH = 48;   // エクスプローラに残す最低の高さ。これを残せないほど低い窓では、ジャケットを小さくする（40 を切ったら隠す）
static const int kRowH = 16, kHeadH = 18, kSbW = 16;

// 上から メニュー / ツールバー / 一覧 / シークバー / ステータス
Layout Calc(int cw, int ch) {
    Layout L;
    L.menu = { kPad, kPad, cw - kPad, kPad + kMenuH };
    L.toolbar = { kPad, L.menu.bottom, cw - kPad, L.menu.bottom + kToolH };
    L.status = { kPad, ch - kPad - kStatusH, cw - kPad, ch - kPad };
    L.seek = { kPad, L.status.top - 2 - kSeekH, cw - kPad, L.status.top - 2 };
    int top = L.toolbar.bottom + 2, bot = L.seek.top - 2;
    // スペアナの欄：一番下に置く（境目 4px・枠 2px×2・中身・エラーの行）。一覧が小さくなりすぎないよう、中身の高さを詰める
    int errH = g_errOn ? g_textH + 4 : 0;
    int inner = g_visInner;
    if (inner > 0) inner = std::max(16, std::min(inner, (bot - top) - 80 - kSplitH - 4 - errH));
    int visTot = inner > 0 ? kSplitH + 4 + inner + errH : 0;
    int wrapBot = bot - visTot;
    if (inner > 0) {
        L.split = { kPad, wrapBot, cw - kPad, wrapBot + kSplitH };
        L.visFrame = { kPad, L.split.bottom, cw - kPad, L.split.bottom + 4 + inner };
        L.vis = { L.visFrame.left + 2, L.visFrame.top + 2, L.visFrame.right - 2, L.visFrame.bottom - 2 };
        L.errStrip = { kPad, L.visFrame.bottom, cw - kPad, L.visFrame.bottom + errH };
    } else { L.split = L.visFrame = L.vis = L.errStrip = RECT{ 0, 0, 0, 0 }; }
    // 情報パネル：いちばん右（ツールバーの下〜スペアナの欄の上）。幅 g_panelW は左端の仕切り4pxを含む
    int left = kPad, right = cw - kPad;
    L.panel = L.psplit = RECT{ 0, 0, 0, 0 };
    if (g_panelW > 0) {
        L.psplit = { cw - kPad - g_panelW, top, cw - kPad - g_panelW + kSplitH, wrapBot };
        L.panel = { L.psplit.right, top, cw - kPad, wrapBot };
        right = L.psplit.left;
    }
    // 左の列：上にジャケット（正方形、列の幅いっぱい）、その下にエクスプローラ。右端に仕切り4px
    L.cover = L.exp = L.csplit = RECT{ 0, 0, 0, 0 };
    if (g_colW > 0) {
        int cwid = std::max(1, g_colW - kSplitH);
        L.csplit = { left + cwid, top, left + cwid + kSplitH, wrapBot };
        int side = std::min(cwid, (wrapBot - top) - kExpMinH - 2);
        if (g_coverShown && side >= 40) {
            L.cover = { left, top, left + side, top + side };
            L.exp = { left, L.cover.bottom + 2, left + cwid, std::max((int)L.cover.bottom + 2, wrapBot) };
        } else L.exp = { left, top, left + cwid, std::max(top, wrapBot) };
        left = L.csplit.right;
    }
    L.wrap = { left, top, right, wrapBot };
    L.view = { L.wrap.left + 2, L.wrap.top + 2, L.wrap.right - 2, L.wrap.bottom - 2 };
    return L;
}

static int g_cw = 0, g_ch = 0;
void SetSize(int cw, int ch) { g_cw = cw; g_ch = ch; }

// シークバーの溝（左右の時間表示の間）
static RECT TrackRect(const RECT& seek) {
    int l = seek.left + 4 + g_timeW + 6, r = seek.right - 4 - g_timeW - 6;
    int cy = (seek.top + seek.bottom) / 2;
    return RECT{ l, cy - 4, std::max(l + 1, r), cy + 4 };
}
static const int kMiniW = 312, kMiniH = 76;
static bool g_miniVis = false;   // ミニ窓の下にスペアナの欄を出している（高さが2倍）
static const int kMiniVolX = 212;   // ミニ窓の音量スライダーの左端
// ミニ窓のボタンの左端：prev play next | repeat mute（音量スライダー）fullscreen
static const int kMiniBtnX[6] = { 70, 94, 118, 150, 182, 284 };
static RECT MiniSeekR() { return RECT{ 4, 66, kMiniW - 4, 72 }; }
static RECT MiniBtn(int i) { int x = kMiniBtnX[i]; return RECT{ x, 38, x + 24, 60 }; }
bool HitSeek(int x, int y) {
    if (g_mini) { RECT t = MiniSeekR(); POINT p = { x, y }; RECT h = { t.left, t.top - 3, t.right, t.bottom + 3 }; return PtInRect(&h, p) != 0; }
    RECT s = Calc(g_cw, g_ch).seek, t = TrackRect(s);
    POINT p = { x, y }; RECT h = { t.left - 2, s.top, t.right + 2, s.bottom };
    return PtInRect(&h, p) != 0;
}
double SeekFracAt(int x) {
    if (g_mini) { RECT t = MiniSeekR(); double lo = t.left + 1, hi = t.right - 1; return std::max(0.0, std::min(1.0, (x - lo) / (hi - lo))); }
    RECT t = TrackRect(Calc(g_cw, g_ch).seek);
    double lo = t.left + 1 + kKnobW / 2, hi = t.right - 1 - kKnobW / 2;
    if (hi <= lo) return 0;
    return std::max(0.0, std::min(1.0, (x - lo) / (hi - lo)));
}

std::wstring FmtTime(double sec, bool hours) {
    if (!(sec > 0)) sec = 0;
    long long t = (long long)sec;
    wchar_t b[32];
    if (hours) swprintf_s(b, L"%lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    else swprintf_s(b, L"%lld:%02lld", t / 60, t % 60);
    return b;
}

static int MenuTextW(int i) {
    HDC dc = GetDC(nullptr); HGDIOBJ o = SelectObject(dc, g_font);
    int w = TextW(dc, lang::T(kMenus[i]));
    SelectObject(dc, o); ReleaseDC(nullptr, dc);
    return w;
}
RECT MenuItemRect(int i) {
    int x = 2;
    for (int k = 0; k < i; k++) x += MenuTextW(k) + 12;
    return RECT{ x, 2, x + MenuTextW(i) + 12, 2 + kMenuH };
}
int HitMenu(int x, int y) {
    if (g_mini) return -1;
    POINT p = { x, y };
    for (int i = 0; i < kNumMenus; i++) { RECT r = MenuItemRect(i); if (PtInRect(&r, p)) return i; }
    return -1;
}

// ツールバーの並びを、ボタン・区切り・スライダー(+数字)の位置に展開する
struct Item { Kind kind; int btn; RECT r; const Tool* t; };   // btn: ボタンの通し番号（ボタン以外は -1）
static std::vector<Item> Items(int tt) {
    std::vector<Item> v; int x = 4, n = 0;
    for (auto& t : kTools) {
        switch (t.kind) {
        case K_SEP: x += 8; break;
        case K_BTN: v.push_back({ K_BTN, n++, RECT{ x, tt + 2, x + 24, tt + 24 }, &t }); x += 24; break;
        case K_VOL: { int w = kVolSliderW + 4 + g_volLabW; v.push_back({ K_VOL, -1, RECT{ x, tt + 2, x + w, tt + 24 }, &t }); x += w + 4; break; }
        }
    }
    return v;
}
static int ToolbarW() { int tt = 0; auto v = Items(tt); return v.empty() ? 0 : v.back().r.right + 4; }
int ToolCount() { int n = 0; for (auto& t : kTools) if (t.kind == K_BTN) n++; return n; }
RECT ToolRect(int i) { for (auto& it : Items(Calc(g_cw, g_ch).toolbar.top)) if (it.btn == i) return it.r; return RECT{ 0, 0, 0, 0 }; }
int HitTool(int x, int y) { if (g_mini) return -1; POINT p = { x, y }; for (int i = 0; i < ToolCount(); i++) { RECT r = ToolRect(i); if (PtInRect(&r, p)) return i; } return -1; }
static const Tool& NthTool(int i) { int n = 0; for (auto& t : kTools) if (t.kind == K_BTN) { if (n == i) return t; n++; } return kTools[0]; }
int ToolCmd(int i) { return NthTool(i).cmd; }
bool ToolEnabled(int i, const UiState& st) {
    switch (NthTool(i).cmd) {
    case ID_PREV: case ID_NEXT: case ID_PLAY: return st.haveList;
    }
    return true;
}
int MinClientWidth() { return std::max(ToolbarW() + 2, 320); }   // 右に外枠2。シークバーが潰れない幅も確保
static std::wstring g_visName;   // 空 = None
const wchar_t* ToolTip(int i) {
    if (NthTool(i).cmd == ID_VIS) { static std::wstring s; s = lang::Sub(lang::TIP_VIS, g_visName.empty() ? std::wstring(lang::T(lang::P_V_NONE)) : g_visName); return s.c_str(); }   // 左クリック・V はオン／オフ、右クリックで一覧
    return lang::T(NthTool(i).tip);
}

// 音量スライダー。溝の長方形
static RECT VolTrack() {
    if (g_mini) return RECT{ kMiniVolX, 49 - 3, kMiniVolX + kVolSliderW, 49 + 3 };
    for (auto& it : Items(Calc(g_cw, g_ch).toolbar.top)) if (it.kind == K_VOL) {
        int cy = (it.r.top + it.r.bottom) / 2;
        return RECT{ it.r.left, cy - 3, it.r.left + kVolSliderW, cy + 3 };
    }
    return RECT{ 0, 0, 0, 0 };
}
int HitVolume(int x, int y) {
    POINT p = { x, y };
    RECT t = VolTrack(); RECT h = { t.left - 3, t.top - 7, t.right + 3, t.bottom + 7 };
    return (t.right > t.left && PtInRect(&h, p)) ? 1 : 0;
}
int VolumeAtX(int x) {
    RECT t = VolTrack();
    double lo = t.left + kKnobSW / 2, hi = t.right - 1 - kKnobSW / 2;
    double f = hi > lo ? std::max(0.0, std::min(1.0, (x - lo) / (hi - lo))) : 0;
    return (int)std::lround(f * 20) * 5;
}

// ---------------------------------------------------------------- 一覧の配置
int RowH() { return kRowH; }
// 一覧の幾何（第5b段：列の合計が欄より広ければ下に横スクロールバー。縦のバーとお互いに場所を取るので、数回まわして決める）
static const std::vector<Row>* g_rowsP = nullptr;
static const ColState* g_colsP = nullptr;
static int g_hscroll = 0;
void SetListModel(const std::vector<Row>* rows, const ColState* cols) { g_rowsP = rows; g_colsP = cols; }
void SetHScroll(int x) { g_hscroll = std::max(0, x); }
int HScroll() { return g_hscroll; }
static int TotalW() { int t = 0; if (g_colsP) for (int i = 0; i < LC_COUNT; i++) if (g_colsP->on[i]) t += g_colsP->w[i]; return t; }
struct ListG { RECT v, body; int rows, availW, total; bool vbar, hbar; };
static ListG Geom() {
    ListG g; g.v = Calc(g_cw, g_ch).view; g.total = TotalW();
    int count = g_rowsP ? (int)g_rowsP->size() : 0;
    int vw = (int)(g.v.right - g.v.left), bodyH0 = (int)(g.v.bottom - g.v.top) - kHeadH;
    bool vbar = false, hbar = false; int rows = 1;
    for (int it = 0; it < 3; it++) {
        int avail = vw - (vbar ? kSbW : 0);
        hbar = g.total > avail;
        rows = std::max(1, (bodyH0 - (hbar ? kSbW : 0)) / kRowH);
        vbar = count > rows;
    }
    g.vbar = vbar; g.availW = vw - (vbar ? kSbW : 0); g.hbar = g.total > g.availW; g.rows = rows;
    g.body = RECT{ g.v.left, g.v.top + kHeadH, g.v.right, g.v.bottom - (g.hbar ? kSbW : 0) };
    return g;
}
static RECT BodyRect() { return Geom().body; }
int VisibleRows() { return Geom().rows; }
int MaxScroll(int count) { return std::max(0, count - VisibleRows()); }
static bool NeedBar(int count) { return count > VisibleRows(); }
static RECT BarRect() { ListG g = Geom(); return RECT{ g.v.right - kSbW, g.body.top, g.v.right, g.body.bottom }; }
int MaxHScroll() { ListG g = Geom(); return std::max(0, g.total - g.availW); }
int ColLeft(int c) { ListG g = Geom(); int x = (int)g.v.left - g_hscroll; if (g_colsP) for (int i = 0; i < c && i < LC_COUNT; i++) if (g_colsP->on[i]) x += g_colsP->w[i]; return x; }
bool HitHeader(int x, int y) { ListG g = Geom(); return x >= g.v.left && x < g.v.left + g.availW && y >= g.v.top && y < g.v.top + kHeadH; }
int HitHeaderCol(int x, int y) {
    if (!g_colsP || !HitHeader(x, y)) return -1;
    for (int i = 0; i < LC_COUNT; i++) if (g_colsP->on[i]) { int l = ColLeft(i); if (x >= l && x < l + g_colsP->w[i]) return i; }
    return -1;
}
int HitColEdge(int x, int y) {
    if (!g_colsP || !HitHeader(x, y)) return -1;
    for (int i = 0; i < LC_COUNT; i++) if (g_colsP->on[i]) { int e = ColLeft(i) + g_colsP->w[i]; if (std::abs(x - e) <= 3) return i; }
    return -1;
}
static RECT HBarRect() { ListG g = Geom(); return RECT{ g.v.left, g.body.bottom, g.v.left + g.availW, g.v.bottom }; }
RECT HThumbRect() {
    ListG g = Geom(); RECT t = HBarRect(); int tw = (int)(t.right - t.left);
    if (!g.hbar || g.total <= 0) return t;
    int w = std::max(16, tw * g.availW / g.total), ms = MaxHScroll();
    int left = t.left + (ms > 0 ? (int)((long long)(tw - w) * std::min(ms, g_hscroll) / ms) : 0);
    return RECT{ left, t.top, left + w, t.bottom };
}
int HitHScrollbar(int x, int y) {
    ListG g = Geom(); if (!g.hbar) return 0;
    RECT t = HBarRect(); POINT p = { x, y }; if (!PtInRect(&t, p)) return 0;
    RECT th = HThumbRect();
    if (x < th.left) return 2;
    if (x >= th.right) return 3;
    return 1;
}
int HScrollFromThumbLeft(int left) {
    RECT t = HBarRect(), th = HThumbRect(); int span = (int)(t.right - t.left) - (int)(th.right - th.left), ms = MaxHScroll();
    if (span <= 0 || ms <= 0) return 0;
    int off = std::max(0, std::min(span, left - (int)t.left));
    return (int)(((long long)off * ms + span / 2) / span);
}
const wchar_t* ColName(int c) {
    using namespace lang;
    switch (c) {
    case LC_NO: return T(COL_NO); case LC_TRACK: return T(COL_TRACK); case LC_TITLE: return T(COL_TITLE); case LC_ARTIST: return T(COL_ARTIST);
    case LC_ALBUM: return T(COL_ALBUM); case LC_ALBUMARTIST: return T(COL_ALBUMARTIST); case LC_YEAR: return T(COL_YEAR); case LC_GENRE: return T(COL_GENRE);
    case LC_LEN: return T(COL_LEN); case LC_FORMAT: return T(COL_FORMAT); case LC_BITRATE: return T(COL_BITRATE); case LC_RATE: return T(COL_RATE);
    case LC_SIZE: return T(COL_SIZE); case LC_FILE: return T(COL_FILE); case LC_MODIFIED: return T(COL_MODIFIED);
    }
    return L"";
}
// つまみの計算（一覧とエクスプローラで共通）。t: 目盛りの溝、vis: 見える行数
static RECT ThumbIn(const RECT& t, int vis, int scroll, int count) {
    int th = t.bottom - t.top;
    if (count <= vis) return RECT{ t.left, t.top, t.right, t.bottom };
    int h = std::max(16, th * vis / count);
    int ms = count - vis;
    int top = t.top + (int)((long long)(th - h) * std::max(0, std::min(ms, scroll)) / ms);
    return RECT{ t.left, top, t.right, top + h };
}
static int ScrollFromTopIn(const RECT& t, int vis, int top, int count) {
    RECT th = ThumbIn(t, vis, 0, count);
    int h = th.bottom - th.top, span = (t.bottom - t.top) - h, ms = std::max(0, count - vis);
    if (span <= 0 || ms <= 0) return 0;
    int off = std::max(0, std::min(span, (int)(top - t.top)));
    return (int)(((long long)off * ms + span / 2) / span);
}
RECT ThumbRect(int scroll, int count) { return ThumbIn(BarRect(), VisibleRows(), scroll, count); }
int ScrollFromThumbTop(int top, int count) { return ScrollFromTopIn(BarRect(), VisibleRows(), top, count); }
// 目盛り（溝とつまみ）を描く
static void PaintScrollbar(HDC dc, const RECT& tk, const RECT& th) {
    Fill(dc, tk.left, tk.top, tk.right - tk.left, tk.bottom - tk.top, LT);
    Ring(dc, tk.left, tk.top, tk.right - tk.left, tk.bottom - tk.top, SH, HI);
    int tw = th.right - th.left, thh = th.bottom - th.top;
    Fill(dc, th.left + 1, th.top, tw - 2, thh, FACE);
    Ring(dc, th.left + 1, th.top, tw - 2, thh, HI, DK);
    Ring(dc, th.left + 2, th.top + 1, tw - 4, thh - 2, LT, SH);
}

// ---------------------------------------------------------------- エクスプローラの配置（枠の内側を全部使う。見出し行なし）
static RECT ExpView() { RECT e = Calc(g_cw, g_ch).exp; if (e.right <= e.left) return RECT{ 0, 0, 0, 0 }; return RECT{ e.left + 2, e.top + 2, e.right - 2, e.bottom - 2 }; }
int ExpVisibleRows() { RECT v = ExpView(); return std::max(1, (int)(v.bottom - v.top) / kRowH); }
int ExpMaxScroll(int count) { return std::max(0, count - ExpVisibleRows()); }
static bool ExpNeedBar(int count) { return count > ExpVisibleRows(); }
static RECT ExpBarRect() { RECT v = ExpView(); return RECT{ v.right - kSbW, v.top, v.right, v.bottom }; }
RECT ExpThumbRect(int scroll, int count) { return ThumbIn(ExpBarRect(), ExpVisibleRows(), scroll, count); }
int ExpScrollFromThumbTop(int top, int count) { return ScrollFromTopIn(ExpBarRect(), ExpVisibleRows(), top, count); }
bool HitExp(int x, int y) { RECT e = Calc(g_cw, g_ch).exp; POINT p = { x, y }; return e.right > e.left && PtInRect(&e, p) != 0; }
int HitExpRow(int x, int y, int scroll, int count) {
    RECT v = ExpView(); if (v.right <= v.left) return -1;
    POINT p = { x, y };
    RECT r = { v.left, v.top, ExpNeedBar(count) ? v.right - kSbW : v.right, v.bottom };
    if (!PtInRect(&r, p)) return -1;
    int i = scroll + (y - v.top) / kRowH;
    return (i >= 0 && i < count) ? i : -1;
}
int HitExpScrollbar(int x, int y, int scroll, int count) {
    if (!ExpNeedBar(count)) return 0;
    RECT t = ExpBarRect(); POINT p = { x, y };
    if (!PtInRect(&t, p)) return 0;
    RECT th = ExpThumbRect(scroll, count);
    if (y < th.top) return 2;
    if (y >= th.bottom) return 3;
    return 1;
}
bool HitList(int x, int y) { RECT v = Calc(g_cw, g_ch).wrap; POINT p = { x, y }; return PtInRect(&v, p) != 0; }
int HitRow(int x, int y, int scroll, int count) {
    RECT b = BodyRect(); POINT p = { x, y };
    int right = NeedBar(count) ? b.right - kSbW : b.right;
    RECT r = { b.left, b.top, right, b.bottom };
    if (!PtInRect(&r, p)) return -1;
    int i = scroll + (y - b.top) / kRowH;
    return (i >= 0 && i < count) ? i : -1;
}
int HitScrollbar(int x, int y, int scroll, int count) {
    if (!NeedBar(count)) return 0;
    RECT t = BarRect(); POINT p = { x, y };
    if (!PtInRect(&t, p)) return 0;
    RECT th = ThumbRect(scroll, count);
    if (y < th.top) return 2;
    if (y >= th.bottom) return 3;
    return 1;
}

void SetVisName(const std::wstring& n) { g_visName = n; }
void SetVis(int innerH, bool err) { g_visInner = innerH; g_errOn = err; }
bool HitSplit(int x, int y) { RECT s = Calc(g_cw, g_ch).split; POINT p = { x, y }; RECT h = { s.left, s.top - 1, s.right, s.bottom + 1 }; return s.right > s.left && PtInRect(&h, p) != 0; }
int TextH() { return g_textH; }
int ErrH() { return g_textH + 4; }
RECT CoverRect() { RECT c = Calc(g_cw, g_ch).cover; if (c.right <= c.left) return RECT{ 0, 0, 0, 0 }; return RECT{ c.left + 2, c.top + 2, c.right - 2, c.bottom - 2 }; }
bool HitCover(int x, int y) { RECT c = Calc(g_cw, g_ch).cover; POINT p = { x, y }; return c.right > c.left && PtInRect(&c, p) != 0; }

// ---------------------------------------------------------------- メニュー
// Visualizer ▸ 一覧（開くたびに visualizers/ を読み直す。今のものにチェック）・None・Reload。View メニューとミニ窓の右クリックで共通
static void AddVisSubmenu(HMENU m) {
    using namespace lang;
    vis::Rescan();
    HMENU sub = CreatePopupMenu();
    std::wstring cur = vis::Current();
    for (int i = 0; i < vis::Count() && i < 900; i++) {
        std::wstring nm = vis::NameAt(i), esc; for (wchar_t c : nm) { esc += c; if (c == L'&') esc += L'&'; }
        AppendMenuW(sub, MF_STRING | (!_wcsicmp(vis::FileAt(i).c_str(), cur.c_str()) ? MF_CHECKED : 0), ID_VIS_BASE + i, esc.c_str());
    }
    if (vis::Count() == 0) AppendMenuW(sub, MF_STRING | MF_GRAYED, 0, lang::T(lang::P_V_NOFILES));
    AppendMenuW(sub, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(sub, MF_STRING | (cur.empty() ? MF_CHECKED : 0), ID_VIS_NONE, T(P_V_NONE));
    AppendMenuW(sub, MF_STRING, ID_VIS_RELOAD, T(P_V_RELOAD));
    std::wstring t = T(P_VISUALIZER); t += L"\tV";
    AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, t.c_str());
}

// 見出しの右クリック：列の名前をチェックつきで。Title は外せない（灰色）。下に区切り線と Reset Columns
HMENU BuildColMenu(const ColState& cs) {
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < LC_COUNT; i++) {
        std::wstring nm = ColName(i), esc; for (wchar_t c : nm) { esc += c; if (c == L'&') esc += L'&'; }
        AppendMenuW(m, MF_STRING | (cs.on[i] ? MF_CHECKED : 0) | (i == LC_TITLE ? MF_GRAYED : 0), ID_COL_BASE + i, esc.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_COL_RESET, lang::T(lang::COL_RESET));
    return m;
}
// visualizer ボタンの右クリック：使えるシェーダーの一覧（今のものにチェック）と、区切り線の下に Reload
HMENU BuildVisPopup() {
    vis::Rescan();
    HMENU m = CreatePopupMenu();
    std::wstring cur = vis::Current();
    for (int i = 0; i < vis::Count() && i < 900; i++) {
        std::wstring nm = vis::NameAt(i), esc; for (wchar_t c : nm) { esc += c; if (c == L'&') esc += L'&'; }
        AppendMenuW(m, MF_STRING | (!_wcsicmp(vis::FileAt(i).c_str(), cur.c_str()) ? MF_CHECKED : 0), ID_VIS_BASE + i, esc.c_str());
    }
    if (vis::Count() == 0) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, lang::T(lang::P_V_NOFILES));
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_VIS_RELOAD, lang::T(lang::P_V_RELOAD));
    return m;
}

HMENU BuildMenu(int idx, const UiState& st) {
    using namespace lang;
    HMENU m = CreatePopupMenu();
    // 項目名（& つき）に、必要ならキー（\t 以降）を足す
    auto lab = [](Id id, const wchar_t* key) { std::wstring t = T(id); if (key) { t += L'\t'; t += key; } return t; };
    auto add = [&](HMENU h, UINT id, Id name, const wchar_t* key, UINT flags = MF_STRING) { AppendMenuW(h, flags, id, lab(name, key).c_str()); };
    UINT have = st.haveList ? MF_ENABLED : MF_GRAYED;
    switch (idx) {
    case 0:
        add(m, ID_OPEN, P_OPEN, L"Ctrl+O");
        add(m, ID_OPENFOLDER, P_OPENFOLDER, L"Ctrl+Shift+O");
        add(m, ID_SHOWEXP, P_SHOWEXP, nullptr, MF_STRING | have);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        add(m, ID_SETTINGS, P_SETTINGS, L"Ctrl+,");
        add(m, ID_EXIT, P_EXIT, nullptr);
        break;
    case 1: {
        add(m, ID_PLAY, P_PLAY, L"Space", MF_STRING | have);
        add(m, ID_PREV, P_PREV, L"Left", MF_STRING | have);
        add(m, ID_NEXT, P_NEXT, L"Right", MF_STRING | have);
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        HMENU sub = CreatePopupMenu();
        add(sub, ID_REP_ONE, P_R_ONE, nullptr, MF_STRING | (st.repeat == settings::R_ONE ? MF_CHECKED : 0));
        add(sub, ID_REP_ALL, P_R_ALL, nullptr, MF_STRING | (st.repeat == settings::R_ALL ? MF_CHECKED : 0));
        add(sub, ID_REP_STOP, P_R_STOP, nullptr, MF_STRING | (st.repeat == settings::R_STOP ? MF_CHECKED : 0));
        add(sub, ID_REP_TRACK, P_R_TRACK, nullptr, MF_STRING | (st.repeat == settings::R_TRACK ? MF_CHECKED : 0));
        AppendMenuW(m, MF_POPUP, (UINT_PTR)sub, lab(P_REPEAT, L"R").c_str());
        add(m, ID_MUTE, P_MUTE, L"M", MF_STRING | (st.muted ? MF_CHECKED : 0));
        break; }
    case 2: {   // View
        add(m, ID_MINI, P_MINI, L"Ctrl+M", MF_STRING | (st.mini ? MF_CHECKED : 0));
        add(m, ID_TOP, P_TOP, L"Ctrl+T", MF_STRING | (st.onTop ? MF_CHECKED : 0));
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        add(m, ID_EQ, P_EQ, L"E", MF_STRING | (st.eqOpen ? MF_CHECKED : 0));
        add(m, ID_INFO, P_INFO, L"I", MF_STRING | (st.infoOpen ? MF_CHECKED : 0));
        add(m, ID_COVER, P_COVER, L"C", MF_STRING | (st.coverOn ? MF_CHECKED : 0));
        AddVisSubmenu(m);
        break; }
    case 3:
        add(m, ID_KEYS, P_KEYS, nullptr);
        add(m, ID_ABOUT, P_ABOUT, nullptr);
        break;
    case 100:   // ミニ窓の右クリック
        add(m, ID_MINI, P_NORMAL, L"Ctrl+M");
        add(m, ID_TOP, P_TOP, L"Ctrl+T", MF_STRING | (st.onTop ? MF_CHECKED : 0));
        add(m, ID_MUTE, P_MUTE, L"M", MF_STRING | (st.muted ? MF_CHECKED : 0));
        AddVisSubmenu(m);
        add(m, ID_SHOWVIS, P_SHOWVIS, nullptr, MF_STRING | (st.miniVis ? MF_CHECKED : 0));   // 下のスペアナを出すか（Settings のチェックと同じ値）
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        add(m, ID_EXIT, P_EXIT, nullptr);
        break;
    }
    return m;
}

// ---------------------------------------------------------------- ミニ窓
void SetMini(bool on) { g_mini = on; }
bool IsMini() { return g_mini; }
void SetMiniVis(bool on) { g_miniVis = on; }
void MiniSize(int& w, int& h) { w = kMiniW; h = g_miniVis ? kMiniH * 2 : kMiniH; }
RECT MiniCoverRect() { return RECT{ 4, 4, 64, 64 }; }
RECT MiniVisRect() { return g_miniVis ? RECT{ 0, kMiniH, kMiniW, kMiniH * 2 } : RECT{ 0, 0, 0, 0 }; }
int HitMiniBtn(int x, int y) { POINT p = { x, y }; for (int i = 0; i < 6; i++) { RECT r = MiniBtn(i); if (PtInRect(&r, p)) return i; } return -1; }

void SetPanel(bool on, int w) { g_panelW = on ? w : 0; }
void SetCoverCol(int w) { g_colW = w; }
void SetCoverShown(bool on) { g_coverShown = on; }
bool HitCoverSplit(int x, int y) { RECT s = Calc(g_cw, g_ch).csplit; POINT p = { x, y }; return s.right > s.left && PtInRect(&s, p) != 0; }
int CoverSplitLeft() { return Calc(g_cw, g_ch).csplit.left; }
bool HitPanelSplit(int x, int y) { RECT s = Calc(g_cw, g_ch).psplit; POINT p = { x, y }; return s.right > s.left && PtInRect(&s, p) != 0; }
int PanelSplitLeft() { return Calc(g_cw, g_ch).psplit.left; }

static void PaintMini(HDC dc, int cw, int ch, const UiState& st) {
    HGDIOBJ oldf = SelectObject(dc, g_font);
    Fill(dc, 0, 0, cw, ch, FACE);
    Ring(dc, 0, 0, cw, kMiniH, HI, DK);   // 枠は上の段だけ。下の段（スペアナ）は子窓が全面を覆う（黒を下に敷いておく）
    if (ch > kMiniH) Fill(dc, 0, kMiniH, cw, ch - kMiniH, RGB(0, 0, 0));
    RECT c = MiniCoverRect();
    Fill(dc, c.left, c.top, c.right - c.left, c.bottom - c.top, st.haveCover ? RGB(0, 0, 0) : RGB(0xa0, 0xa0, 0xa0));   // 絵は main が上から描く
    RECT tr = { 70, 6, cw - 6, 20 }, ar = { 70, 20, cw - 6, 34 };
    Text(dc, st.title, tr, TXT);
    Text(dc, st.artist, ar, GRAYTXT);
    const char* icons[6] = { "prev", st.playing ? "pause" : "play", "next", RepeatIcon(st.repeat), st.muted ? "mute_on" : "mute_off", "fullscreen" };
    for (int i = 0; i < 6; i++) {
        RECT r = MiniBtn(i); bool en = i < 3 ? st.haveList : true;   // prev play next は曲が無いと灰色。repeat mute fullscreen は常に効く
        bool on = en && i == 4 && st.muted;
        bool press = en && st.miniPress == i && st.miniHover == i;
        if (on || press) { if (on) Fill(dc, r.left, r.top, 24, 22, LT); Ring(dc, r.left, r.top, 24, 22, SH, HI); }
        else if (en && st.miniHover == i) Ring(dc, r.left, r.top, 24, 22, HI, SH);
        DrawIcon(dc, FindIcon(icons[i]), r.left + 4 + (press ? 1 : 0), r.top + 3 + (press ? 1 : 0), !en);
    }
    DrawVolSlider(dc, kMiniVolX, 49, kVolSliderW, st.volume);
    RECT t = MiniSeekR(); int tw = t.right - t.left, th = t.bottom - t.top;   // 下の端の細いシークバー
    Ring(dc, t.left, t.top, tw, th, SH, HI);
    Fill(dc, t.left + 1, t.top + 1, tw - 2, th - 2, RGB(255, 255, 255));
    if (st.loaded && st.dur > 0) {
        double f = std::max(0.0, std::min(1.0, st.pos / st.dur));
        Fill(dc, t.left + 1, t.top + 1, (int)std::lround(f * (tw - 2)), th - 2, NAVY);
    }
    SelectObject(dc, oldf);
}

// ---------------------------------------------------------------- エクスプローラの描画
static void PaintExplorer(HDC dc, const Layout& L, const UiState& st) {
    const RECT& w = L.exp; int ww = w.right - w.left, wh = w.bottom - w.top;
    Ring(dc, w.left, w.top, ww, wh, SH, HI);
    Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
    RECT v = { w.left + 2, w.top + 2, w.right - 2, w.bottom - 2 };
    Fill(dc, v.left, v.top, v.right - v.left, v.bottom - v.top, RGB(255, 255, 255));
    int count = st.exp ? (int)st.exp->size() : 0;
    int vis = ExpVisibleRows();
    bool bar = count > vis;
    int right = bar ? v.right - kSbW : v.right;
    const COLORREF LINE = RGB(0xa0, 0xa0, 0xa0);
    int lx = v.left + 10;                       // 縦線の x（見出しのフォルダの絵の真ん中）
    const Icon* folder = FindIcon("open");
    for (int k = 0; k < vis; k++) {
        int i = st.expScroll + k;
        if (!st.exp || i >= count) break;
        const ExpRow& r = (*st.exp)[i];
        int y = v.top + k * kRowH;
        bool sel = i == st.expSel && r.kind != 0 && r.kind != 4;
        if (sel) Fill(dc, v.left, y, right - v.left, kRowH, st.expFocus ? NAVY : FACE);
        COLORREF c = sel ? (st.expFocus ? RGB(255, 255, 255) : TXT) : (r.kind == 4 ? GRAYTXT : TXT);
        int tx;
        if (r.kind == 0) { DrawIcon(dc, folder, v.left + 2, y); tx = v.left + 22; }
        else {
            Fill(dc, lx, y, 1, r.last ? kRowH / 2 + 1 : kRowH, LINE);       // 木の線：縦線と横棒
            Fill(dc, lx, y + kRowH / 2, 8, 1, LINE);
            if (r.kind == 1 || r.kind == 2) {
                DrawIcon(dc, folder, lx + 10, y); tx = lx + 30;
                if (r.zip) {   // ZIP：フォルダの絵の真ん中にファスナー（市松の縦の2列）を重ねて、フォルダと見分ける
                    int zx = lx + 10 + 7, zy = y + (kRowH - 16) / 2 + 2;
                    COLORREF zc = sel && st.expFocus ? RGB(255, 255, 255) : RGB(0, 0, 0);
                    Fill(dc, zx - 1, zy, 4, 12, RGB(255, 255, 255));
                    for (int q = 0; q < 12; q++) Fill(dc, zx + (q & 1), zy + q, 1, 1, zc);
                    Fill(dc, zx - 1, zy + 12, 4, 1, RGB(255, 255, 255));
                }
            }
            else {
                tx = lx + 20;
                if (r.kind == 3 && !st.curPath.empty() && !_wcsicmp(r.path.c_str(), st.curPath.c_str()))   // 今鳴っている曲の印（▶）
                    for (int q = 0; q < 5; q++) Fill(dc, lx + 11 + q, y + 3 + q, 1, 9 - 2 * q, sel && st.expFocus ? RGB(255, 255, 255) : TXT);
            }
        }
        RECT tb = { tx, y, std::max(tx, right - 2), y + kRowH };
        Text(dc, r.name, tb, c);
    }
    if (bar) PaintScrollbar(dc, ExpBarRect(), ExpThumbRect(st.expScroll, count));
}

// ---------------------------------------------------------------- 描画
void Paint(HDC dc, int cw, int ch, const UiState& st) {
    if (g_mini) { PaintMini(dc, cw, ch, st); return; }
    Layout L = Calc(cw, ch);
    HGDIOBJ oldf = SelectObject(dc, g_font);
    Fill(dc, 0, 0, cw, ch, FACE);

    // メニューバー（頭文字に下線）
    for (int i = 0; i < kNumMenus; i++) {
        RECT r = MenuItemRect(i);
        if (st.openMenu == i) { Ring(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, SH, HI); }
        else if (st.hoverMenu == i && st.openMenu < 0) { Ring(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, HI, SH); }
        int off = st.openMenu == i ? 1 : 0;
        RECT tr = { r.left + 6 + off, r.top + off, r.right - 6, r.bottom };
        const std::wstring label = lang::T(kMenus[i]);
        Text(dc, label, tr, TXT);
        int ux = tr.left, cwid = TextW(dc, label.substr(0, 1));
        int ty = tr.top + ((tr.bottom - tr.top) - g_textH) / 2 + g_textH;
        Fill(dc, ux, ty - 1, cwid, 1, TXT);
    }

    // ツールバー（上に溝の線）
    int tt = L.toolbar.top;
    Fill(dc, L.toolbar.left, tt, L.toolbar.right - L.toolbar.left, 1, SH);
    Fill(dc, L.toolbar.left, tt + 1, L.toolbar.right - L.toolbar.left, 1, HI);
    {
        int x = 4;
        for (auto& t : kTools) {
            if (t.kind == K_SEP) {
                int sy = tt + 2 + 5 / 2;
                Fill(dc, x + 3, sy, 1, 18, SH); Fill(dc, x + 4, sy, 1, 18, HI);
                x += 8;
            } else if (t.kind == K_VOL) {
                RECT r = { x, tt + 2, x + kVolSliderW + 4 + g_volLabW, tt + 24 };
                wchar_t b[16]; swprintf_s(b, L"%d%%", st.volume);
                int cy = (r.top + r.bottom) / 2, w = kVolSliderW;
                DrawVolSlider(dc, r.left, cy, w, st.volume);
                RECT lb = { r.left + w + 4, r.top, r.left + w + 4 + g_volLabW, r.bottom };
                Text(dc, b, lb, TXT);
                x += r.right - r.left + 4;
            } else {
                RECT r = { x, tt + 2, x + 24, tt + 24 };
                int n = -1; { int k = 0; for (auto& u : kTools) { if (&u == &t) { n = k; break; } if (u.kind == K_BTN) k++; } }
                bool en = ToolEnabled(n, st);
                bool on = en && ((t.cmd == ID_MUTE && st.muted) || (t.cmd == ID_INFO && st.infoOpen) || (t.cmd == ID_COVER && st.coverOn) || (t.cmd == ID_EQ && st.eqOpen) || (t.cmd == ID_VIS && st.visOn));
                bool press = en && st.pressTool == n && st.hoverTool == n;
                int w = 24, h = 22;
                if (on || press) { if (on) Fill(dc, r.left, r.top, w, h, LT); Ring(dc, r.left, r.top, w, h, SH, HI); }
                else if (en && st.hoverTool == n) Ring(dc, r.left, r.top, w, h, HI, SH);
                const char* icon = t.icon;
                if (t.cmd == ID_PLAY && st.playing) icon = "pause";
                if (t.cmd == ID_MUTE && st.muted) icon = "mute_on";
                if (t.cmd == ID_REPEAT) icon = RepeatIcon(st.repeat);
                DrawIcon(dc, FindIcon(icon), r.left + 4 + (press ? 1 : 0), r.top + 3 + (press ? 1 : 0), !en);
                x += 24;
            }
        }
    }

    // 情報パネルの仕切りと枠（中身の EDIT は main が重ねる）
    if (L.panel.right > L.panel.left) {
        const RECT& sp = L.psplit; Ring(dc, sp.left, sp.top, sp.right - sp.left, sp.bottom - sp.top, HI, SH);
        const RECT& w = L.panel; int ww = w.right - w.left, wh = w.bottom - w.top;
        Ring(dc, w.left, w.top, ww, wh, SH, HI);
        Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
        Fill(dc, w.left + 2, w.top + 2, ww - 4, wh - 4, RGB(255, 255, 255));
    }

    // ジャケット欄の枠（絵は main が上から描く。無いときは灰色）
    if (L.cover.right > L.cover.left) {
        const RECT& w = L.cover; int ww = w.right - w.left, wh = w.bottom - w.top;
        Ring(dc, w.left, w.top, ww, wh, SH, HI);
        Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
        Fill(dc, w.left + 2, w.top + 2, ww - 4, wh - 4, st.haveCover ? RGB(0, 0, 0) : RGB(0xa0, 0xa0, 0xa0));
    }

    // 左の列の仕切りと、エクスプローラ
    if (L.csplit.right > L.csplit.left) { const RECT& sp = L.csplit; Ring(dc, sp.left, sp.top, sp.right - sp.left, sp.bottom - sp.top, HI, SH); }
    if (L.exp.right > L.exp.left) PaintExplorer(dc, L, st);

    // スペアナの欄の枠（中身は子窓の D3D11）と、枠の下のエラーの行（赤）
    if (L.visFrame.right > L.visFrame.left) {
        const RECT& w = L.visFrame; int ww = w.right - w.left, wh = w.bottom - w.top;
        Ring(dc, w.left, w.top, ww, wh, SH, HI);
        Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
        Fill(dc, L.vis.left, L.vis.top, L.vis.right - L.vis.left, L.vis.bottom - L.vis.top, RGB(0, 0, 0));
        if (L.errStrip.bottom > L.errStrip.top && !st.visErr.empty()) {
            RECT tb = { L.errStrip.left + 2, L.errStrip.top, L.errStrip.right - 2, L.errStrip.bottom };
            Text(dc, st.visErr, tb, RGB(0xc0, 0, 0));
        }
    }

    // 一覧の枠
    {
        const RECT& w = L.wrap; int ww = w.right - w.left, wh = w.bottom - w.top;
        Ring(dc, w.left, w.top, ww, wh, SH, HI);
        Ring(dc, w.left + 1, w.top + 1, ww - 2, wh - 2, DK, LT);
        ListG G = Geom();
        const RECT& v = G.v;
        int vw = v.right - v.left, vh = v.bottom - v.top;
        Fill(dc, v.left, v.top, vw, vh, RGB(255, 255, 255));
        int count = st.rows ? (int)st.rows->size() : 0;
        int right = v.left + G.availW;       // 縦のバーの左
        const ColState* cs = g_colsP;
        int firstCol = -1; if (cs) for (int i = 0; i < LC_COUNT; i++) if (cs->on[i]) { firstCol = i; break; }
        int saved = SaveDC(dc);
        IntersectClipRect(dc, v.left, v.top, right, G.body.bottom);   // 横スクロールではみ出した列は見えない
        // 見出し（クリックで並べ替え。並べ替えの列には ▲ / ▼）
        int endX = v.left - g_hscroll;
        if (cs) for (int c = 0; c < LC_COUNT; c++) {
            if (!cs->on[c]) continue;
            int x0 = ColLeft(c), x1 = x0 + cs->w[c]; endX = x1;
            Fill(dc, x0, v.top, x1 - x0, kHeadH, FACE);
            Ring(dc, x0, v.top, x1 - x0, kHeadH, HI, SH);
            bool arrow = cs->sortCol == c;
            RECT tb = { x0 + 4, v.top, x1 - 4 - (arrow ? 11 : 0), v.top + kHeadH };
            Text(dc, ColName(c), tb, TXT, ColIsNumeric(c));
            if (arrow) {   // 7x4 の小さな三角（1倍で描いて整数倍に拡大される）
                int ax = x1 - 4 - 7, ay = v.top + (kHeadH - 4) / 2;
                for (int r = 0; r < 4; r++) { int wdt = cs->desc ? 7 - 2 * r : 1 + 2 * r; Fill(dc, ax + (7 - wdt) / 2, ay + r, wdt, 1, TXT); }
            }
        }
        if (endX < right) Fill(dc, endX, v.top, right - endX, kHeadH, FACE);
        if (G.vbar) Fill(dc, right, v.top, kSbW, kHeadH, FACE);
        // 行
        int vis = G.rows;
        for (int k = 0; k < vis; k++) {
            int i = st.scroll + k;
            if (!st.rows || i >= count) break;
            const Row& rw = (*st.rows)[i];
            int y = v.top + kHeadH + k * kRowH;
            bool sel = i == st.sel;
            if (sel) Fill(dc, v.left, y, right - v.left, kRowH, NAVY);
            COLORREF c = rw.bad ? (sel ? RGB(0xc0, 0xc0, 0xc0) : GRAYTXT) : (sel ? RGB(255, 255, 255) : TXT);
            if (cs) for (int col = 0; col < LC_COUNT; col++) {
                if (!cs->on[col]) continue;
                int x0 = ColLeft(col), x1 = x0 + cs->w[col];
                if (x1 <= v.left || x0 >= right) continue;
                bool rightAl = ColIsNumeric(col);
                RECT tb = { x0 + (rightAl ? 4 : (col == firstCol ? 11 : 4)), y, x1 - (rightAl ? 6 : 4), y + kRowH };
                const std::wstring& txt = rw.cell[col];
                TextEll(dc, txt, tb, c, rightAl);
            }
            if (i == st.cur && firstCol >= 0) {   // 再生中の印（▶）
                COLORREF mc = sel ? RGB(255, 255, 255) : (rw.bad ? GRAYTXT : TXT);
                int mx = ColLeft(firstCol) + 3;
                for (int q = 0; q < 5; q++) Fill(dc, mx + q, y + 3 + q, 1, 9 - 2 * q, mc);
            }
        }
        RestoreDC(dc, saved);
        // 目盛り（スクロールバー）
        if (G.vbar) PaintScrollbar(dc, BarRect(), ThumbRect(st.scroll, count));
        if (G.hbar) {
            RECT hb = HBarRect(); RECT ht = HThumbRect();
            Fill(dc, hb.left, hb.top, hb.right - hb.left, hb.bottom - hb.top, LT);
            Ring(dc, hb.left, hb.top, hb.right - hb.left, hb.bottom - hb.top, SH, HI);
            int tw = ht.right - ht.left, thh = ht.bottom - ht.top;
            Fill(dc, ht.left, ht.top + 1, tw, thh - 2, FACE);
            Ring(dc, ht.left, ht.top + 1, tw, thh - 2, HI, DK);
            Ring(dc, ht.left + 1, ht.top + 2, tw - 2, thh - 4, LT, SH);
            if (G.vbar) Fill(dc, right, hb.top, kSbW, hb.bottom - hb.top, FACE);
        }
    }

    // シークバー  1:23 [====|----] 4:56
    {
        bool hours = st.dur >= 3600;
        RECT lt = { L.seek.left + 4, L.seek.top, L.seek.left + 4 + g_timeW, L.seek.bottom };
        RECT rt = { L.seek.right - 4 - g_timeW, L.seek.top, L.seek.right - 4, L.seek.bottom };
        Text(dc, FmtTime(st.loaded ? st.pos : 0, hours), lt, TXT);
        Text(dc, FmtTime(st.loaded ? st.dur : 0, hours), rt, TXT, true);
        RECT t = TrackRect(L.seek); int tw = t.right - t.left, th = t.bottom - t.top;
        Ring(dc, t.left, t.top, tw, th, SH, HI);
        Ring(dc, t.left + 1, t.top + 1, tw - 2, th - 2, DK, LT);
        Fill(dc, t.left + 2, t.top + 2, tw - 4, th - 4, RGB(255, 255, 255));
        if (st.loaded && st.dur > 0) {
            double f = std::max(0.0, std::min(1.0, st.pos / st.dur));
            int lo = t.left + 1 + kKnobW / 2, hi = t.right - 1 - kKnobW / 2;
            int kx = lo + (int)std::lround(f * (hi - lo));
            Fill(dc, t.left + 2, t.top + 2, std::max(0, kx - (int)(t.left + 2)), th - 4, NAVY);
            int kt = L.seek.top + 2, kb = L.seek.bottom - 2;
            Fill(dc, kx - kKnobW / 2, kt, kKnobW, kb - kt, FACE);
            Ring(dc, kx - kKnobW / 2, kt, kKnobW, kb - kt, HI, DK);
            Ring(dc, kx - kKnobW / 2 + 1, kt + 1, kKnobW - 2, kb - kt - 2, LT, SH);
        }
    }

    // ステータスバー：形式 / 44.1kHz 16bit / 3 / 12
    {
        const RECT& sr = L.status; int top = sr.top, h = kStatusH, gap = 2;
        const StatusInfo& si = st.status;
        struct P { const std::wstring* t; int w; };
        std::vector<P> ps = { { &si.rate, std::max(TextW(dc, L"000.0kHz 00bit"), TextW(dc, si.rate)) + 10 },
                              { &si.index, std::max(TextW(dc, L"0000 / 0000"), TextW(dc, si.index)) + 10 } };
        int rightTotal = 0;
        for (auto& p : ps) rightTotal += p.w + gap;
        int leftW = std::max(0, (int)(sr.right - sr.left) - rightTotal);
        int x = sr.left;
        auto pane = [&](int w, const std::wstring& t, COLORREF c) {
            Ring(dc, x, top, w, h, SH, HI);
            RECT tb = { x + 5, top + 2, x + w - 5, top + h - 2 };
            Text(dc, t, tb, c);
            x += w + gap;
        };
        pane(leftW - gap < 0 ? 0 : leftW - gap, si.left, st.errorMsg ? RGB(0xc0, 0, 0) : TXT);
        for (auto& p : ps) pane(p.w, *p.t, TXT);
    }
    SelectObject(dc, oldf);
}

}  // namespace ui
