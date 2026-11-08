#include "resample.h"
#include "util.h"
#include <mfapi.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <mfidl.h>
#include <algorithm>

#pragma comment(lib, "wmcodecdspuuid.lib")

static ComPtr<IMFMediaType> MakeType(int rate) {
    ComPtr<IMFMediaType> mt; MFCreateMediaType(&mt);
    mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)rate);
    mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
    mt->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 8);
    mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)rate * 8);
    mt->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    return mt;
}

bool Resampler::Init(int inRate, int outRate) {
    inRate_ = inRate; outRate_ = outRate;
    mft_.Reset();
    ComPtr<IMFTransform> t;
    if (FAILED(CoCreateInstance(CLSID_CResamplerMediaObject, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&t))) || !t) return false;
    ComPtr<IWMResamplerProps> props;
    if (SUCCEEDED(t.As(&props))) props->SetHalfFilterLength(60);   // 品質 60（最高）
    if (FAILED(t->SetInputType(0, MakeType(inRate).Get(), 0))) return false;
    if (FAILED(t->SetOutputType(0, MakeType(outRate).Get(), 0))) return false;
    t->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    t->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    mft_ = t;
    return true;
}

void Resampler::Pull(std::vector<float>& out) {
    for (int guard = 0; guard < 64; guard++) {
        MFT_OUTPUT_STREAM_INFO info = {};
        mft_->GetOutputStreamInfo(0, &info);
        MFT_OUTPUT_DATA_BUFFER ob = {};
        ComPtr<IMFSample> smp; ComPtr<IMFMediaBuffer> buf;
        if (!(info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES)) {
            DWORD cb = std::max<DWORD>(info.cbSize, (DWORD)(8192 * 8));
            MFCreateSample(&smp); MFCreateMemoryBuffer(cb, &buf); smp->AddBuffer(buf.Get());
            ob.pSample = smp.Get(); ob.pSample->AddRef();
        }
        DWORD status = 0;
        HRESULT hr = mft_->ProcessOutput(0, 1, &ob, &status);
        if (ob.pEvents) ob.pEvents->Release();
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) { if (ob.pSample) ob.pSample->Release(); return; }
        if (FAILED(hr)) { if (ob.pSample) ob.pSample->Release(); return; }
        ComPtr<IMFMediaBuffer> ob2;
        if (SUCCEEDED(ob.pSample->ConvertToContiguousBuffer(&ob2)) && ob2) {
            BYTE* p = nullptr; DWORD ml = 0, cl = 0;
            if (SUCCEEDED(ob2->Lock(&p, &ml, &cl))) {
                size_t n = cl / sizeof(float);
                size_t o = out.size(); out.resize(o + n);
                memcpy(&out[o], p, n * sizeof(float));
                ob2->Unlock();
            }
        }
        ob.pSample->Release();
    }
}

void Resampler::Process(const float* in, int frames, std::vector<float>& out) {
    if (!mft_ || frames <= 0) return;
    ComPtr<IMFSample> smp; ComPtr<IMFMediaBuffer> buf;
    if (FAILED(MFCreateSample(&smp)) || FAILED(MFCreateMemoryBuffer((DWORD)frames * 8, &buf))) return;
    BYTE* p = nullptr; DWORD ml = 0;
    if (FAILED(buf->Lock(&p, &ml, nullptr))) return;
    memcpy(p, in, (size_t)frames * 8);
    buf->Unlock(); buf->SetCurrentLength((DWORD)frames * 8);
    smp->AddBuffer(buf.Get());
    if (FAILED(mft_->ProcessInput(0, smp.Get(), 0))) { Pull(out); mft_->ProcessInput(0, smp.Get(), 0); }
    Pull(out);
}

void Resampler::Drain(std::vector<float>& out) {
    if (!mft_) return;
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
    Pull(out);
}

void Resampler::Flush() {
    if (!mft_) return;
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
}
