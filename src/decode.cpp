#include "decode.h"
#include "util.h"
#include "zipfs.h"
#include <shlwapi.h>
extern "C" {
#define STB_VORBIS_HEADER_ONLY
#include "../third_party/stb_vorbis-1.22/stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#include <opusfile.h>
}
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <propvarutil.h>
#include <algorithm>
#include <cmath>

bool MfStart() { return SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL)); }
void MfStop() { MFShutdown(); }

// subtype の Data1（WAVE_FORMAT_* または FourCC）から名前を決める
static void NameCodec(const GUID& sub, AudioInfo& ai) {
    switch (sub.Data1) {
    case 0x0055: ai.codec = L"MP3"; break;
    case 0x1610: case 0x00FF: case 0x1600: case 0x1602: ai.codec = L"AAC"; break;
    case 0x6C61: case 0x63616C61: case 0x616C6163: ai.codec = L"ALAC"; ai.lossless = true; break;   // 'alac'
    case 0xF1AC: ai.codec = L"FLAC"; ai.lossless = true; break;
    case 0x0001: case 0x0003: ai.codec = L"WAV"; ai.lossless = true; break;
    case 0x0160: case 0x0161: case 0x0162: case 0x0164: ai.codec = L"WMA"; break;
    case 0x0163: ai.codec = L"WMA Lossless"; ai.lossless = true; break;
    default: ai.codec = L""; break;
    }
}

void Decoder::Close() {
    if (h_) {
        if (kind_ == 1) stb_vorbis_close((stb_vorbis*)h_);
        else if (kind_ == 2) op_free((OggOpusFile*)h_);
        h_ = nullptr;
    }
    kind_ = 0;
    rd_.Reset();
    mem_.reset();
    pend_.clear(); pendPos_ = 0; eos_ = false; seekPending_ = false; skipFrames_ = 0;
    info_ = AudioInfo();
}

void Decoder::QueryOutType() {
    ComPtr<IMFMediaType> t;
    if (SUCCEEDED(rd_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &t)) && t) {
        UINT32 ch = 0; if (SUCCEEDED(t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch)) && ch >= 1) outCh_ = (int)ch;
    }
}

// ---------------------------------------------------------------- Ogg（Vorbis / Opus）
bool Decoder::OpenOgg(const std::wstring& path) {
    // 先頭のページの最初のパケットで見分ける
    unsigned char head[96] = {}; size_t got = 0;
    if (mem_) { got = std::min<size_t>(sizeof head, mem_->size()); if (got) memcpy(head, mem_->data(), got); }
    else { FILE* f = nullptr; if (_wfopen_s(&f, path.c_str(), L"rb") || !f) return false; got = fread(head, 1, sizeof head, f); fclose(f); }
    if (got < 40 || memcmp(head, "OggS", 4)) return false;
    size_t p = 27 + head[26];
    bool vorbis = p + 7 <= got && !memcmp(head + p, "\x01vorbis", 7), opus = p + 8 <= got && !memcmp(head + p, "OpusHead", 8);
    if (vorbis) {
        int err = 0; stb_vorbis* v = nullptr;
        if (mem_) { if (mem_->size() > 0x7FFFFFFF) return false; v = stb_vorbis_open_memory(mem_->data(), (int)mem_->size(), &err, nullptr); }
        else {
            FILE* f = nullptr; if (_wfopen_s(&f, path.c_str(), L"rb") || !f) return false;
            v = stb_vorbis_open_file(f, 1, &err, nullptr);   // 失敗してもファイルは閉じられる
        }
        if (!v) { Trace(L"decode: stb_vorbis open failed (%d)", err); return false; }
        stb_vorbis_info vi = stb_vorbis_get_info(v);
        h_ = v; kind_ = 1;
        info_.rate = (int)vi.sample_rate; info_.channels = vi.channels; info_.codec = L"Vorbis"; info_.bits = 0; info_.lossless = false;
        unsigned len = stb_vorbis_stream_length_in_samples(v);
        info_.duration = vi.sample_rate ? (double)len / vi.sample_rate : 0;
    } else if (opus) {
        int err = 0; OggOpusFile* of = nullptr;
        if (mem_) of = op_open_memory(mem_->data(), mem_->size(), &err);
        else {
            char u8[MAX_PATH * 4]; if (!WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, u8, sizeof u8, nullptr, nullptr)) return false;
            of = op_open_file(u8, &err);
        }
        if (!of) { Trace(L"decode: opusfile open failed (%d)", err); return false; }
        h_ = of; kind_ = 2;
        info_.rate = 48000; info_.channels = op_channel_count(of, -1); info_.codec = L"Opus"; info_.bits = 0; info_.lossless = false;
        ogg_int64_t total = op_pcm_total(of, -1);
        info_.duration = total > 0 ? (double)total / 48000.0 : 0;   // pre-skip と末尾（最後のページの granule）は opusfile が処理した長さ
    } else return false;
    int r = FillOgg();
    if (r <= 0) { Trace(L"decode: ogg first read failed (%d)", r); Close(); return false; }
    return true;
}

