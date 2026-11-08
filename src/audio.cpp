#include "audio.h"
#include "resample.h"
#include "eq.h"
#include "vis.h"
#include "util.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace audio {
namespace {

const int kChunk = 4096;   // デコードスレッドが一度に読むフレーム数（曲のレートで）

// 1曲ぶんの流れ。ring は float 2ch のリングバッファ（出力のレートで。フレーム単位。g_m で守る）
struct Stream {
    std::wstring path;
    Decoder dec;
    AudioInfo info;
    Resampler rs;                // レートが違うときだけ使う（decode スレッドだけが触る）
    bool useRs = false;
    int outRate = 0;             // ring のレート（デバイスのミックスレート）
    std::vector<float> ring;
    size_t cap = 0, rd = 0, cnt = 0, need = 0;
    bool eos = false;            // デコードが終わった（読み残しはリングの中だけ）
    std::vector<float> tail;     // レート変換の尾（Drain の出力）。次の曲の頭に足す
    uint64_t inFrames = 0, outMain = 0;   // 入れた入力のフレーム数 / ring に入れた（尾を除く）出力のフレーム数
    bool finished = false;       // 最後まで出し切って AE_ENDED を投げた
    double base = 0;             // 最後のシーク（または頭）の位置（秒）。consumed はそこからの数
    uint64_t consumed = 0;       // リングから取り出したフレーム数
    double pendingSeek = -1;     // デコードスレッドが行うシーク（秒）。負なら無し
    uint64_t seekGen = 0;
    unsigned gen = 0;

