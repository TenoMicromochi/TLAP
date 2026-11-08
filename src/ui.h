#pragma once
#include "common.h"

enum Cmd { ID_OPEN = 100, ID_OPENFOLDER, ID_SHOWEXP, ID_EXIT, ID_PREV, ID_NEXT, ID_PLAY, ID_MUTE,
    ID_REPEAT, ID_REP_STOP, ID_REP_ALL, ID_REP_ONE, ID_INFO, ID_EQ, ID_VIS,
    ID_SETTINGS, ID_MINI, ID_TOP, ID_KEYS, ID_ABOUT, ID_VIS_NONE, ID_VIS_RELOAD, ID_REP_TRACK, ID_SHOWVIS, ID_COL_RESET, ID_COVER, ID_COL_BASE = 3000, ID_VIS_BASE = 2000 };   // ID_COL_BASE + i = 見出しの右クリックの i 番目の列   // ID_VIS_BASE + i = View ▸ Visualizer の i 番目

// 曲の一覧の列（第5b段）。並びはこの順で固定（左右の入れ替えはしない）
enum ListCol { LC_NO, LC_TRACK, LC_TITLE, LC_ARTIST, LC_ALBUM, LC_ALBUMARTIST, LC_YEAR, LC_GENRE, LC_LEN, LC_FORMAT, LC_BITRATE, LC_RATE, LC_SIZE, LC_FILE, LC_MODIFIED, LC_COUNT };
struct ColState {
    bool on[LC_COUNT] = {};
    int w[LC_COUNT] = {};              // 幅（論理px）
    int sortCol = LC_NO;               // 並べ替えの列と向き（初期は # 昇順）
    bool desc = false;
    ColState() { Reset(); }
    void Reset() {
        static const int dw[LC_COUNT] = { 40, 56, 240, 150, 150, 150, 48, 100, 56, 64, 80, 80, 72, 180, 120 };
        for (int i = 0; i < LC_COUNT; i++) { on[i] = (i == LC_NO || i == LC_TITLE || i == LC_ARTIST || i == LC_LEN); w[i] = dw[i]; }
        sortCol = LC_NO; desc = false;
    }
};
inline bool ColIsNumeric(int c) { return c == LC_NO || c == LC_TRACK || c == LC_YEAR || c == LC_LEN || c == LC_BITRATE || c == LC_RATE || c == LC_SIZE; }
inline int ColMinW(int c) { return c == LC_TITLE ? 80 : 24; }

// 一覧の1行
struct Row {
    std::wstring name;     // 曲名（タグが無ければファイル名・拡張子なし）
    double dur = 0;        // 秒（分かれば。0 は不明）
    bool bad = false;      // 再生できない（灰色）
    std::wstring cell[LC_COUNT];   // 各列に出す文字（main が作る）
};

// エクスプローラの1行（第4d段）。kind: 0 見出し（今のフォルダ。いちばん上の1行だけ）/ 1 ..（ひとつ上）/ 2 フォルダ（ドライブも）/ 3 曲 / 4 メッセージ（(cannot open)）
struct ExpRow {
    std::wstring name, path;
    int kind = 0;
    bool zip = false;    // .zip ファイル（kind 2 のまま。フォルダの絵にファスナーを重ねる）
    bool last = false;   // 木の線：最後の枝（縦線をここで止める）
};

struct StatusInfo {
    std::wstring left, rate, index;   // 形式（または Can't play this file） / 44.1kHz 16bit / 3 / 12
    bool operator==(const StatusInfo& o) const { return left == o.left && rate == o.rate && index == o.index; }
};

struct UiState {
    int hoverTool = -1, pressTool = -1;
    int hoverMenu = -1, openMenu = -1;
    bool haveList = false;            // 一覧に曲がある
    bool loaded = false;              // 曲が開いている（シークバーが使える）
    bool playing = false;
    bool mini = false, onTop = false; // ミニ窓か / Always on Top
    std::wstring title, artist;       // ミニ窓の2行（曲名・アーティスト）
    int miniHover = -1, miniPress = -1;   // ミニ窓のボタン（0 prev, 1 play, 2 next, 3 repeat, 4 mute, 5 fullscreen）
    bool eqOpen = false;              // EQ パネルが開いている
    bool visOn = false;               // スペアナ（シェーダー）が選ばれている
    std::wstring visErr;              // スペアナの欄の下に出すエラー（1行）
    bool infoOpen = false;            // 情報パネル（右の欄）を出している
    bool coverOn = true;              // ジャケットの欄を出している（cover ボタンのへこみ・View ▸ Cover のチェック）
    bool haveCover = false;           // ジャケットがある（無ければ灰色の枠）
    bool errorMsg = false;            // ステータスの左が「再生できない」の知らせ
    int repeat = 0;                   // settings::Repeat
    int volume = 30; bool muted = false;
    double pos = 0, dur = 0;          // 秒（ドラッグ中はドラッグ先）
    StatusInfo status;
    const std::vector<Row>* rows = nullptr;
    int cur = -1;                     // 今の曲（再生中の印）
    int sel = -1;                     // 選んだ行
    int scroll = 0;                   // 一覧の先頭の行
    bool miniVis = true;              // Show Visualizer（ミニ窓の右クリックのチェック）
    // エクスプローラ（左の列、ジャケットの下）
    const std::vector<ExpRow>* exp = nullptr;
    int expSel = -1, expScroll = 0;
    bool expFocus = false;            // キー操作がエクスプローラに向いている（選択色が濃くなる）
    std::wstring curPath;             // 今鳴っている曲のフルパス（エクスプローラの印）
};