int Decoder::FillOgg() {
    pend_.clear(); pendPos_ = 0;
    const int kFrames = 4096;
    pend_.resize((size_t)kFrames * 2);
    for (int guard = 0; guard < 64; guard++) {
        int n = 0;
        if (kind_ == 1) n = stb_vorbis_get_samples_float_interleaved((stb_vorbis*)h_, 2, pend_.data(), kFrames * 2);
        else n = op_read_float_stereo((OggOpusFile*)h_, pend_.data(), kFrames * 2);
        if (kind_ == 2 && n == OP_HOLE) continue;   // データの欠け：読み続ける
        if (n < 0) { pend_.clear(); return -1; }
        if (n == 0) { pend_.clear(); eos_ = true; return 0; }
        pend_.resize((size_t)n * 2);
        for (auto& v : pend_) if (!(v > -64.f && v < 64.f)) v = 0;
        return 1;
    }
    pend_.clear(); return -1;
}

bool Decoder::Open(const std::wstring& path) {
    Close();
    if (zipfs::IsInside(path)) {   // ZIP の中の曲：その1曲だけをメモリに展開して、メモリから読む（一時ファイルは作らない）
        mem_ = zipfs::Acquire(path);
        if (!mem_ || mem_->empty()) { Trace(L"decode: zip entry not readable %s", path.c_str()); Close(); return false; }
    }
    {
        const wchar_t* e = PathFindExtensionW(path.c_str());
        if (!_wcsicmp(e, L".ogg") || !_wcsicmp(e, L".oga") || !_wcsicmp(e, L".opus")) return OpenOgg(path);
    }
    ComPtr<IMFAttributes> at; MFCreateAttributes(&at, 1);
    HRESULT hr0 = E_FAIL;
    if (mem_) {
        if (mem_->size() > 0xFFFFFFFFull) { Close(); return false; }
        ComPtr<IStream> st; st.Attach(SHCreateMemStream(mem_->data(), (UINT)mem_->size()));
        ComPtr<IMFByteStream> bs;
        if (st && SUCCEEDED(MFCreateMFByteStreamOnStream(st.Get(), &bs)) && bs) {
            ComPtr<IMFAttributes> ba;
            if (SUCCEEDED(bs.As(&ba)) && ba) ba->SetString(MF_BYTESTREAM_ORIGIN_NAME, NameOf(path).c_str());   // 形式の見分けに拡張子を使わせる
            hr0 = MFCreateSourceReaderFromByteStream(bs.Get(), at.Get(), &rd_);
        }
    } else hr0 = MFCreateSourceReaderFromURL(path.c_str(), at.Get(), &rd_);
    if (FAILED(hr0) || !rd_) { Trace(L"decode: CreateSourceReader failed %08x", (unsigned)hr0); Close(); return false; }
    rd_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(rd_->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE))) { Close(); return false; }

    ComPtr<IMFMediaType> nt;
    if (FAILED(rd_->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &nt)) || !nt) { Close(); return false; }
    GUID major = {}; nt->GetGUID(MF_MT_MAJOR_TYPE, &major);
    if (major != MFMediaType_Audio) { Close(); return false; }
    UINT32 rate = 0, ch = 0, bits = 0;
    nt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    nt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
    nt->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
    GUID sub = {}; nt->GetGUID(MF_MT_SUBTYPE, &sub);
    NameCodec(sub, info_);
    Trace(L"decode: native subtype %08x, %u Hz, %u ch, %u bit", (unsigned)sub.Data1, rate, ch, bits);
    if (rate < 8000 || rate > 768000) rate = 44100;
    info_.bits = (int)bits; info_.channels = (int)ch;

    // 出力は float 32bit。まず 2ch で頼み、通らなければ元のチャンネル数で受けて自分で 2ch にする
    auto setType = [&](UINT32 channels) {
        ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt);
        mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels);
        mt->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4);
        mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, channels * 4 * rate);
        mt->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        return SUCCEEDED(rd_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, mt.Get()));
    };
    if (!setType(2)) { if (ch < 1 || ch > 8 || !setType(ch)) { Trace(L"decode: SetCurrentMediaType failed (native %u ch %u Hz)", ch, rate); Close(); return false; } }
    QueryOutType();
    { ComPtr<IMFMediaType> t; UINT32 r = 0;
      if (SUCCEEDED(rd_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &t)) && t && SUCCEEDED(t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r)) && r) rate = r; }
    info_.rate = (int)rate;

    PROPVARIANT pv; PropVariantInit(&pv);
    if (SUCCEEDED(rd_->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)) && pv.vt == VT_UI8)
        info_.duration = (double)pv.uhVal.QuadPart / 1e7;
    PropVariantClear(&pv);

    // 最初の音を読んでみる（壊れたファイルはここで落とす）
    int r = Fill();
    if (r <= 0) { Trace(L"decode: first read failed (%d)", r); Close(); return false; }
    return true;
}