    size_t free() const { return cap - cnt; }
    void push(const float* in, size_t n) {
        size_t w = (rd + cnt) % cap;
        size_t first = std::min(n, cap - w);
        memcpy(&ring[w * 2], in, first * 8);
        if (n > first) memcpy(&ring[0], in + first * 2, (n - first) * 8);
        cnt += n;
    }
    size_t pop(float* out, size_t n) {
        n = std::min(n, cnt);
        size_t first = std::min(n, cap - rd);
        memcpy(out, &ring[rd * 2], first * 8);
        if (n > first) memcpy(out + first * 2, &ring[0], (n - first) * 8);
        rd = (rd + n) % cap; cnt -= n;
        return n;
    }
    void clear() { rd = cnt = 0; }
};

std::mutex g_m;
std::condition_variable g_cv;                 // デコードスレッドの起床（空きができた・シーク・新しい曲・終了）
std::shared_ptr<Stream> g_s, g_next;          // g_m で守る
unsigned g_gen = 0;                           // g_m で守る
std::atomic<bool> g_quit{ false }, g_play{ false }, g_flush{ false }, g_muted{ false }, g_devChanged{ false };
std::atomic<int> g_vol{ 30 };
std::atomic<double> g_outPos{ 0 };
std::atomic<int> g_R{ 48000 };                // 出力（デバイスのミックス）のレート
std::atomic<int> g_chOut{ 2 };                // 出力のチャンネル数
HANDLE g_ctrl = nullptr;                      // 音声スレッドへの合図（状態が変わった）
HWND g_notify = nullptr;
std::thread g_decThread, g_outThread;
bool g_inited = false;

float Gain() {
    if (g_muted) return 0.f;
    float v = g_vol / 100.f;
    return v * v;                             // 耳の感じに合わせて (v/100)^2
}

// 曲を開いて、出力のレート R に合わせた Stream を作る。失敗は null
std::shared_ptr<Stream> MakeStream(const std::wstring& path, int R) {
    auto s = std::make_shared<Stream>();
    s->path = path;
    if (!s->dec.Open(path)) return nullptr;
    s->info = s->dec.Info();
    s->outRate = R;
    if (s->info.rate != R) {
        if (!s->rs.Init(s->info.rate, R)) { Trace(L"audio: resampler init failed (%d -> %d)", s->info.rate, R); return nullptr; }
        s->useRs = true;
    }
    int mul = (R + s->info.rate - 1) / s->info.rate + 1;
    s->need = (size_t)kChunk * mul + 1024;
    s->cap = std::max<size_t>((size_t)R, s->need * 2);   // 約1秒
    s->ring.assign(s->cap * 2, 0.f);
    return s;
}

// ---------------------------------------------------------------- デコードスレッド
void DecodeLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::vector<float> tmp((size_t)kChunk * 2), conv;
    std::unique_lock<std::mutex> lk(g_m);
    while (!g_quit) {
        auto needs = [](const std::shared_ptr<Stream>& x) {
            return x && !x->finished && (x->pendingSeek >= 0 || (!x->eos && x->free() >= x->need));
        };
        std::shared_ptr<Stream> s = needs(g_s) ? g_s : (g_s && !g_s->finished && needs(g_next) ? g_next : nullptr);
        if (!s) { g_cv.wait(lk); continue; }
        if (s->pendingSeek >= 0) {
            double t = s->pendingSeek; s->pendingSeek = -1;
            lk.unlock(); s->dec.Seek(t); if (s->useRs) s->rs.Flush(); lk.lock();
            continue;
        }
        uint64_t sg = s->seekGen;
        lk.unlock();
        int n = s->dec.Read(tmp.data(), kChunk);
        conv.clear();
        std::vector<float> tail;
        if (n > 0) { if (s->useRs) s->rs.Process(tmp.data(), n, conv); }
        else if (s->useRs) s->rs.Drain(tail);     // 終わり：尾を取っておく
        lk.lock();
        if ((g_s != s && g_next != s) || s->seekGen != sg) continue;   // 読んでいる間にシークされた・曲が替わった：捨てる
        if (n > 0) {
            const float* src = s->useRs ? conv.data() : tmp.data();
            size_t fr = s->useRs ? conv.size() / 2 : (size_t)n;
            if (fr > s->free()) fr = s->free();   // 念のため（need で余裕を取ってある）
            if (fr) s->push(src, fr);
            s->inFrames += (uint64_t)n; s->outMain += fr;
        } else {
            s->tail = std::move(tail);
            if (s->useRs) {   // 尾の最初の k フレームは、次の曲より前（今の曲の理想の長さまで）に出す。残りを次の曲の頭に足す
                long long ideal = (long long)std::llround((double)s->inFrames * s->outRate / s->info.rate);
                long long k = ideal - (long long)s->outMain;
                Trace(L"audio: resampler tail %d frames, main %llu, ideal %lld, k = %lld", (int)(s->tail.size() / 2), (unsigned long long)s->outMain, ideal, k);
                if (k > 0) {
                    size_t kk = std::min<size_t>((size_t)k, s->tail.size() / 2);
                    if (kk + 8 <= s->free()) {
                        s->push(s->tail.data(), kk); s->tail.erase(s->tail.begin(), s->tail.begin() + kk * 2);
                        // 尾が k に足りないぶん（1フレームほど）は、直線で伸ばして埋める（次の曲の頭を理想の位置に置くため）
                        for (long long e = (long long)kk; e < k && s->cnt > 1; e++) {
                            size_t li = (s->rd + s->cnt - 1) % s->cap, pi = (s->rd + s->cnt - 2) % s->cap; float add[2];
                            for (int c = 0; c < 2; c++) add[c] = 2 * s->ring[li * 2 + c] - s->ring[pi * 2 + c];
                            s->push(add, 1);
                        }
                    }
                }
            }
            s->eos = true;                        // 0 = 終わり、-1 = 読めなくなった（どちらも曲の終わりとして扱う）
        }
        if (s->eos) SetEvent(g_ctrl);
    }
    CoUninitialize();
}

// ---------------------------------------------------------------- 音声スレッド（WASAPI）
struct Out {
    ComPtr<IAudioClient> cl;
    ComPtr<IAudioRenderClient> rc;
    int rate = 0, ch = 2;
    UINT32 bufFrames = 0;
    UINT32 target = 0;      // バッファに溜めておくフレーム数（約 60ms。止めたときの遅れを小さくする）
};

void Teardown(Out& o, bool& started) {
    if (o.cl && started) o.cl->Stop();
    started = false;
    o.rc.Reset(); o.cl.Reset(); o.rate = 0;
}

