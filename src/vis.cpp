#include "vis.h"
#include "util.h"
#include "ui.h"
#include "lang.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <shlwapi.h>
#include <thread>
#include <memory>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace vis {
namespace {

// ---------------------------------------------------------------- 共有の状態
struct Entry { std::wstring file, name; };
std::mutex g_m;                       // 一覧・選択・エラー・コンパイル結果
std::vector<Entry> g_list;
std::wstring g_cur, g_err;
FILETIME g_curTime = {};              // 今のシェーダーを最後にコンパイルしたときの更新時刻
unsigned g_req = 0;                   // コンパイル要求の番号（古い結果は捨てる）

struct Compiled { bool ok = false; unsigned req = 0; ComPtr<ID3DBlob> code; int pixel = 2; std::wstring err; std::wstring file; };
Compiled g_pending; bool g_hasPending = false;
bool g_clearShader = false;           // None にされた：描画スレッドが今のシェーダーを捨てる

HWND g_wnd = nullptr, g_parent = nullptr;
std::thread g_renderThr, g_watchThr;
std::atomic<bool> g_quit{ false };
HANDLE g_evt = nullptr;               // 描画スレッドの起床
std::atomic<int> g_scale{ 1 };
std::atomic<bool> g_paneVisible{ false }, g_minimized{ false }, g_loseDevice{ false };
std::atomic<int> g_compiling{ 0 };
std::atomic<bool> g_playingFlag{ false };
std::wstring g_nameText; ULONGLONG g_nameUntil = 0; bool g_nameDirty = false;   // 名前の表示（g_m で守る）
std::atomic<unsigned long long> g_frames{ 0 };
HANDLE g_dirHandle = INVALID_HANDLE_VALUE;
HMODULE g_d3dc = nullptr;
typedef HRESULT(WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
PFN_Compile g_compile = nullptr;
std::wstring g_dllErr;

// 音声スレッド → 描画スレッド：ロックなしのリング（フレーム = L,R）
const size_t kRing = 16384;
float g_ring[kRing * 2];
std::atomic<size_t> g_wr{ 0 }, g_rd{ 0 };    // 通し番号（フレーム）
std::atomic<int> g_feedRate{ 48000 };
std::atomic<bool> g_wantFeed{ false };

std::wstring ExeDir() {
    wchar_t b[MAX_PATH]; GetModuleFileNameW(nullptr, b, MAX_PATH);
    std::wstring s = b; return s.substr(0, s.find_last_of(L'\\'));
}
std::wstring VisDir() { return ExeDir() + L"\\visualizers"; }

void Notify() { if (g_parent) PostMessageW(g_parent, WM_VIS, 0, 0); }
void SetError(const std::wstring& e) { { std::lock_guard<std::mutex> lk(g_m); g_err = e; } Notify(); }

bool ReadText(const std::wstring& path, std::string& out, bool& tooBig) {
    tooBig = false;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > 256 * 1024) { CloseHandle(h); tooBig = true; return false; }
    out.resize((size_t)sz.QuadPart); DWORD got = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, &out[0], (DWORD)out.size(), &got, nullptr);
    CloseHandle(h); out.resize(got);
    if (out.size() >= 3 && (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF) out.erase(0, 3);   // BOM
    return ok != 0;
}

// 先頭の // コメントから name: と pixel: を読む
void ParseHeader(const std::string& src, std::wstring& name, int& pixel) {
    pixel = 2;
    size_t pos = 0; int lines = 0;
    while (pos < src.size() && lines < 30) {
        size_t e = src.find('\n', pos); if (e == std::string::npos) e = src.size();
        std::string ln = src.substr(pos, e - pos); pos = e + 1; lines++;
        size_t i = ln.find_first_not_of(" \t\r"); if (i == std::string::npos) continue;
        if (ln.compare(i, 2, "//") != 0) continue;
        i = ln.find_first_not_of(" \t", i + 2); if (i == std::string::npos) continue;
        auto val = [&](const char* key, std::string& v) {
            size_t k = strlen(key); if (ln.compare(i, k, key) != 0) return false;
            size_t s = ln.find_first_not_of(" \t", i + k); if (s == std::string::npos) { v = ""; return true; }
            size_t t = ln.find_last_not_of(" \t\r"); v = ln.substr(s, t - s + 1); return true; };
        std::string v;
        if (val("name:", v)) { if (!v.empty()) { int w = MultiByteToWideChar(CP_UTF8, 0, v.c_str(), (int)v.size(), nullptr, 0); name.assign(w, 0); MultiByteToWideChar(CP_UTF8, 0, v.c_str(), (int)v.size(), &name[0], w); } }
        else if (val("pixel:", v)) pixel = std::max(1, std::min(16, atoi(v.c_str())));
    }
}

// ---------------------------------------------------------------- HLSL の前置き（SPEC「書き方」）
const char* kPrelude =
    "cbuffer TLAPCB : register(b0) {\n"
    "  float2 iResolution; float iTime; float iTimeDelta;\n"
    "  int iFrame; float playing; float level; float bass;\n"
    "  float mid; float treble; float wavePeak; int _histHead;\n"
    "};\n"
    "Texture2D<float> gAudio : register(t0);\n"
    "Texture2D<float4> gPrev : register(t1);\n"
    "Texture2D<float> gHist : register(t2);\n"
    "SamplerState gPrevS : register(s0);\n"
    "float _row(int row, int n, float x) {\n"
    "  float p = saturate(x) * (n - 1); int i0 = (int)floor(p); int i1 = min(i0 + 1, n - 1); float f = p - i0;\n"
    "  return lerp(gAudio.Load(int3(i0, row, 0)), gAudio.Load(int3(i1, row, 0)), f);\n"
    "}\n"
    "float band(float x) { return _row(1, 64, x); }\n"
    "float fftAt(float x) { return _row(0, 1024, x); }\n"
    "float2 fftLR(float x) { return float2(_row(5, 1024, x), _row(6, 1024, x)); }\n"
    "float waveAt(float x) { return _row(2, 512, x); }\n"
    "float2 waveLR(float x) { return float2(_row(3, 512, x), _row(4, 512, x)); }\n"
    "static const int HIST = 128;\n"
    "float _hrow(int base, int n, float x, int age) {\n"
    "  int row = base + ((_histHead - clamp(age, 0, 127)) & 127);\n"
    "  float p = saturate(x) * (n - 1); int i0 = (int)floor(p); int i1 = min(i0 + 1, n - 1); float f = p - i0;\n"
    "  return lerp(gHist.Load(int3(i0, row, 0)), gHist.Load(int3(i1, row, 0)), f);\n"
    "}\n"
    "float waveHist(float x, int age) { return _hrow(0, 512, x, age); }\n"
    "float2 waveLRHist(float x, int age) { return float2(_hrow(128, 512, x, age), _hrow(256, 512, x, age)); }\n"
    "float bandHist(float x, int age) { return _hrow(384, 64, x, age); }\n"
    "float wavePeakHist(int age) { return gHist.Load(int3(0, 512 + ((_histHead - clamp(age, 0, 127)) & 127), 0)); }\n"
    "float3 prevFrame(float2 uv) { return gPrev.SampleLevel(gPrevS, float2(uv.x, 1.0 - uv.y), 0).rgb; }\n"
    "#line 1\n";
const char* kEntry =
    "\nfloat4 TLAP_main(float4 sv : SV_Position) : SV_Target {\n"
    "  float4 c = mainImage(float2(sv.x, iResolution.y - sv.y));\n"
    "  return float4(saturate(c.rgb), 1);\n"
    "}\n";
const char* kBlit =
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSOut VSMain(uint id : SV_VertexID) { VSOut o; float2 uv = float2((id << 1) & 2, id & 2); o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }\n"
    "Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
    "float4 PSBlit(VSOut i) : SV_Target { return tex.SampleLevel(smp, i.uv, 0); }\n";

bool LoadCompiler() {
    if (g_compile) return true;
    if (!g_d3dc) g_d3dc = LoadLibraryExW(L"d3dcompiler_47.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!g_d3dc) { g_dllErr = lang::T(lang::VIS_NODLL); return false; }
    g_compile = (PFN_Compile)GetProcAddress(g_d3dc, "D3DCompile");
    if (!g_compile) { g_dllErr = lang::T(lang::VIS_NODLL); return false; }
    return true;
}

// エラーの1行目を「line N: メッセージ」にする（行番号は書いた .hlsl の行番号：前置きの後に #line 1 を入れてある）
std::wstring FirstErrorLine(const char* msg) {
    std::string s = msg ? msg : "";
    size_t e = s.find_first_of("\r\n"); if (e != std::string::npos) s.resize(e);
    // 例： shader(12,5): error X3000: syntax error ...
    size_t lp = s.find('('), rp = s.find(')');
    std::string out = s;
    if (lp != std::string::npos && rp != std::string::npos && rp > lp) {
        int line = atoi(s.c_str() + lp + 1);
        size_t colon = s.find(':', rp);
        out = "line " + std::to_string(line) + ": " + (colon == std::string::npos ? s.substr(rp + 1) : s.substr(colon + 1));
        while (!out.empty() && out.back() == ' ') out.pop_back();
        size_t sp = out.find(": ") ; (void)sp;
    }
    int w = MultiByteToWideChar(CP_UTF8, 0, out.c_str(), (int)out.size(), nullptr, 0);
    std::wstring ws(w, 0); MultiByteToWideChar(CP_UTF8, 0, out.c_str(), (int)out.size(), &ws[0], w);
    while (!ws.empty() && ws[0] == L' ') ws.erase(0, 1);
    return ws;
}

// 別スレッドで読んで・検査して・コンパイルする。結果は g_pending へ（古い要求なら捨てる）
void CompileAsync(const std::wstring& file) {
    unsigned req; { std::lock_guard<std::mutex> lk(g_m); req = ++g_req; }
    g_compiling++;
    std::thread([file, req] {
        Compiled r; r.req = req; r.file = file;
        std::wstring path = VisDir() + L"\\" + file;
        std::string src; bool big = false;
        FILETIME ft = {}; { WIN32_FILE_ATTRIBUTE_DATA d; if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) ft = d.ftLastWriteTime; }
        if (!LoadCompiler()) r.err = g_dllErr;
        else if (!ReadText(path, src, big)) r.err = big ? lang::T(lang::VIS_BIG) : lang::T(lang::VIS_READ);
        else {
            std::wstring nm; ParseHeader(src, nm, r.pixel);
            // #include は使えない（include ハンドラを渡さない。念のため自分でも見て、行番号つきで知らせる）
            int line = 1; size_t pos = 0; bool inc = false;
            while (pos <= src.size() && !inc) {
                size_t e = src.find('\n', pos); if (e == std::string::npos) e = src.size();
                size_t i = src.find_first_not_of(" \t", pos);
                if (i != std::string::npos && i < e && src.compare(i, 8, "#include") == 0) { r.err = L"line " + std::to_wstring(line) + L": " + std::wstring(lang::T(lang::VIS_INC)); inc = true; }
                pos = e + 1; line++;
            }
            if (!inc) {
                std::string full = std::string(kPrelude) + src + kEntry;
                ComPtr<ID3DBlob> code, errs;
                HRESULT hr = g_compile(full.data(), full.size(), "shader", nullptr, nullptr, "TLAP_main", "ps_5_0", 0, 0, &code, &errs);
                if (FAILED(hr) || !code) r.err = FirstErrorLine(errs ? (const char*)errs->GetBufferPointer() : "compile failed");
                else { r.ok = true; r.code = code; }
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_m);
            if (req == g_req && g_cur == file) { g_pending = r; g_hasPending = true; g_curTime = ft; }
        }
        Trace(L"vis: compile %s -> %s %s", file.c_str(), r.ok ? L"ok" : L"FAILED", r.err.c_str());
        if (g_evt) SetEvent(g_evt);
        g_compiling--;
    }).detach();
}

// ---------------------------------------------------------------- 解析（FFT）
const int kN = 2048;
struct Analyzer {
    float L[kN] = {}, R[kN] = {};
    float fftL[1024] = {}, fftR[1024] = {}, fft[1024] = {}, band[64] = {}, waveM[512] = {}, waveL[512] = {}, waveR[512] = {};
    float level = 0, bass = 0, mid = 0, treble = 0, wavePeak = 0;
    float win[kN];
    Analyzer() { for (int i = 0; i < kN; i++) win[i] = 0.5f - 0.5f * (float)std::cos(2 * 3.14159265358979 * i / kN); }