namespace ui {
// 論理座標（1倍）。main が整数倍に転送する
struct Layout { RECT menu, toolbar, cover, exp, csplit, wrap, view, split, visFrame, vis, errStrip, seek, status, panel, psplit; };   // cover: ジャケット欄の枠（幅 0 なら無い）。exp: エクスプローラの枠。csplit: 左の列の右端の仕切り。panel / psplit: 情報パネルの枠と、その左端の仕切り（無ければ空）

void SetSize(int cw, int ch);
void Init();                       // アイコンとフォントの読み込み
void RebuildFont();                // g_font を作り直す
HFONT CreateUiFont(int scale);     // UI と同じフォントを scale 倍で作る（呼んだ側が DeleteObject）
Layout Calc(int cw, int ch);
void Paint(HDC hdc, int cw, int ch, const UiState& st);

RECT MenuItemRect(int i);
int HitMenu(int x, int y);
int MenuLetter(int i);             // Alt+この文字でメニューを開く
int MenuCount();
HMENU BuildMenu(int idx, const UiState& st);

int ToolCount();
RECT ToolRect(int i);
int HitTool(int x, int y);
int ToolCmd(int i);
bool ToolEnabled(int i, const UiState& st);
const wchar_t* ToolTip(int i);
int MinClientWidth();              // ツールバーのボタンが全部見える最小幅（論理px）
int HitVolume(int x, int y);       // 音量スライダーの上か（1/0）
int VolumeAtX(int x);              // 0..100（5刻み）

bool HitSeek(int x, int y);        // シークバーの上か
double SeekFracAt(int x);          // x 位置を 0..1 に
std::wstring FmtTime(double sec, bool hours);   // m:ss / h:mm:ss

// 曲の一覧の列（第5b段）
void SetListModel(const std::vector<Row>* rows, const ColState* cols);   // 一覧の幾何（行数・列）を知らせる（起動時に1回）
void SetHScroll(int x);
int HScroll();
const wchar_t* ColName(int c);                      // 見出しの文字（今の言語）
int ColLeft(int c);                                 // 列 c の左端 x（論理px、横スクロール込み）
bool HitHeader(int x, int y);                       // 見出しの帯の上
int HitHeaderCol(int x, int y);                     // 見出しの列（列の上でなければ -1）
int HitColEdge(int x, int y);                       // 列の右端の ±3px にある列（無ければ -1）
int MaxHScroll();
int HitHScrollbar(int x, int y);                    // 0 なし, 1 つまみ, 2 つまみの左, 3 つまみの右
RECT HThumbRect();
int HScrollFromThumbLeft(int left);
HMENU BuildColMenu(const ColState& cs);             // 見出しの右クリック
HMENU BuildVisPopup();                              // visualizer ボタンの右クリック（一覧・Reload）

// 曲の一覧
int RowH();
int VisibleRows();                                  // 一覧が一度に見せる行数
bool HitList(int x, int y);                         // 一覧の枠の中（見出し・目盛り含む）
int HitRow(int x, int y, int scroll, int count);    // 行の番号（行の上でなければ -1）
int HitScrollbar(int x, int y, int scroll, int count);   // 0 なし, 1 つまみ, 2 つまみの上, 3 つまみの下
RECT ThumbRect(int scroll, int count);
int ScrollFromThumbTop(int top, int count);         // つまみの上端 y から scroll
int MaxScroll(int count);

// ジャケット欄（正方形）。絵そのものは main が高品質に描く（物理ピクセル）
// スペアナの欄：一覧との間の境目（ドラッグで高さを変える）、枠、中身（子窓）、枠の下のエラーの行
// ミニ窓（小さな窓：ジャケット・曲名とアーティスト・prev play next・下の端に細いシークバー）
void SetMini(bool on);
bool IsMini();
void SetMiniVis(bool on);           // ミニ窓の下にスペアナの欄を出す（高さが2倍になる）
void MiniSize(int& w, int& h);      // 論理px
RECT MiniCoverRect();               // ジャケットを置く欄（論理px）
RECT MiniVisRect();                 // 下のスペアナの欄（論理px。出さないときは空）
int HitMiniBtn(int x, int y);       // 0 prev, 1 play, 2 next, 3 repeat, 4 mute, 5 fullscreen、なければ -1
void SetCoverShown(bool on);          // ジャケットの欄を出すか（オフなら左の列はエクスプローラだけ）
void SetCoverCol(int w);            // 左の列（ジャケット＋エクスプローラ）。w: 幅（論理px、右端の仕切り4pxを含む）
bool HitCoverSplit(int x, int y);
int CoverSplitLeft();
// エクスプローラ
int ExpVisibleRows();
int ExpMaxScroll(int count);
bool HitExp(int x, int y);                                    // 枠の中
int HitExpRow(int x, int y, int scroll, int count);           // 行の番号（行の上でなければ -1）
int HitExpScrollbar(int x, int y, int scroll, int count);     // 0 なし, 1 つまみ, 2 つまみの上, 3 つまみの下
RECT ExpThumbRect(int scroll, int count);
int ExpScrollFromThumbTop(int top, int count);
void SetPanel(bool on, int w);      // 情報パネル（右の欄）。w: 幅（論理px、仕切り4pxを含む）
bool HitPanelSplit(int x, int y);   // 情報パネルの仕切りの上か
int PanelSplitLeft();               // 仕切りの左端 x（ドラッグの掴み位置の計算用）
void SetVisName(const std::wstring& name);   // visualizer ボタンのツールチップに出す今の名前
void SetVis(int innerH, bool err);   // innerH: 中身の高さ（論理px。0 = 欄ごと無し）、err: エラーの行を出す
bool HitSplit(int x, int y);
int TextH();
int ErrH();
RECT CoverRect();                  // 絵を置く内側（論理px）。幅 0 なら欄が無い
bool HitCover(int x, int y);
}