// 既定のデバイスのミックス形式で作る（float 32bit 前提。共有モードのミックスは通常そうなる）
bool InitOut(IMMDeviceEnumerator* en, Out& o, HANDLE ev) {
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev) return false;
    ComPtr<IAudioClient> cl;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&cl)) || !cl) return false;
    WAVEFORMATEX* mix = nullptr;
    if (FAILED(cl->GetMixFormat(&mix)) || !mix) return false;
    int rate = (int)mix->nSamplesPerSec, ch = mix->nChannels, bits = mix->wBitsPerSample;
    HRESULT hr = cl->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 1000000, 0, mix, nullptr);
    CoTaskMemFree(mix);
    if (FAILED(hr)) { Trace(L"audio: Initialize failed %08x", (unsigned)hr); return false; }
    if (bits != 32 || ch < 1 || ch > 16) { Trace(L"audio: unsupported mix format (%d bit, %d ch)", bits, ch); return false; }
    if (FAILED(cl->SetEventHandle(ev))) return false;
    UINT32 bf = 0; if (FAILED(cl->GetBufferSize(&bf)) || !bf) return false;
    ComPtr<IAudioRenderClient> rc;
    if (FAILED(cl->GetService(__uuidof(IAudioRenderClient), (void**)&rc)) || !rc) return false;
    o.cl = cl; o.rc = rc; o.rate = rate; o.ch = ch; o.bufFrames = bf;
    o.target = std::min<UINT32>(bf, (UINT32)rate * 60 / 1000);
    g_R = rate; g_chOut = ch;
    Trace(L"audio: client (device mix) %d Hz %d ch buffer %u target %u", rate, ch, bf, o.target);
    return true;
}

