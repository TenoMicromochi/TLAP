#pragma once
#include "common.h"

// スペアナ（ビジュアライザ）。中身は HLSL のピクセルシェーダー（visualizers/*.hlsl、書き方は visualizers/README.txt）。
//
// スレッド：
//   UI スレッド      Select / Next / Move など（下の関数）。欄は main 窓の子窓（D3D11 のスワップチェーン、FLIP_DISCARD）
//   描画スレッド     FFT（音声スレッドからロックなしのリングで受ける）・シェーダーを低解像度のキャンバスに描く・整数倍・補間なしで欄へ・Present（垂直同期）
//   コンパイル       シェーダーのコンパイルは別スレッド（その間も絵と音は止まらない）。失敗したら前の絵を動かし続けてエラーを出す
//   監視スレッド     visualizers/ を ReadDirectoryChangesW で見て、保存されたら読み直す（ホットリロード）
// d3dcompiler_47.dll は System32 から LoadLibraryEx で読む（同梱しない）。無ければスペアナは出さず、理由を Error() に入れる
namespace vis {

const UINT WM_VIS = WM_APP + 20;       // 一覧・エラー・状態が変わった（main が描き直す）

bool Create(HWND parent);
void Shutdown();
HWND Hwnd();
void Move(int x, int y, int w, int h);  // 物理px（欄の中身。枠の内側）
void SetScale(int scale);               // UI の拡大率（pixel × 拡大率 が画面上の1ドットの大きさ）
void SetPaneVisible(bool v);            // 欄が見えているか（見えていなければ描かない）
void SetMinimized(bool m);
void SetPlaying(bool p);                // 再生中か（シェーダーの playing）
void Rescan();                          // visualizers/ を読み直す（一覧）
int Count();
std::wstring NameAt(int i);             // `// name:` があればその名前、無ければファイル名（拡張子なし）
std::wstring FileAt(int i);
bool Select(const std::wstring& file, bool announce = false);  // ファイル名。空 = None。announce: 切り替えた瞬間に欄の左上へ名前を2秒出す（V・ボタンのとき。起動時とホットリロードでは出さない）
std::wstring Current();
std::wstring CurrentName();               // 今の名前（`// name:`、無ければファイル名。None のときは "None"）
void Next();                            // 次のシェーダーへ。最後の次は None
std::wstring Error();                   // 欄の下に出す1行（空なら無し）
bool Active();                          // シェーダーが選ばれていて、欄を出すべきか
unsigned long long FramesPresented();   // 確認用：Present した回数

// 音声スレッドから（EQ の後・音量の前の音）
bool WantsFeed();
void Feed(const float* stereo, size_t frames, int rate);

void DebugLoseDevice();                 // 確認用：デバイス消失と同じ復帰処理を手動で呼ぶ（GPU を固めずに試す）

}  // namespace vis
