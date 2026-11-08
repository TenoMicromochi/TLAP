// 確認用：既定の出力デバイスの音を WASAPI ループバックで録って、0.25 秒ごとの大きさと音の高さ（ゼロ交差から）を出す。
//   probe.exe <秒> [出力csv]      csv: 経過ms, rms(0..1), 周波数Hz(左ch), チャンネル別rms
// 出力デバイスが 48kHz のときの鳴り方（速さ・高さ）を、耳でなく数で確かめるための道具。build は tools\build_tools.bat
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>
#pragma comment(lib, "ole32.lib")
using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t** argv) {
    double secs = argc > 1 ? _wtof(argv[1]) : 5;
    FILE* csv = nullptr; if (argc > 2) _wfopen_s(&csv, argv[2], L"w");
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IMMDeviceEnumerator> en; CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    ComPtr<IMMDevice> dev; if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) { puts("no device"); return 1; }
    ComPtr<IAudioClient> cl; dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&cl);
    WAVEFORMATEX* mix = nullptr; cl->GetMixFormat(&mix);
    printf("mix: %lu Hz %u ch %u bit tag %u\n", mix->nSamplesPerSec, mix->nChannels, mix->wBitsPerSample, mix->wFormatTag);
    if (FAILED(cl->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, mix, nullptr))) { puts("init failed"); return 1; }
    ComPtr<IAudioCaptureClient> cap; cl->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
    cl->Start();
    int ch = mix->nChannels; int rate = mix->nSamplesPerSec;
    int win = rate / 4;
    std::vector<float> L, R;   // 窓ぶんためる（float 前提。共有モードのミックスは通常 float32）
    LARGE_INTEGER f, t0; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    bool isFloat = mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT || (mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && mix->wBitsPerSample == 32);
    double peak = 0;
    for (;;) {
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        double el = (double)(t.QuadPart - t0.QuadPart) / f.QuadPart;
        if (el > secs) break;
        UINT32 n = 0; cap->GetNextPacketSize(&n);
        if (!n) { Sleep(5); continue; }
        BYTE* d; DWORD fl; UINT32 got; cap->GetBuffer(&d, &got, &fl, nullptr, nullptr);
        for (UINT32 i = 0; i < got; i++) {
            float l = 0, r = 0;
            if (!(fl & AUDCLNT_BUFFERFLAGS_SILENT) && isFloat) { l = ((float*)d)[i * ch]; r = ch > 1 ? ((float*)d)[i * ch + 1] : l; }
            L.push_back(l); R.push_back(r);
            if (std::fabs(l) > peak) peak = std::fabs(l);
            if (std::fabs(r) > peak) peak = std::fabs(r);
            if ((int)L.size() >= win) {
                double sl = 0, sr = 0; int cross = 0;
                for (int k = 0; k < win; k++) { sl += L[k] * L[k]; sr += R[k] * R[k]; if (k && ((L[k - 1] < 0) != (L[k] < 0)) && std::fabs(L[k]) + std::fabs(L[k - 1]) > 0.0005) cross++; }
                double rl = std::sqrt(sl / win), rr = std::sqrt(sr / win);
                QueryPerformanceCounter(&t);
                double ms = (double)(t.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
                double hz = cross / 2.0 / ((double)win / rate);
                printf("%7.0f ms  rmsL %.4f rmsR %.4f  ~%.0f Hz\n", ms, rl, rr, rl > 0.001 ? hz : 0.0);
                if (csv) fprintf(csv, "%.0f,%.5f,%.0f,%.5f,%.5f\n", ms, (rl + rr) / 2, rl > 0.001 ? hz : 0.0, rl, rr);
                L.clear(); R.clear();
            }
        }
        cap->ReleaseBuffer(got);
    }
    printf("peak %.4f\n", peak);
    cl->Stop(); if (csv) fclose(csv);
    return 0;
}
