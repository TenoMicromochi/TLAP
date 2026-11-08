#pragma once
#include "common.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

// 形式ごとのデコーダー。出力はどれも「float 32bit・2ch（インターリーブ）・曲のサンプルレート」。
// Media Foundation（IMFSourceReader）と、Ogg Vorbis（stb_vorbis）・Opus（opusfile）。どれも同じ形（Open / Read / Seek / Info）
struct AudioInfo {
    int rate = 0;             // 出力のサンプルレート（Hz）
    int bits = 0;             // 元のビット数（不明は 0）
    int channels = 0;         // 元のチャンネル数
    double duration = 0;      // 秒（不明は 0）
    std::wstring codec;       // MP3 / AAC / ALAC / FLAC / WAV / WMA / ...
    bool lossless = false;
};

class Decoder {
public:
    Decoder() = default;
    ~Decoder() { Close(); }
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // 開いて、最初の音を1つ読んでみる。読めなければ false（壊れたファイル）
    bool Open(const std::wstring& path);
    void Close();
    const AudioInfo& Info() const { return info_; }
    // 最大 maxFrames フレームを out（2ch）に入れて、入れた数を返す。0 = 終わり、-1 = 読めなくなった
    int Read(float* out, int maxFrames);
    // sec へ移る（正確な位置まで読み飛ばす）。出来なければ false
    bool Seek(double sec);

private:
    int Fill();   // 次の1サンプルを pend_ に。1 = 入れた, 0 = 終わり, -1 = エラー
    bool OpenOgg(const std::wstring& path);   // .ogg / .oga / .opus（先頭のページで Vorbis か Opus かを見分ける）。ZIP の中なら mem_ から
    int FillOgg();
    void QueryOutType();

    int kind_ = 0;              // 0 = Media Foundation, 1 = Vorbis（stb_vorbis）, 2 = Opus（opusfile）
    void* h_ = nullptr;         // kind_ 1: stb_vorbis*, kind_ 2: OggOpusFile*
    std::shared_ptr<const std::vector<uint8_t>> mem_;   // ZIP の中の曲をメモリに展開したもの（Vorbis / Opus はこの上を直接読むので、開いている間は持っておく）
    ComPtr<IMFSourceReader> rd_;
    AudioInfo info_;
    int outCh_ = 2;
    std::vector<float> pend_;   // 2ch
    size_t pendPos_ = 0;        // pend_ の読み出し位置（float 単位）
    bool eos_ = false;
    bool seekPending_ = false;
    double seekTarget_ = 0;
    uint64_t skipFrames_ = 0;
};

// Media Foundation の起動・終了（プロセスで1回）
bool MfStart();
void MfStop();
