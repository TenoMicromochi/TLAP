#pragma once
#include "common.h"
#include <mftransform.h>

// サンプルレート変換（Windows 標準の Audio Resampler DSP、CLSID_CResamplerMediaObject、品質 60）。float 32bit・2ch のインターリーブ。
// ギャップレスのため、Drain した「尾」（フィルターの余韻）は捨てずに取っておき、次の曲の頭に足し込む（audio.cpp）
class Resampler {
public:
    bool Init(int inRate, int outRate);
    // in の frames フレームを入れて、出てきた分を out の後ろに足す
    void Process(const float* in, int frames, std::vector<float>& out);
    // 入力の終わり：残りを全部出して out の後ろに足す（その後は Flush するまで使えない）
    void Drain(std::vector<float>& out);
    // シークなどで流れを切る
    void Flush();
    bool Ready() const { return mft_ != nullptr; }
private:
    void Pull(std::vector<float>& out);
    ComPtr<IMFTransform> mft_;
    int inRate_ = 0, outRate_ = 0;
};