// 既定のデバイスが変わった知らせ
class DevNotify : public IMMNotificationClient {
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID r, void** p) override {
        if (r == __uuidof(IUnknown) || r == __uuidof(IMMNotificationClient)) { *p = this; return S_OK; }
        *p = nullptr; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow f, ERole r, LPCWSTR) override {
        if (f == eRender && r == eConsole) { g_devChanged = true; if (g_ctrl) SetEvent(g_ctrl); Trace(L"audio: default device changed"); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { g_devChanged = true; if (g_ctrl) SetEvent(g_ctrl); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};
DevNotify g_devNotify;

// ミックスのレートが変わった：曲を今の位置から、新しいレートで開き直す
void RebuildStreams(int R) {
    std::shared_ptr<Stream> cur, nx; double pos = g_outPos.load();
    { std::lock_guard<std::mutex> lk(g_m); cur = g_s; nx = g_next; }
    std::shared_ptr<Stream> ncur, nnx;
    if (cur) { ncur = MakeStream(cur->path, R); if (ncur) { ncur->gen = cur->gen; ncur->pendingSeek = pos; ncur->seekGen = 1; ncur->base = pos; } }
    if (nx) { nnx = MakeStream(nx->path, R); if (nnx) nnx->gen = nx->gen; }
    std::lock_guard<std::mutex> lk(g_m);
    g_s = ncur; g_next = nnx;
    g_cv.notify_all();
    Trace(L"audio: streams rebuilt for %d Hz at %.2f s", R, pos);
}

void OutLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    DWORD task = 0; HANDLE mm = AvSetMmThreadCharacteristicsW(L"Audio", &task);
    ComPtr<IMMDeviceEnumerator> en;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    if (en) en->RegisterEndpointNotificationCallback(&g_devNotify);
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE waits[2] = { ev, g_ctrl };

    Out o; bool started = false;
    float cur = 0, lastTarget = 0, delta = 0; int rampLeft = 0;   // 音量の滑らかな変化（10ms）
    uint32_t trail = 0;      // 最後に音を入れた後に入れた無音のフレーム数（バッファの中の「音の残り」を数えるため）
    uint32_t silSince = 0;   // 一時停止中に続けて入れた無音のフレーム数
    std::vector<float> tmp, devbuf;
    eq::Processor eqp;                                  // EQ（音声スレッド専用）
    std::vector<float> tailBuf; size_t tailOff = 0;   // 前の曲のレート変換の尾（今の曲の頭に足す）
#ifdef TLAP_TRACE
    FILE* dump = nullptr;
    { wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH); std::wstring p = b; p = p.substr(0, p.find_last_of(L'\\') + 1) + L"TLAP_out.f32"; _wfopen_s(&dump, p.c_str(), L"wb"); }
#endif

    while (!g_quit) {
        std::shared_ptr<Stream> s;
        { std::lock_guard<std::mutex> lk(g_m); s = g_s; }
        bool flush = g_flush.exchange(false);
        bool devCh = g_devChanged.exchange(false);
        if (devCh) { Teardown(o, started); }
        if (!o.cl) {
            if (!en || !InitOut(en.Get(), o, ev)) { WaitForSingleObject(g_ctrl, 500); continue; }
            tmp.assign((size_t)o.bufFrames * 2, 0.f); devbuf.assign((size_t)o.bufFrames * o.ch, 0.f);
            cur = lastTarget = 0; rampLeft = 0; trail = silSince = 0;
            if (s && s->outRate != o.rate) { RebuildStreams(o.rate); continue; }
        }
        bool finished = s ? s->finished : true;
        if (!s || finished) {
            if (started) { o.cl->Stop(); started = false; }
            if (flush) o.cl->Reset();
            WaitForSingleObject(g_ctrl, INFINITE);
            continue;
        }
        if (flush) {   // シーク・曲の切り替え：バッファに残った古い音を捨てる
            if (started) o.cl->Stop();
            o.cl->Reset(); started = false;
            cur = 0; rampLeft = 0; lastTarget = 0; trail = silSince = 0;
            tailBuf.clear(); tailOff = 0; eqp.Reset();
        }
        bool playNow = g_play.load();
        if (playNow && !started) {
            if (FAILED(o.cl->Start())) { Teardown(o, started); WaitForSingleObject(g_ctrl, 300); continue; }
            started = true; silSince = 0;
        }
        if (!started) { WaitForSingleObject(g_ctrl, INFINITE); continue; }

        DWORD wr = WaitForMultipleObjects(2, waits, FALSE, 100);
        if (wr == WAIT_OBJECT_0 + 1) continue;   // 状態が変わった：読み直す

        UINT32 pad = 0;
        if (FAILED(o.cl->GetCurrentPadding(&pad))) { Teardown(o, started); WaitForSingleObject(g_ctrl, 300); continue; }
        UINT32 room = pad >= o.target ? 0 : o.target - pad;
        room = std::min(room, o.bufFrames - pad);

        playNow = g_play.load();
        float tgt = playNow ? Gain() : 0.f;
        int R = o.rate;
        if (tgt != lastTarget) { lastTarget = tgt; rampLeft = std::max(1, R / 100); delta = (tgt - cur) / (float)rampLeft; }
        bool consume = playNow || cur > 0.f || rampLeft > 0;

        size_t n = 0; bool eosNow = false, stale = false, switched = false; double base = 0; uint64_t cons = 0; unsigned newGen = 0;
        {
            std::lock_guard<std::mutex> lk(g_m);
            if (g_s != s) stale = true;
            else if (consume && room > 0) {
                n = s->pop(tmp.data(), room);
                s->consumed += n;
                if (n < room && s->eos && s->cnt == 0 && g_next) {   // 今の曲を出し切った：次の曲へ継ぎ目なしで移る
                    tailBuf = std::move(s->tail); tailOff = 0;
                    g_s = g_next; g_next.reset(); s = g_s; switched = true; newGen = s->gen;
                    size_t n2 = s->pop(tmp.data() + n * 2, room - n);
                    // 新しい曲の頭に、前の曲のレート変換の尾を足す（n2 フレームぶん。取り出した frames の先頭から）
                    for (size_t i = 0; i < n2 && tailOff < tailBuf.size() / 2; i++, tailOff++) {
                        tmp[(n + i) * 2] += tailBuf[tailOff * 2]; tmp[(n + i) * 2 + 1] += tailBuf[tailOff * 2 + 1];
                    }
                    s->consumed = n2; s->base = 0; n += n2;
                } else if (tailOff < tailBuf.size() / 2 && n > 0) {      // 切り替え直後に取り出せなかった分の続き
                    for (size_t i = 0; i < n && tailOff < tailBuf.size() / 2; i++, tailOff++) {
                        tmp[i * 2] += tailBuf[tailOff * 2]; tmp[i * 2 + 1] += tailBuf[tailOff * 2 + 1];
                    }
                }
            }
            if (!stale) { eosNow = s->eos && s->cnt == 0 && !g_next; base = s->base; cons = s->consumed; }
        }
        if (stale) continue;
        if (switched) { Trace(L"audio: switched to gen %u (gapless)", newGen); PostMessageW(g_notify, WM_AUDIO, AE_SWITCHED, (LPARAM)newGen); }
        if (n) g_cv.notify_one();

        UINT32 out = room;
        if (consume && eosNow && n < room) out = (UINT32)n;   // 曲の終わり：本物の音だけ入れる
        if (out > n) memset(&tmp[n * 2], 0, (size_t)(out - n) * 8);
        // EQ（デコード → レート変換の後、音量の前。オフのときは通らない）と、スペアナへ渡す（EQ の後・音量の前）
        eqp.Sync(R);
        if (n > 0 && eqp.Active()) eqp.Process(tmp.data(), n);
        if (n > 0 && vis::WantsFeed()) vis::Feed(tmp.data(), n, R);
#ifdef TLAP_TRACE
        {   // 動作の確認用：デコード後・音量の前の音の高さ（ゼロ交差）と大きさを 0.5 秒ごとに記録する
            static uint64_t acc = 0; static int cross = 0; static float prev = 0; static double sq = 0;
            for (size_t i = 0; i < n; i++) {
                float v = tmp[i * 2];
                if ((prev < 0) != (v < 0) && std::fabs(v) + std::fabs(prev) > 0.001f) cross++;
                prev = v; sq += (double)v * v; acc++;
                if (acc >= (uint64_t)R / 2) {
                    Trace(L"tone %.0f Hz rms %.3f (gen %u, base %.2f, consumed %.2f s)", cross / 2.0 / ((double)acc / R), std::sqrt(sq / acc), s->gen, base, (double)cons / R);
                    acc = 0; cross = 0; sq = 0;
                }
            }
        }
#endif
        if (consume) {
            for (size_t i = 0; i < n; i++) {
                if (rampLeft > 0) { cur += delta; if (--rampLeft == 0) cur = lastTarget; }
                tmp[i * 2] *= cur; tmp[i * 2 + 1] *= cur;
            }
        }
        if (out > 0) {
            BYTE* d = nullptr;
            if (FAILED(o.rc->GetBuffer(out, &d)) || !d) { Teardown(o, started); WaitForSingleObject(g_ctrl, 300); continue; }
            float* df = (float*)d;
            if (o.ch == 2) memcpy(df, tmp.data(), (size_t)out * 8);
            else for (UINT32 i = 0; i < out; i++) {   // 左右を最初の2チャンネルへ、残りは無音
                for (int c = 0; c < o.ch; c++) df[(size_t)i * o.ch + c] = c < 2 ? tmp[(size_t)i * 2 + c] : 0.f;
            }
            o.rc->ReleaseBuffer(out, 0);
#ifdef TLAP_TRACE
            if (dump) { fwrite(tmp.data(), 8, out, dump); fflush(dump); }
#endif
        }
        if (n > 0) trail = out - (UINT32)n; else trail += out;
        UINT32 padAfter = pad + out;
        UINT32 audioQ = padAfter > trail ? padAfter - trail : 0;   // バッファの中にまだ残っている本物の音
        double pos = base + ((double)cons - (double)audioQ) / R;
        g_outPos = std::max(base, pos);

        if (consume && eosNow && n == 0 && audioQ == 0) {   // 出し切った（次の曲は無い）
            { std::lock_guard<std::mutex> lk(g_m); if (g_s == s) s->finished = true; }
            o.cl->Stop(); started = false;
            Trace(L"audio: ended gen %u", s->gen);
            PostMessageW(g_notify, WM_AUDIO, AE_ENDED, (LPARAM)s->gen);
            continue;
        }
        if (!consume) {   // 一時停止：音の残りを無音で押し出してから止める
            silSince += out;
            if (silSince >= o.target) { o.cl->Stop(); started = false; silSince = 0; }
        } else silSince = 0;
    }
    if (o.cl && started) o.cl->Stop();
    o.rc.Reset(); o.cl.Reset();
    if (en) en->UnregisterEndpointNotificationCallback(&g_devNotify);
#ifdef TLAP_TRACE
    if (dump) fclose(dump);
#endif
    if (mm) AvRevertMmThreadCharacteristics(mm);
    CloseHandle(ev);
    CoUninitialize();
}

}  // namespace