int Decoder::Fill() {
    if (kind_) return FillOgg();
    pend_.clear(); pendPos_ = 0;
    for (int guard = 0; guard < 4000; guard++) {
        if (eos_) return 0;
        DWORD actual = 0, flags = 0; LONGLONG ts = 0; ComPtr<IMFSample> smp;
        HRESULT hr = rd_->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &actual, &flags, &ts, &smp);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) return -1;
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) QueryOutType();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) eos_ = true;
        if (!smp) { if (eos_) return 0; continue; }
        ComPtr<IMFMediaBuffer> buf;
        if (FAILED(smp->ConvertToContiguousBuffer(&buf)) || !buf) return -1;
        BYTE* p = nullptr; DWORD maxLen = 0, curLen = 0;
        if (FAILED(buf->Lock(&p, &maxLen, &curLen))) return -1;
        size_t frames = curLen / (sizeof(float) * (size_t)outCh_);
        const float* f = (const float*)p;
        pend_.resize(frames * 2);
        if (outCh_ == 2) memcpy(pend_.data(), f, frames * 8);
        else if (outCh_ == 1) for (size_t i = 0; i < frames; i++) pend_[i * 2] = pend_[i * 2 + 1] = f[i];
        else for (size_t i = 0; i < frames; i++) { pend_[i * 2] = f[i * outCh_]; pend_[i * 2 + 1] = f[i * outCh_ + 1]; }
        buf->Unlock();
        for (auto& v : pend_) if (!(v > -64.f && v < 64.f)) v = 0;   // NaN や桁外れは捨てる
        if (seekPending_) {
            seekPending_ = false;
            double t0 = (double)ts / 1e7;
            if (t0 < seekTarget_) skipFrames_ = (uint64_t)((seekTarget_ - t0) * info_.rate + 0.5);
        }
        if (skipFrames_) {
            size_t drop = (size_t)std::min<uint64_t>(skipFrames_, frames);
            skipFrames_ -= drop;
            pend_.erase(pend_.begin(), pend_.begin() + drop * 2);
        }
        if (!pend_.empty()) return 1;
    }
    return -1;
}

int Decoder::Read(float* out, int maxFrames) {
    if (!rd_ && !kind_) return -1;
    int got = 0;
    while (got < maxFrames) {
        if (pendPos_ >= pend_.size()) {
            int r = Fill();
            if (r <= 0) return got > 0 ? got : r;
        }
        size_t availFrames = (pend_.size() - pendPos_) / 2;
        size_t n = std::min<size_t>(availFrames, (size_t)(maxFrames - got));
        memcpy(out + (size_t)got * 2, pend_.data() + pendPos_, n * 8);
        pendPos_ += n * 2; got += (int)n;
    }
    return got;
}

bool Decoder::Seek(double sec) {
    if (kind_) {
        if (sec < 0) sec = 0;
        bool ok = kind_ == 1 ? stb_vorbis_seek((stb_vorbis*)h_, (unsigned)(sec * info_.rate)) != 0
                             : op_pcm_seek((OggOpusFile*)h_, (ogg_int64_t)(sec * 48000.0)) == 0;
        pend_.clear(); pendPos_ = 0; eos_ = false; skipFrames_ = 0; seekPending_ = false;
        return ok;
    }
    if (!rd_) return false;
    if (sec < 0) sec = 0;
    PROPVARIANT pv; PropVariantInit(&pv);
    pv.vt = VT_I8; pv.hVal.QuadPart = (LONGLONG)(sec * 1e7);
    HRESULT hr = rd_->SetCurrentPosition(GUID_NULL, pv);
    pend_.clear(); pendPos_ = 0; eos_ = false; skipFrames_ = 0;
    seekPending_ = SUCCEEDED(hr); seekTarget_ = sec;
    return SUCCEEDED(hr);
}