    static void Fft(float* re, float* im) {
        for (int i = 1, j = 0; i < kN; i++) { int bit = kN >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
        for (int len = 2; len <= kN; len <<= 1) {
            double ang = -2 * 3.14159265358979 / len; float wr = (float)std::cos(ang), wi = (float)std::sin(ang);
            for (int i = 0; i < kN; i += len) {
                float cr = 1, ci = 0;
                for (int k = 0; k < len / 2; k++) {
                    int a = i + k, b = i + k + len / 2;
                    float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                    float nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
    }
    static float ToUnit(double amp) { double db = 20 * std::log10(std::max(amp, 1e-9)); return (float)std::max(0.0, std::min(1.0, (db + 60) / 60)); }

    void Push(const float* st, size_t n) {   // 新しい標本を末尾へ（n フレーム。st == nullptr なら無音）
        if (n >= (size_t)kN) { st += (n - kN) * 2; n = kN; }
        memmove(L, L + n, (kN - n) * sizeof(float)); memmove(R, R + n, (kN - n) * sizeof(float));
        for (size_t i = 0; i < n; i++) { L[kN - n + i] = st ? st[i * 2] : 0; R[kN - n + i] = st ? st[i * 2 + 1] : 0; }
    }
    // 大きさ：上がるときは即、下がるときは 1 秒で 0 まで
    static void Smooth(float& cur, float tgt, float dt) { cur = std::max(tgt, cur - dt); }
    void Analyze(int rate, float dt) {
        static float re[kN], im[kN]; double sq = 0;
        for (int i = 0; i < kN; i++) { float m = (L[i] + R[i]) * 0.5f; re[i] = m * win[i]; im[i] = 0; sq += (double)m * m; }
        Fft(re, im);
        float mag[1024];
        for (int k = 0; k < 1024; k++) mag[k] = ToUnit(2.0 * std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]) / (kN / 2.0));   // 全振幅の正弦波で 1.0（Hann の総和 = N/2）
        for (int k = 0; k < 1024; k++) Smooth(fft[k], mag[k], dt);
        for (int ch = 0; ch < 2; ch++) {   // 左右それぞれの FFT（fftLR 用。換算はモノラルと同じ）
            const float* src = ch ? R : L; float* dst = ch ? fftR : fftL;
            for (int i = 0; i < kN; i++) { re[i] = src[i] * win[i]; im[i] = 0; }
            Fft(re, im);
            for (int k = 0; k < 1024; k++) Smooth(dst[k], ToUnit(2.0 * std::sqrt((double)re[k] * re[k] + (double)im[k] * im[k]) / (kN / 2.0)), dt);
        }
        double binHz = (double)rate / kN;
        auto at = [&](double pos) { int i = (int)pos; if (i < 0) return 0.f; if (i >= 1023) return mag[1023]; float f = (float)(pos - i); return mag[i] * (1 - f) + mag[i + 1] * f; };
        for (int b = 0; b < 64; b++) {
            double f0 = 20.0 * std::pow(1000.0, b / 64.0), f1 = 20.0 * std::pow(1000.0, (b + 1) / 64.0);
            double lo = f0 / binHz, hi = f1 / binHz; float v = 0;
            if (hi - lo < 1.0) v = at((lo + hi) * 0.5);
            else for (int k = (int)std::ceil(lo); k <= (int)std::floor(hi) && k < 1024; k++) v = std::max(v, mag[k]);
            Smooth(band[b], v, dt);
        }
        auto range = [&](double f0, double f1) { float v = 0; for (int k = (int)std::ceil(f0 / binHz); k <= (int)std::floor(f1 / binHz) && k < 1024; k++) v = std::max(v, mag[k]); return v; };
        Smooth(bass, range(20, 250), dt); Smooth(mid, range(250, 4000), dt); Smooth(treble, range(4000, 20000), dt);
        Smooth(level, ToUnit(std::sqrt(sq / kN)), dt);
        {   // 波形のピーク（線形 0..1）：上がりは即、下がりは 2 秒で半分。履歴の 1 行はこの 2048 点の中から切り出すので、行の最大値を超えない
            float pk = 0; for (int i = 0; i < kN; i++) pk = std::max(pk, std::fabs((L[i] + R[i]) * 0.5f));
            wavePeak = std::max(std::min(pk, 1.0f), wavePeak * (float)std::pow(0.5, dt / 2.0));
        }
        for (int i = 0; i < 512; i++) { int s = kN - 512 + i; waveM[i] = (L[s] + R[s]) * 0.5f; waveL[i] = L[s]; waveR[i] = R[s]; }
    }
};

// ---------------------------------------------------------------- 履歴（第6段）：128 行、1 行 = 1/60 秒
// 行の並び（テクスチャの行）：0..127 モノ波形、128..255 L、256..383 R、384..511 band、512..639 wavePeak（x=0 の 1 個）。各ブロック内はリング、_histHead が最新
const int kHist = 128;
struct History {
    float wave[kHist][512], wl[kHist][512], wr[kHist][512], bnd[kHist][64], peak[kHist];
    int head = 0; double acc = 0;
    History() { memset(wave, 0, sizeof wave); memset(wl, 0, sizeof wl); memset(wr, 0, sizeof wr); memset(bnd, 0, sizeof bnd); memset(peak, 0, sizeof peak); }
    // 上がりのゼロ交差で頭を揃えて 512 個を切り出す（新しいほうの交差を探す。見つからなければ揃えない）
    void AddRow(const Analyzer& a) {
        head = (head + 1) & (kHist - 1);
        const float* L = a.L; const float* R = a.R;
        int start = kN - 512; float frac = 0; bool found = false;
        auto m = [&](int i) { return (L[i] + R[i]) * 0.5f; };
        for (int s = kN - 513; s >= 1; s--) {
            float p = m(s - 1), q = m(s);
            if (p < 0 && q >= 0) { start = s - 1; frac = -p / (q - p); found = true; break; }   // s-1 と s の間で 0 を横切る：start + frac が頭
        }
        for (int i = 0; i < 512; i++) {
            int s = found ? start + i : kN - 512 + i; float f = found ? frac : 0;
            int s2 = std::min(s + 1, kN - 1);
            float l = L[s] + (L[s2] - L[s]) * f, r = R[s] + (R[s2] - R[s]) * f;
            wl[head][i] = l; wr[head][i] = r; wave[head][i] = (l + r) * 0.5f;
        }
        memcpy(bnd[head], a.band, sizeof a.band);
        peak[head] = a.wavePeak;
    }
    void Step(const Analyzer& a, float dt) {   // 描くたびに経過時間を足して、1/60 秒たまるごとに行を足す
        acc += dt; int n = 0;
        while (acc >= 1.0 / 60 && n < 12) { acc -= 1.0 / 60; AddRow(a); n++; }
        if (acc > 1.0 / 60) acc = 0;
    }
};

// ---------------------------------------------------------------- D3D11 の描画（描画スレッドだけが触る）
struct Gpu {
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGISwapChain1> sc;
    ComPtr<ID3D11RenderTargetView> bbRtv;
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> psBlit, psUser;
    ComPtr<ID3D11Buffer> cb; ComPtr<ID3D11Texture2D> audioTex; ComPtr<ID3D11ShaderResourceView> audioSrv;
    ComPtr<ID3D11Texture2D> histTex; ComPtr<ID3D11ShaderResourceView> histSrv;
    ComPtr<ID3D11SamplerState> sBorder, sClamp;
    ComPtr<ID3D11Texture2D> cv[2]; ComPtr<ID3D11RenderTargetView> cvRtv[2]; ComPtr<ID3D11ShaderResourceView> cvSrv[2];
    int bbW = 0, bbH = 0, cvW = 0, cvH = 0;
    int pixel = 2;
    unsigned frame = 0; double time = 0;
    bool curValid = false;   // psUser がある
    ComPtr<ID3D11Texture2D> nameTex; ComPtr<ID3D11ShaderResourceView> nameSrv; int nameW = 0, nameH = 0;   // 名前の箱（物理px）
};

struct CB { float res[2]; float time, dt; int frame; float playing, level, bass; float mid, treble, wavePeak; int histHead; };

bool CreateGpu(Gpu& g, HWND wnd) {
    UINT fl = 0;
    D3D_FEATURE_LEVEL lv[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, fl, lv, 3, D3D11_SDK_VERSION, &g.dev, &got, &g.ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, fl, lv, 3, D3D11_SDK_VERSION, &g.dev, &got, &g.ctx);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIDevice> dd; ComPtr<IDXGIAdapter> ad; ComPtr<IDXGIFactory2> fac;
    if (FAILED(g.dev.As(&dd)) || FAILED(dd->GetAdapter(&ad)) || FAILED(ad->GetParent(IID_PPV_ARGS(&fac)))) return false;
    RECT rc; GetClientRect(wnd, &rc);
    DXGI_SWAP_CHAIN_DESC1 d = {}; d.Width = std::max(1L, rc.right); d.Height = std::max(1L, rc.bottom); d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1; d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; d.Scaling = DXGI_SCALING_STRETCH;
    if (FAILED(fac->CreateSwapChainForHwnd(g.dev.Get(), wnd, &d, nullptr, nullptr, &g.sc))) return false;
    fac->MakeWindowAssociation(wnd, DXGI_MWA_NO_ALT_ENTER);
    g.bbW = d.Width; g.bbH = d.Height;
    ComPtr<ID3D11Texture2D> bb; g.sc->GetBuffer(0, IID_PPV_ARGS(&bb)); g.dev->CreateRenderTargetView(bb.Get(), nullptr, &g.bbRtv);
    if (!LoadCompiler()) return false;
    ComPtr<ID3DBlob> c, e;
    if (FAILED(g_compile(kBlit, strlen(kBlit), "blit", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &c, &e))) return false;
    g.dev->CreateVertexShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &g.vs);
    c.Reset(); e.Reset();
    if (FAILED(g_compile(kBlit, strlen(kBlit), "blit", nullptr, nullptr, "PSBlit", "ps_5_0", 0, 0, &c, &e))) return false;
    g.dev->CreatePixelShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &g.psBlit);
    D3D11_BUFFER_DESC bd = {}; bd.ByteWidth = sizeof(CB); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g.dev->CreateBuffer(&bd, nullptr, &g.cb);
    D3D11_TEXTURE2D_DESC td = {}; td.Width = 1024; td.Height = 7; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC; td.BindFlags = D3D11_BIND_SHADER_RESOURCE; td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g.dev->CreateTexture2D(&td, nullptr, &g.audioTex); g.dev->CreateShaderResourceView(g.audioTex.Get(), nullptr, &g.audioSrv);
    D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER; sd.MaxLOD = D3D11_FLOAT32_MAX;
    g.dev->CreateSamplerState(&sd, &g.sBorder);
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; g.dev->CreateSamplerState(&sd, &g.sClamp);
    td.Width = 512; td.Height = 5 * kHist;
    g.dev->CreateTexture2D(&td, nullptr, &g.histTex); g.dev->CreateShaderResourceView(g.histTex.Get(), nullptr, &g.histSrv);
    return g.vs && g.psBlit && g.cb && g.audioSrv && g.histSrv && g.sBorder && g.sClamp;
}

void ClearCanvases(Gpu& g) { float z[4] = { 0, 0, 0, 1 }; for (int i = 0; i < 2; i++) if (g.cvRtv[i]) g.ctx->ClearRenderTargetView(g.cvRtv[i].Get(), z); }

void MakeCanvas(Gpu& g, int w, int h) {
    for (int i = 0; i < 2; i++) { g.cv[i].Reset(); g.cvRtv[i].Reset(); g.cvSrv[i].Reset(); }
    D3D11_TEXTURE2D_DESC td = {}; td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    for (int i = 0; i < 2; i++) { g.dev->CreateTexture2D(&td, nullptr, &g.cv[i]); g.dev->CreateRenderTargetView(g.cv[i].Get(), nullptr, &g.cvRtv[i]); g.dev->CreateShaderResourceView(g.cv[i].Get(), nullptr, &g.cvSrv[i]); }
    g.cvW = w; g.cvH = h; ClearCanvases(g);   // prevFrame は、キャンバスの大きさが変わったとき黒に戻る
}

// 描画スレッド
void RenderLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Gpu g; Analyzer an; bool haveGpu = false;
    std::unique_ptr<History> hist(new History());
    LARGE_INTEGER fq, last; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&last);
    bool wasRunning = false;
    while (!g_quit) {
        std::wstring cur; bool applyPending = false, clearShader = false; Compiled pend;
        {
            std::lock_guard<std::mutex> lk(g_m); cur = g_cur;
            if (g_clearShader) { g_clearShader = false; clearShader = true; }
        }
        bool run = g_paneVisible && !g_minimized && !cur.empty() && IsWindowVisible(g_wnd);
        g_wantFeed = run;
        if (!run && wasRunning) Trace(L"vis: stop (frames presented so far %llu)", (unsigned long long)g_frames.load());
        if (run && !wasRunning) Trace(L"vis: run (frames presented so far %llu)", (unsigned long long)g_frames.load());
        if (!run) { wasRunning = false; WaitForSingleObject(g_evt, 100); if (clearShader) g.psUser.Reset(), g.curValid = false; continue; }
        if (!haveGpu) {
            g = Gpu(); haveGpu = CreateGpu(g, g_wnd);
            if (!haveGpu) { Trace(L"vis: CreateGpu failed"); SetError(g_dllErr.empty() ? std::wstring(lang::T(lang::VIS_D3D)) : g_dllErr); WaitForSingleObject(g_evt, 500); g = Gpu(); continue; }
        }
        if (clearShader) { g.psUser.Reset(); g.curValid = false; }
        { std::lock_guard<std::mutex> lk(g_m); if (g_hasPending) { pend = g_pending; g_hasPending = false; applyPending = true; } }   // 動かせるときに受け取る（欄が見えるまで待つ）
        if (applyPending && pend.file == cur) {
            if (pend.ok) {
                ComPtr<ID3D11PixelShader> ps;
                if (SUCCEEDED(g.dev->CreatePixelShader(pend.code->GetBufferPointer(), pend.code->GetBufferSize(), nullptr, &ps))) {
                    g.psUser = ps; g.curValid = true; g.pixel = pend.pixel; g.frame = 0; g.time = 0; g.cvW = g.cvH = 0;   // 読み込み直し：時間とキャンバスを最初から
                    Trace(L"vis: shader applied (pixel %d)", pend.pixel); SetError(L"");
                } else SetError(lang::T(lang::VIS_PS));
            } else SetError(pend.err);        // 失敗：前の絵を動かし続ける
        }
        // 時間
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        float dt = (float)((double)(now.QuadPart - last.QuadPart) / fq.QuadPart); last = now;
        if (!wasRunning) { dt = 1.0f / 60; wasRunning = true; }
        dt = std::min(dt, 0.1f);
        // 音の解析
        {
            size_t w = g_wr.load(std::memory_order_acquire), r = g_rd.load(); int rate = g_feedRate.load();
            if (w - r > kRing) r = w - kRing;      // 追い越された：古いぶんは捨てる
            size_t n = w - r;
            if (n > 0) {
                std::vector<float> tmp(n * 2);
                for (size_t i = 0; i < n; i++) { size_t s = (r + i) % kRing; tmp[i * 2] = g_ring[s * 2]; tmp[i * 2 + 1] = g_ring[s * 2 + 1]; }
                an.Push(tmp.data(), n); g_rd.store(w);
            } else if (!g_playingFlag) an.Push(nullptr, std::min<size_t>(kN, (size_t)(dt * rate)));   // 一時停止中など：無音を流して、バーを落とす（再生中に1フレーム音が来ないだけのときは、そのまま）    // 音が来ていない（一時停止中など）：無音を流して、バーを落とす
            an.Analyze(rate, dt);
            hist->Step(an, dt);
#ifdef TLAP_TRACE
            { static int cnt = 0; static size_t nsum = 0; static int zero = 0; nsum += n; if (!n) zero++; if (++cnt >= 60) { float mx = 0, mr = 0; for (int i = 0; i < 512; i++) { mx = std::max(mx, std::fabs(an.waveL[i])); mr = std::max(mr, std::fabs(an.waveR[i])); } Trace(L"vis: 60 frames: fed %zu frames (%d frames with no data), waveL max %.3f waveR max %.3f level %.2f bass %.2f mid %.2f treble %.2f", nsum, zero, mx, mr, an.level, an.bass, an.mid, an.treble); cnt = 0; nsum = 0; zero = 0; } }
#endif
        }
        // スワップチェーンの大きさ
        RECT rc; GetClientRect(g_wnd, &rc);
        int W = rc.right, H = rc.bottom;
        if (W < 1 || H < 1) { WaitForSingleObject(g_evt, 50); continue; }
        if (W != g.bbW || H != g.bbH) {
            g.bbRtv.Reset(); g.ctx->ClearState();
            if (FAILED(g.sc->ResizeBuffers(0, W, H, DXGI_FORMAT_UNKNOWN, 0))) { haveGpu = false; continue; }
            ComPtr<ID3D11Texture2D> bb; g.sc->GetBuffer(0, IID_PPV_ARGS(&bb)); g.dev->CreateRenderTargetView(bb.Get(), nullptr, &g.bbRtv);
            g.bbW = W; g.bbH = H;
        }
        int ps = g.pixel * std::max(1, g_scale.load());
        int cw = std::min(4096, W / ps), ch = std::min(2048, H / ps);
        float black[4] = { 0, 0, 0, 1 };
        g.ctx->OMSetRenderTargets(1, g.bbRtv.GetAddressOf(), nullptr);
        g.ctx->ClearRenderTargetView(g.bbRtv.Get(), black);
        if (g.curValid && cw >= 1 && ch >= 1) {
            if (cw != g.cvW || ch != g.cvH) { MakeCanvas(g, cw, ch); Trace(L"vis: canvas %dx%d (pane %dx%d, dot %d px)", cw, ch, W, H, ps); }
            // 音のテクスチャ（行 0: fft 1024、1: band 64、2: 波形 512、3: L 512、4: R 512）
            D3D11_MAPPED_SUBRESOURCE m;
            if (SUCCEEDED(g.ctx->Map(g.audioTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                float* base = (float*)m.pData; size_t pitch = m.RowPitch / 4;
                memset(base, 0, m.RowPitch * 7);
                memcpy(base, an.fft, sizeof an.fft); memcpy(base + pitch, an.band, sizeof an.band); memcpy(base + pitch * 2, an.waveM, sizeof an.waveM);
                memcpy(base + pitch * 3, an.waveL, sizeof an.waveL); memcpy(base + pitch * 4, an.waveR, sizeof an.waveR);
                memcpy(base + pitch * 5, an.fftL, sizeof an.fftL); memcpy(base + pitch * 6, an.fftR, sizeof an.fftR);
                g.ctx->Unmap(g.audioTex.Get(), 0);
            }
            if (SUCCEEDED(g.ctx->Map(g.histTex.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
                char* base = (char*)m.pData;
                for (int i = 0; i < kHist; i++) {
                    memcpy(base + (size_t)i * m.RowPitch, hist->wave[i], sizeof hist->wave[i]);
                    memcpy(base + (size_t)(kHist + i) * m.RowPitch, hist->wl[i], sizeof hist->wl[i]);
                    memcpy(base + (size_t)(2 * kHist + i) * m.RowPitch, hist->wr[i], sizeof hist->wr[i]);
                    memset(base + (size_t)(3 * kHist + i) * m.RowPitch, 0, 512 * 4);
                    memcpy(base + (size_t)(3 * kHist + i) * m.RowPitch, hist->bnd[i], sizeof hist->bnd[i]);
                    memset(base + (size_t)(4 * kHist + i) * m.RowPitch, 0, 512 * 4);
                    memcpy(base + (size_t)(4 * kHist + i) * m.RowPitch, &hist->peak[i], sizeof(float));
                }
                g.ctx->Unmap(g.histTex.Get(), 0);
            }
            CB cbd = {}; cbd.res[0] = (float)cw; cbd.res[1] = (float)ch; cbd.time = (float)g.time; cbd.dt = dt; cbd.frame = (int)g.frame;
            cbd.playing = 1.0f; cbd.level = an.level; cbd.bass = an.bass; cbd.mid = an.mid; cbd.treble = an.treble; cbd.wavePeak = an.wavePeak;
            cbd.playing = g_playingFlag ? 1.f : 0.f; cbd.histHead = hist->head;
            g.ctx->UpdateSubresource(g.cb.Get(), 0, nullptr, &cbd, 0, 0);
            int cur2 = g.frame & 1, prev = cur2 ^ 1;
            ID3D11ShaderResourceView* none[2] = { nullptr, nullptr };
            g.ctx->PSSetShaderResources(0, 2, none);
            g.ctx->OMSetRenderTargets(1, g.cvRtv[cur2].GetAddressOf(), nullptr);
            D3D11_VIEWPORT vp = { 0, 0, (float)cw, (float)ch, 0, 1 }; g.ctx->RSSetViewports(1, &vp);
            g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g.ctx->IASetInputLayout(nullptr);
            g.ctx->VSSetShader(g.vs.Get(), nullptr, 0); g.ctx->PSSetShader(g.psUser.Get(), nullptr, 0);
            g.ctx->PSSetConstantBuffers(0, 1, g.cb.GetAddressOf());
            ID3D11ShaderResourceView* srv[3] = { g.audioSrv.Get(), g.cvSrv[prev].Get(), g.histSrv.Get() };
            g.ctx->PSSetShaderResources(0, 3, srv);
            g.ctx->PSSetSamplers(0, 1, g.sBorder.GetAddressOf());
            g.ctx->Draw(3, 0);
            // 整数倍・補間なしで欄の中央へ（余りのまわりは黒）
            g.ctx->PSSetShaderResources(0, 2, none);
            g.ctx->OMSetRenderTargets(1, g.bbRtv.GetAddressOf(), nullptr);
            int dw = cw * ps, dh = ch * ps;
            D3D11_VIEWPORT vp2 = { (float)((W - dw) / 2), (float)((H - dh) / 2), (float)dw, (float)dh, 0, 1 }; g.ctx->RSSetViewports(1, &vp2);
            g.ctx->PSSetShader(g.psBlit.Get(), nullptr, 0);
            g.ctx->PSSetShaderResources(0, 1, g.cvSrv[cur2].GetAddressOf());
            g.ctx->PSSetSamplers(0, 1, g.sClamp.GetAddressOf());
            g.ctx->Draw(3, 0);
            g.ctx->PSSetShaderResources(0, 2, none);
            g.frame++; g.time += dt;
        }
        {   // 名前の表示：黒地の箱に白文字（UI フォント 12px を拡大率の整数倍・補間なし）。欄の左上に 2 秒、フェードなし
            std::wstring nm; bool dirty = false; ULONGLONG until = 0;
            { std::lock_guard<std::mutex> lk(g_m); nm = g_nameText; dirty = g_nameDirty; g_nameDirty = false; until = g_nameUntil; }
            if (dirty) {
                g.nameTex.Reset(); g.nameSrv.Reset(); g.nameW = g.nameH = 0;
                if (!nm.empty()) {
                    int S = std::max(1, g_scale.load());
                    HDC dc = CreateCompatibleDC(nullptr); HFONT f = ui::CreateUiFont(1); HGDIOBJ of = SelectObject(dc, f);
                    TEXTMETRICW tm; GetTextMetricsW(dc, &tm); SIZE sz; GetTextExtentPoint32W(dc, nm.c_str(), (int)nm.size(), &sz);
                    int bw = sz.cx + 8, bh = tm.tmHeight + 4;
                    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = bw; bi.bmiHeader.biHeight = -bh; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
                    uint32_t* bits = nullptr; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
                    if (bmp && bits) {
                        HGDIOBJ ob = SelectObject(dc, bmp);
                        for (int i = 0; i < bw * bh; i++) bits[i] = 0x00000000;
                        SetTextColor(dc, RGB(255, 255, 255)); SetBkMode(dc, TRANSPARENT); ExtTextOutW(dc, 4, 2, 0, nullptr, nm.c_str(), (UINT)nm.size(), nullptr);
                        GdiFlush();
                        std::vector<uint32_t> up((size_t)bw * S * bh * S);
                        for (int y = 0; y < bh * S; y++) for (int x = 0; x < bw * S; x++) up[(size_t)y * bw * S + x] = bits[(y / S) * bw + (x / S)] | 0xFF000000u;   // 最近傍で整数倍
                        D3D11_TEXTURE2D_DESC td = {}; td.Width = bw * S; td.Height = bh * S; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                        D3D11_SUBRESOURCE_DATA sd = { up.data(), (UINT)(bw * S * 4), 0 };
                        if (SUCCEEDED(g.dev->CreateTexture2D(&td, &sd, &g.nameTex))) { g.dev->CreateShaderResourceView(g.nameTex.Get(), nullptr, &g.nameSrv); g.nameW = bw * S; g.nameH = bh * S; }
                        SelectObject(dc, ob); DeleteObject(bmp);
                    }
                    SelectObject(dc, of); DeleteObject(f); DeleteDC(dc);
                }
            }
            if (g.nameSrv && GetTickCount64() < until) {
                int S = std::max(1, g_scale.load());
                g.ctx->OMSetRenderTargets(1, g.bbRtv.GetAddressOf(), nullptr);
                D3D11_VIEWPORT vp3 = { (float)(4 * S), (float)(4 * S), (float)g.nameW, (float)g.nameH, 0, 1 }; g.ctx->RSSetViewports(1, &vp3);
                g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); g.ctx->IASetInputLayout(nullptr);
                g.ctx->VSSetShader(g.vs.Get(), nullptr, 0); g.ctx->PSSetShader(g.psBlit.Get(), nullptr, 0);
                g.ctx->PSSetShaderResources(0, 1, g.nameSrv.GetAddressOf()); g.ctx->PSSetSamplers(0, 1, g.sClamp.GetAddressOf());
                g.ctx->Draw(3, 0);
                ID3D11ShaderResourceView* none1[1] = { nullptr }; g.ctx->PSSetShaderResources(0, 1, none1);
            }
        }
        HRESULT hr = g.sc->Present(1, 0);
        g_frames++;
        bool lost = hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || g_loseDevice.exchange(false);
#ifndef TLAP_TRACE
        (void)0;
#endif
        if (lost) {   // GPU がリセットされた：D3D11 を作り直し、このシェーダーは止めて None にして、欄に GPU timeout と出す。音は止まらない
            Trace(L"vis: device lost (hr %08x) -> recreate, shader off", (unsigned)hr);
            g = Gpu(); haveGpu = CreateGpu(g, g_wnd);
            { std::lock_guard<std::mutex> lk(g_m); g_cur.clear(); g_hasPending = false; ++g_req; g_err = lang::T(lang::VIS_GPU); }
            Notify();
        }
    }
    g = Gpu();
    CoUninitialize();
}

// ---------------------------------------------------------------- ホットリロード
void WatchLoop() {
    while (!g_quit) {
        std::wstring dir = VisDir();
        HANDLE h = CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) { for (int i = 0; i < 10 && !g_quit; i++) Sleep(100); continue; }
        g_dirHandle = h;
        std::vector<char> buf(16384);
        while (!g_quit) {
            DWORD ret = 0;
            if (!ReadDirectoryChangesW(h, buf.data(), (DWORD)buf.size(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION, &ret, nullptr, nullptr)) break;
            if (g_quit) break;
            Sleep(120);   // 保存が何回かに分かれても、まとめて1回読み直す
            Rescan();
            std::wstring cur; FILETIME old; { std::lock_guard<std::mutex> lk(g_m); cur = g_cur; old = g_curTime; }
            if (!cur.empty()) {
                WIN32_FILE_ATTRIBUTE_DATA d;
                if (GetFileAttributesExW((VisDir() + L"\\" + cur).c_str(), GetFileExInfoStandard, &d) && CompareFileTime(&d.ftLastWriteTime, &old) != 0) { Trace(L"vis: file changed -> recompile %s", cur.c_str()); CompileAsync(cur); }
            }
        }
        g_dirHandle = INVALID_HANDLE_VALUE; CloseHandle(h);
    }
}

LRESULT CALLBACK VisProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_NCHITTEST) return HTTRANSPARENT;   // マウスは親（main）へ
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

bool Create(HWND parent) {
    g_parent = parent;
    WNDCLASSEXW wc = { sizeof wc }; wc.lpfnWndProc = VisProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"TLAPVis"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);
    g_wnd = CreateWindowExW(0, L"TLAPVis", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, parent, nullptr, wc.hInstance, nullptr);
    if (!g_wnd) return false;
    g_evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Rescan();
    g_renderThr = std::thread(RenderLoop);
    g_watchThr = std::thread(WatchLoop);
    return true;
}