// ---------------------------------------------------------------- 公開
bool Init(HWND notifyWnd) {
    if (g_inited) return true;
    if (!MfStart()) return false;
    g_notify = notifyWnd;
    g_ctrl = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_quit = false;
    {   // 出力のレートを先に知る（曲を開くときのレート変換に要る）
        ComPtr<IMMDeviceEnumerator> en; ComPtr<IMMDevice> dev; ComPtr<IAudioClient> cl; WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) &&
            SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
            SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&cl)) &&
            SUCCEEDED(cl->GetMixFormat(&mix)) && mix) { g_R = (int)mix->nSamplesPerSec; g_chOut = mix->nChannels; CoTaskMemFree(mix); }
    }
    g_decThread = std::thread(DecodeLoop);
    g_outThread = std::thread(OutLoop);
    g_inited = true;
    return true;
}

void Shutdown() {
    if (!g_inited) return;
    g_quit = true; g_play = false;
    SetEvent(g_ctrl); g_cv.notify_all();
    if (g_decThread.joinable()) g_decThread.join();
    if (g_outThread.joinable()) g_outThread.join();
    { std::lock_guard<std::mutex> lk(g_m); g_s.reset(); g_next.reset(); }
    CloseHandle(g_ctrl); g_ctrl = nullptr;
    MfStop();
    g_inited = false;
}

