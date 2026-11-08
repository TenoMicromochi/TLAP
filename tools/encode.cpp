// テスト用の音を作る道具：WAV を Media Foundation の変換（IMFTranscodeProfile）で別形式にする。
//   encode.exe <in.wav> <out> <aac|mp3|wma|flac|alac>
// 作れない形式は、失敗の HRESULT を出して終了コード 1 を返す。build は tools\build_tools.bat
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

using Microsoft::WRL::ComPtr;

static const GUID kALAC = { 0x63616C61, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID kFLAC = { 0x0000F1AC, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

static int Fail(const char* what, HRESULT hr) { fprintf(stderr, "FAIL %s: 0x%08lx\n", what, (unsigned long)hr); return 1; }

int wmain(int argc, wchar_t** argv) {
    if (argc < 4) { fprintf(stderr, "usage: encode in.wav out aac|mp3|wma|flac|alac\n"); return 2; }
    std::wstring kind = argv[3];
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    fprintf(stderr, "step: start\n");
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return Fail("MFStartup", hr);

    GUID sub, container;
    if (kind == L"aac") { sub = MFAudioFormat_AAC; container = MFTranscodeContainerType_MPEG4; }
    else if (kind == L"mp3") { sub = MFAudioFormat_MP3; container = MFTranscodeContainerType_MP3; }
    else if (kind == L"wma") { sub = MFAudioFormat_WMAudioV9; container = MFTranscodeContainerType_ASF; }
    else if (kind == L"flac") { sub = kFLAC; container = MFTranscodeContainerType_FLAC; }
    else if (kind == L"alac") { sub = kALAC; container = MFTranscodeContainerType_MPEG4; }
    else { fprintf(stderr, "unknown kind\n"); return 2; }

    // 入力の形（サンプルレート・チャンネル）を読む
    ComPtr<IMFSourceResolver> res; hr = MFCreateSourceResolver(&res); if (FAILED(hr)) return Fail("resolver", hr);
    MF_OBJECT_TYPE ot = MF_OBJECT_INVALID; ComPtr<IUnknown> un;
    hr = res->CreateObjectFromURL(argv[1], MF_RESOLUTION_MEDIASOURCE, nullptr, &ot, &un); if (FAILED(hr)) return Fail("open input", hr);
    ComPtr<IMFMediaSource> src; hr = un.As(&src); if (FAILED(hr) || !src) return Fail("as source", hr);
    UINT32 rate = 44100, ch = 2;
    {
        ComPtr<IMFPresentationDescriptor> pd; hr = src->CreatePresentationDescriptor(&pd); if (FAILED(hr) || !pd) return Fail("pd", hr);
        ComPtr<IMFStreamDescriptor> sd; BOOL sel = FALSE; hr = pd->GetStreamDescriptorByIndex(0, &sel, &sd); if (FAILED(hr) || !sd) return Fail("sd", hr);
        ComPtr<IMFMediaTypeHandler> mh; hr = sd->GetMediaTypeHandler(&mh); if (FAILED(hr) || !mh) return Fail("mh", hr);
        ComPtr<IMFMediaType> mt; hr = mh->GetMediaTypeByIndex(0, &mt); if (FAILED(hr) || !mt) return Fail("mt", hr);
        mt->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate); mt->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
    }

    fprintf(stderr, "step: input %u Hz %u ch\n", rate, ch);
    ComPtr<IMFTranscodeProfile> prof; hr = MFCreateTranscodeProfile(&prof); if (FAILED(hr)) return Fail("profile", hr);
    ComPtr<IMFAttributes> aa; MFCreateAttributes(&aa, 8);
    bool filled = false;
    // 変換が用意している出力の型の中から、レート・チャンネルが合うものを選ぶ（ビットレートは最大のもの）
    {
        ComPtr<IMFCollection> col;
        if (SUCCEEDED(MFTranscodeGetAudioOutputAvailableTypes(sub, MFT_ENUM_FLAG_ALL, nullptr, &col)) && col) {
            DWORD n = 0; col->GetElementCount(&n);
            ComPtr<IMFMediaType> best; UINT32 bestBr = 0;
            for (DWORD i = 0; i < n; i++) {
                ComPtr<IUnknown> u; col->GetElement(i, &u);
                ComPtr<IMFMediaType> t; if (FAILED(u.As(&t))) continue;
                UINT32 r = 0, c = 0, br = 0, bits = 0;
                t->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &r); t->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &c);
                t->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &br); t->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
                if (r != rate || c != ch) continue;
                if (kind == L"aac" || kind == L"mp3" || kind == L"wma") { if (br > 24000 && br < 40000 && (!best || br > bestBr)) { best = t; bestBr = br; } }
                else if (bits == 16 && !best) { best = t; }
            }
            if (!best && n) fprintf(stderr, "note: %lu available types, none matched %u Hz %u ch\n", (unsigned long)n, rate, ch);
            if (best) { best->CopyAllItems(aa.Get()); filled = true; }
        }
    }
    if (!filled) {
        aa->SetGUID(MF_MT_SUBTYPE, sub);
        aa->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        aa->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
        aa->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        if (kind == L"aac") aa->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 20000);
        if (kind == L"mp3") aa->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
        if (kind == L"wma") aa->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 16000);
    }
    fprintf(stderr, "step: filled=%d\n", (int)filled);
    hr = prof->SetAudioAttributes(aa.Get()); if (FAILED(hr)) return Fail("SetAudioAttributes", hr);
    ComPtr<IMFAttributes> ca; MFCreateAttributes(&ca, 2);
    ca->SetGUID(MF_TRANSCODE_CONTAINERTYPE, container);
    hr = prof->SetContainerAttributes(ca.Get()); if (FAILED(hr)) return Fail("SetContainerAttributes", hr);

    fprintf(stderr, "step: profile ready\n");
    ComPtr<IMFTopology> topo;
    hr = MFCreateTranscodeTopology(src.Get(), argv[2], prof.Get(), &topo); if (FAILED(hr)) return Fail("CreateTranscodeTopology", hr);
    ComPtr<IMFMediaSession> sess; hr = MFCreateMediaSession(nullptr, &sess); if (FAILED(hr)) return Fail("session", hr);
    hr = sess->SetTopology(0, topo.Get()); if (FAILED(hr)) return Fail("SetTopology", hr);
    PROPVARIANT v; PropVariantInit(&v);
    hr = sess->Start(&GUID_NULL, &v); if (FAILED(hr)) return Fail("Start", hr);
    for (;;) {
        ComPtr<IMFMediaEvent> ev; hr = sess->GetEvent(0, &ev); if (FAILED(hr)) return Fail("GetEvent", hr);
        MediaEventType t = MEUnknown; ev->GetType(&t);
        HRESULT st = S_OK; ev->GetStatus(&st);
        if (FAILED(st)) return Fail("event status", st);
        if (t == MESessionEnded) break;
    }
    // 出力ファイルの仕上げ（moov など）は Close の完了を待たないと書かれない
    sess->Close();
    for (;;) {
        ComPtr<IMFMediaEvent> ev; if (FAILED(sess->GetEvent(0, &ev))) break;
        MediaEventType t = MEUnknown; ev->GetType(&t);
        HRESULT st2 = S_OK; ev->GetStatus(&st2);
        fprintf(stderr, "close event %lu status 0x%08lx\n", (unsigned long)t, (unsigned long)st2);
        if (t == MESessionClosed) break;
    }
    src->Shutdown(); sess->Shutdown();
    MFShutdown();
    wprintf(L"ok %s\n", argv[2]);
    return 0;
}