void Shutdown() {
    g_quit = true; if (g_evt) SetEvent(g_evt);
    if (g_dirHandle != INVALID_HANDLE_VALUE) CancelIoEx(g_dirHandle, nullptr);
    if (g_watchThr.joinable()) g_watchThr.join();
    if (g_renderThr.joinable()) g_renderThr.join();
    for (int i = 0; i < 300 && g_compiling > 0; i++) Sleep(10);
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; }
}

HWND Hwnd() { return g_wnd; }
void Move(int x, int y, int w, int h) {
    if (!g_wnd) return;
    SetWindowPos(g_wnd, nullptr, x, y, std::max(1, w), std::max(1, h), SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_evt) SetEvent(g_evt);
}
void SetScale(int s) { g_scale = std::max(1, s); }
void SetPaneVisible(bool v) { g_paneVisible = v; if (g_wnd) ShowWindow(g_wnd, v ? SW_SHOWNA : SW_HIDE); if (g_evt) SetEvent(g_evt); }
void SetMinimized(bool m) { g_minimized = m; if (g_evt) SetEvent(g_evt); }

void Rescan() {
    std::vector<Entry> v;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((VisDir() + L"\\*.hlsl").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            Entry e; e.file = fd.cFileName; e.name = StemOf(e.file);
            std::string src; bool big; ReadText(VisDir() + L"\\" + e.file, src, big);
            if (src.size() > 4096) src.resize(4096);
            std::wstring nm; int px; ParseHeader(src, nm, px); if (!nm.empty()) e.name = nm;
            v.push_back(e);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return StrCmpLogicalW(a.file.c_str(), b.file.c_str()) < 0; });
    { std::lock_guard<std::mutex> lk(g_m); g_list = v; }
    Notify();
}
int Count() { std::lock_guard<std::mutex> lk(g_m); return (int)g_list.size(); }
std::wstring NameAt(int i) { std::lock_guard<std::mutex> lk(g_m); return i >= 0 && i < (int)g_list.size() ? g_list[i].name : L""; }
std::wstring FileAt(int i) { std::lock_guard<std::mutex> lk(g_m); return i >= 0 && i < (int)g_list.size() ? g_list[i].file : L""; }

