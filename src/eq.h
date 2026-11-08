#pragma once
#include "common.h"

// 10バンドのグラフィック EQ（ピーキング biquad、Q=1.41、各 ±12dB・1dB 刻み）。プリアンプは自動：上げたバンドの最大値の分だけ全体を下げる。
// デコード → レート変換の後、音量の前で、音声スレッドが Processor で掛ける。オフのときは処理自体を通らない（ビット単位で素通し）。
// 値の受け渡し：UI スレッドが Set する（mutex）。音声スレッドは版番号が変わったときだけ読み直して係数を作り直す
namespace eq {

const int kBands = 10;
extern const int kFreq[kBands];                       // 31 62 125 250 500 1k 2k 4k 8k 16k
enum Preset { P_FLAT, P_BASS, P_TREBLE, P_VOCAL, P_LOUDNESS, P_CUSTOM, kPresetCount };

struct Params {
    bool on = false;
    int preset = P_FLAT;
    int gain[kBands] = {};                            // dB（-12..12）
};

void Set(const Params& p);
Params Get();
const int* PresetGains(int preset);                   // Custom は null
const wchar_t* PresetName(int preset);

class Processor {                                     // 音声スレッド専用
public:
    void Sync(int rate);                              // 値かレートが変わっていれば係数を作り直す
    bool Active() const { return active_; }           // オンで、かつ何か処理がある
    void Process(float* stereo, size_t frames);       // インターリーブ 2ch をその場で
    void Reset();                                     // 内部の状態を空にする（シーク・曲の切り替え）
private:
    unsigned ver_ = 0xFFFFFFFFu; int rate_ = 0;
    bool active_ = false;
    int n_ = 0;                                       // 有効なバンドの数
    double b0_[kBands], b1_[kBands], b2_[kBands], a1_[kBands], a2_[kBands];
    double z1_[2][kBands] = {}, z2_[2][kBands] = {};
    float pre_ = 1.f;
};

}  // namespace eq