bool Open(const std::wstring& path, bool play) {
    auto s = MakeStream(path, g_R);
    if (!s) return false;
    {
        std::lock_guard<std::mutex> lk(g_m);
        s->gen = ++g_gen;
        g_s = s; g_next.reset();
    }
    g_outPos = 0;
    g_play = play;
    g_flush = true;
    SetEvent(g_ctrl); g_cv.notify_all();
    Trace(L"audio: open gen %u rate %d -> %d dur %.2f codec %s", s->gen, s->info.rate, s->outRate, s->info.duration, s->info.codec.c_str());
    return true;
}

bool QueueNext(const std::wstring& path) {
    auto s = MakeStream(path, g_R);
    if (!s) return false;
    {
        std::lock_guard<std::mutex> lk(g_m);
        if (!g_s || g_s->finished) return false;
        s->gen = ++g_gen;
        g_next = s;
    }
    g_cv.notify_all();
    Trace(L"audio: queued next gen %u (%s)", s->gen, s->info.codec.c_str());
    return true;
}

void ClearNext() { std::lock_guard<std::mutex> lk(g_m); g_next.reset(); }

void Close() {
    g_play = false;
    { std::lock_guard<std::mutex> lk(g_m); g_s.reset(); g_next.reset(); ++g_gen; }
    g_outPos = 0;
    g_flush = true;
    SetEvent(g_ctrl); g_cv.notify_all();
}

bool Loaded() { std::lock_guard<std::mutex> lk(g_m); return g_s != nullptr; }
unsigned Generation() { std::lock_guard<std::mutex> lk(g_m); return g_s ? g_s->gen : 0; }
unsigned NextGeneration() { std::lock_guard<std::mutex> lk(g_m); return g_next ? g_next->gen : 0; }

void Play() { g_play = true; SetEvent(g_ctrl); }
void Pause() { g_play = false; SetEvent(g_ctrl); }
bool Playing() { std::lock_guard<std::mutex> lk(g_m); return g_play && g_s && !g_s->finished; }

void Seek(double sec) {
    {
        std::lock_guard<std::mutex> lk(g_m);
        if (!g_s) return;
        if (sec < 0) sec = 0;
        if (g_s->info.duration > 0 && sec > g_s->info.duration) sec = g_s->info.duration;
        g_s->pendingSeek = sec; g_s->seekGen++;
        g_s->clear(); g_s->eos = false; g_s->finished = false; g_s->tail.clear();
        g_s->consumed = 0; g_s->base = sec;
    }
    g_outPos = sec;
    g_flush = true;
    SetEvent(g_ctrl); g_cv.notify_all();
    Trace(L"audio: seek %.2f", sec);
}

double Position() { return g_outPos.load(); }
double Duration() { std::lock_guard<std::mutex> lk(g_m); return g_s ? g_s->info.duration : 0; }
AudioInfo Info() { std::lock_guard<std::mutex> lk(g_m); return g_s ? g_s->info : AudioInfo(); }
int OutputRate() { return g_R; }

void SetVolume(int percent) { g_vol = std::max(0, std::min(100, percent)); if (g_ctrl) SetEvent(g_ctrl); }
int Volume() { return g_vol; }
void SetMuted(bool m) { g_muted = m; if (g_ctrl) SetEvent(g_ctrl); }
bool Muted() { return g_muted; }

}  // namespace audio