bool Select(const std::wstring& file, bool announce) {
    if (file.empty()) {   // None（名前は出さない）
        { std::lock_guard<std::mutex> lk(g_m); g_nameText.clear(); g_nameDirty = true; g_nameUntil = 0; }
        { std::lock_guard<std::mutex> lk(g_m); g_cur.clear(); g_err.clear(); g_hasPending = false; ++g_req; g_clearShader = true; }
        Notify(); if (g_evt) SetEvent(g_evt); return true;
    }
    { std::lock_guard<std::mutex> lk(g_m); g_cur = file; g_err.clear(); }
    { std::wstring nm = file; { std::lock_guard<std::mutex> lk(g_m); for (auto& e : g_list) if (!_wcsicmp(e.file.c_str(), file.c_str())) nm = e.name; } Trace(L"vis: select %s (name: %s)", file.c_str(), nm.c_str());
      if (announce) { std::lock_guard<std::mutex> lk(g_m); g_nameText = nm; g_nameDirty = true; g_nameUntil = GetTickCount64() + 2000; if (g_evt) SetEvent(g_evt); } }
    Notify();
    CompileAsync(file);
    return true;
}
std::wstring Current() { std::lock_guard<std::mutex> lk(g_m); return g_cur; }

void Next() {
    Rescan();
    std::wstring cur = Current(); int n = Count(), idx = -1;
    for (int i = 0; i < n; i++) if (!_wcsicmp(FileAt(i).c_str(), cur.c_str())) idx = i;
    if (cur.empty()) { if (n > 0) Select(FileAt(0), true); return; }
    if (idx + 1 >= n) Select(L"", true); else Select(FileAt(idx + 1), true);
}
std::wstring CurrentName() {
    std::lock_guard<std::mutex> lk(g_m);
    if (g_cur.empty()) return L"";   // None（表示側が今の言語で出す）
    for (auto& e : g_list) if (!_wcsicmp(e.file.c_str(), g_cur.c_str())) return e.name;
    return StemOf(g_cur);
}
std::wstring Error() { std::lock_guard<std::mutex> lk(g_m); return g_err; }
bool Active() { std::lock_guard<std::mutex> lk(g_m); return !g_cur.empty(); }
unsigned long long FramesPresented() { return g_frames.load(); }

bool WantsFeed() { return g_wantFeed.load(std::memory_order_relaxed); }
void Feed(const float* st, size_t frames, int rate) {
    g_feedRate = rate;
    size_t w = g_wr.load(std::memory_order_relaxed);
    for (size_t i = 0; i < frames; i++) { size_t s = (w + i) % kRing; g_ring[s * 2] = st[i * 2]; g_ring[s * 2 + 1] = st[i * 2 + 1]; }
    g_wr.store(w + frames, std::memory_order_release);
}
void DebugLoseDevice() { g_loseDevice = true; if (g_evt) SetEvent(g_evt); }

void SetPlaying(bool p) { g_playingFlag = p; }

}  // namespace vis
