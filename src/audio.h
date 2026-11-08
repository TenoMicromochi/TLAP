#pragma once
#include "common.h"
#include "decode.h"

// 音を鳴らす部分。流れは  デコード（decode.h）→ サンプルレート変換（resample.h、デバイスのレートへ）→ リングバッファ
//   → 処理（今は音量だけ。第3段で EQ とスペアナ）→ WASAPI 共有モード・イベント駆動（デバイスのミックス形式で1本に固定）。
//
// スレッド：
//   UI スレッド      Open / QueueNext / Play / Pause / Seek など（下の関数）。値は mutex か atomic で渡す
//   デコードスレッド 今の曲と「次の曲」をデコード・レート変換して、それぞれのリングバッファに1秒ぶんためる（シークもここ）
//   音声スレッド     WASAPI のイベントごとに、リングから取って音量を掛けて出す。今の曲の最後まで出したら、
//                    次の曲があればそのまま続きを出す（ギャップレス）。無ければ AE_ENDED を投げる
// ギャップレス：レート変換の「尾」（フィルターの余韻）を、次の曲の頭に足し込む。レートが違う曲の継ぎ目でも波形がつながる。
// 既定の出力デバイスが変わったら（IMMNotificationClient）新しいデバイスで出し直す。ミックスのレートが違えば、曲を今の位置から開き直す
namespace audio {

const UINT WM_AUDIO = WM_APP + 1;     // wParam = AE_*, lParam = 曲の世代（Generation）
enum {
    AE_ENDED = 1,                     // 今の曲を最後まで出し切った（次の曲は無い）
    AE_SWITCHED = 2,                  // 次の曲へ継ぎ目なしで移った。lParam = 新しい曲の世代
};

bool Init(HWND notifyWnd);
void Shutdown();

// 曲を開く。デコードできなければ false（前の曲はそのまま）。play = true なら開いてすぐ鳴らす。「次の曲」の予約は消える
bool Open(const std::wstring& path, bool play);
// 今の曲の次に続ける曲を予約する。開けなければ false。予約済みなら置き換える
bool QueueNext(const std::wstring& path);
void ClearNext();
void Close();                         // 曲を閉じる（停止して空にする）
bool Loaded();                        // 曲が開いている
unsigned Generation();                // 今の曲の世代（Open・継ぎ目のたびに変わる）
unsigned NextGeneration();            // 予約した曲の世代（無ければ 0）

void Play();
void Pause();
bool Playing();                       // 鳴らしている（一時停止でなく、最後まで出し切ってもいない）
void Seek(double sec);

double Position();                    // 秒（耳に届いている位置の推定）
double Duration();                    // 秒（不明は 0）
AudioInfo Info();
int OutputRate();                     // 今の出力（デバイスのミックス）のサンプルレート

void SetVolume(int percent);          // 0..100。10ms で滑らかに変える
int Volume();
void SetMuted(bool m);
bool Muted();

}  // namespace audio
