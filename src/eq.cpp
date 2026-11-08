#include "eq.h"
#include "lang.h"
#include <mutex>
#include <atomic>
#include <cmath>
#include <algorithm>

namespace eq {

const int kFreq[kBands] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
static const int kPresets[5][kBands] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },          // Flat
    { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 },          // Bass
    { 0, 0, 0, 0, 0, 0, 2, 4, 5, 6 },          // Treble
    { -2, -1, 0, 2, 4, 4, 3, 1, 0, -1 },       // Vocal
    { 6, 4, 0, 0, -1, -1, 0, 1, 3, 4 },        // Loudness
};

static std::mutex g_m;
static Params g_p;
static std::atomic<unsigned> g_ver{ 0 };

void Set(const Params& p) {
    std::lock_guard<std::mutex> lk(g_m);
    g_p = p;
    for (auto& g : g_p.gain) g = std::max(-12, std::min(12, g));
    g_ver++;
}
Params Get() { std::lock_guard<std::mutex> lk(g_m); return g_p; }
const int* PresetGains(int p) { return (p >= 0 && p < 5) ? kPresets[p] : nullptr; }
const wchar_t* PresetName(int p) { return lang::T((lang::Id)(lang::EQ_P_FLAT + std::max(0, std::min(kPresetCount - 1, p)))); }   // 今の言語の名前（順番は EQ_P_FLAT.. と同じ）

void Processor::Sync(int rate) {
    unsigned v = g_ver.load();
    if (v == ver_ && rate == rate_) return;
    Params p = Get(); ver_ = v; rate_ = rate;
    n_ = 0; int mx = 0;
    for (int i = 0; i < kBands; i++) {
        int g = p.gain[i]; mx = std::max(mx, g);
        if (g == 0 || kFreq[i] >= rate * 0.45) continue;   // 0dB は素通し（処理しない）
        double A = std::pow(10.0, g / 40.0), w0 = 2 * 3.14159265358979323846 * kFreq[i] / rate, al = std::sin(w0) / (2 * 1.41), c = std::cos(w0);
        double a0 = 1 + al / A;
        b0_[n_] = (1 + al * A) / a0; b1_[n_] = -2 * c / a0; b2_[n_] = (1 - al * A) / a0; a1_[n_] = -2 * c / a0; a2_[n_] = (1 - al / A) / a0;
        n_++;
    }
    pre_ = (float)std::pow(10.0, -mx / 20.0);            // 自動プリアンプ：最大の持ち上げの分だけ下げる
    active_ = p.on && (n_ > 0);
}

void Processor::Reset() { memset(z1_, 0, sizeof z1_); memset(z2_, 0, sizeof z2_); }

void Processor::Process(float* s, size_t frames) {
    for (size_t f = 0; f < frames; f++) {
        for (int c = 0; c < 2; c++) {
            double x = s[f * 2 + c];
            for (int b = 0; b < n_; b++) {
                double y = b0_[b] * x + z1_[c][b];
                z1_[c][b] = b1_[b] * x - a1_[b] * y + z2_[c][b];
                z2_[c][b] = b2_[b] * x - a2_[b] * y;
                x = y;
            }
            s[f * 2 + c] = (float)(x * pre_);
        }
    }
}

}  // namespace eq
