#include "zipfs.h"
#include "folder.h"
#include "util.h"
#include <shlwapi.h>
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <cstring>
#include <new>

namespace zipfs {

// ---------------------------------------------------------------- CRC32
static uint32_t Crc32(const uint8_t* p, size_t n) {
    static uint32_t tab[256]; static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; tab[i] = c; }
        init = true;   // 先に表を作る（どのスレッドも同じ値を書くので、競合しても結果は同じ）
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = tab[(c ^ p[i]) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------- inflate（Deflate の展開。自前）
namespace {

struct Huff {
    uint16_t count[16];
    uint16_t symbol[288];
    uint16_t fast[512];   // 9ビットまでの符号：(長さ << 9) | 記号。0 = 長い符号（遅い道へ）
};

// 0 = 良い（不完全な符号も可）、-1 = 符号が多すぎる
int Construct(Huff& h, const uint8_t* lens, int n) {
    memset(h.count, 0, sizeof h.count); memset(h.fast, 0, sizeof h.fast);
    for (int i = 0; i < n; i++) h.count[lens[i]]++;
    int left = 1;
    for (int len = 1; len < 16; len++) { left <<= 1; left -= h.count[len]; if (left < 0) return -1; }
    uint16_t offs[16]; offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int i = 0; i < n; i++) if (lens[i]) h.symbol[offs[lens[i]]++] = (uint16_t)i;
    // 速い表
    uint32_t next[16]; uint32_t code = 0; next[0] = 0;
    for (int len = 1; len < 16; len++) { code = (code + (len > 1 ? h.count[len - 1] : 0)) << 1; next[len] = code; }
    for (int i = 0; i < n; i++) {
        int len = lens[i]; if (!len) continue;
        uint32_t c = next[len]++;
        if (len > 9) continue;
        uint32_t rev = 0; for (int k = 0; k < len; k++) if (c & (1u << k)) rev |= 1u << (len - 1 - k);
        for (uint32_t k = rev; k < 512; k += 1u << len) h.fast[k] = (uint16_t)((len << 9) | i);
    }
    return 0;
}

struct Inf {
    const uint8_t* in; size_t n; size_t pos = 0; uint64_t bb = 0; int bc = 0;
    uint8_t* out; size_t cap; size_t op = 0;

    void Refill() { while (bc <= 56 && pos < n) { bb |= (uint64_t)in[pos++] << bc; bc += 8; } }
    bool Bits(int k, unsigned& v) {
        if (bc < k) { Refill(); if (bc < k) return false; }
        v = (unsigned)(bb & ((1ull << k) - 1)); bb >>= k; bc -= k; return true;
    }
    int Decode(const Huff& h) {
        if (bc < 15) Refill();
        uint16_t e = h.fast[bb & 511];
        if (e) { int len = e >> 9; if (len > bc) return -1; bb >>= len; bc -= len; return e & 511; }
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            if (len > bc) return -1;
            code |= (int)((bb >> (len - 1)) & 1);
            int count = h.count[len];
            if (code - count < first) { bb >>= len; bc -= len; return h.symbol[index + (code - first)]; }
            index += count; first += count; first <<= 1; code <<= 1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const uint8_t kLenExtra[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const uint16_t kDistBase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
const uint8_t kDistExtra[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

bool Codes(Inf& s, const Huff& lit, const Huff& dist) {
    for (;;) {
        int sym = s.Decode(lit);
        if (sym < 0) return false;
        if (sym < 256) { if (s.op >= s.cap) return false; s.out[s.op++] = (uint8_t)sym; continue; }
        if (sym == 256) return true;
        sym -= 257; if (sym >= 29) return false;
        unsigned ex = 0; if (kLenExtra[sym] && !s.Bits(kLenExtra[sym], ex)) return false;
        size_t len = kLenBase[sym] + ex;
        int ds = s.Decode(dist); if (ds < 0 || ds >= 30) return false;
        unsigned dx = 0; if (kDistExtra[ds] && !s.Bits(kDistExtra[ds], dx)) return false;
        size_t d = kDistBase[ds] + dx;
        if (d > s.op || len > s.cap - s.op) return false;
        uint8_t* o = s.out + s.op; const uint8_t* src = o - d;
        for (size_t i = 0; i < len; i++) o[i] = src[i];
        s.op += len;
    }
}

bool Inflate(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
    Inf s; s.in = in; s.n = n; s.out = out; s.cap = cap;
    static Huff fixedLit, fixedDist; static bool fixedInit = false;
    if (!fixedInit) {
        uint8_t l[288]; int i = 0;
        for (; i < 144; i++) l[i] = 8; for (; i < 256; i++) l[i] = 9; for (; i < 280; i++) l[i] = 7; for (; i < 288; i++) l[i] = 8;
        Construct(fixedLit, l, 288);
        uint8_t d[30]; for (i = 0; i < 30; i++) d[i] = 5;
        Construct(fixedDist, d, 30);
        fixedInit = true;
    }
    unsigned last = 0;
    do {
        unsigned type = 0;
        if (!s.Bits(1, last) || !s.Bits(2, type)) return false;
        if (type == 0) {
            int drop = s.bc & 7; s.bb >>= drop; s.bc -= drop;
            unsigned len = 0, nlen = 0;
            if (!s.Bits(16, len) || !s.Bits(16, nlen) || (len ^ 0xFFFF) != nlen) return false;
            if (len > s.cap - s.op) return false;
            while (len && s.bc >= 8) { s.out[s.op++] = (uint8_t)(s.bb & 255); s.bb >>= 8; s.bc -= 8; len--; }
            if (len) { if (len > s.n - s.pos) return false; memcpy(s.out + s.op, s.in + s.pos, len); s.pos += len; s.op += len; }
        } else if (type == 1) {
            if (!Codes(s, fixedLit, fixedDist)) return false;
        } else if (type == 2) {
            unsigned hlit, hdist, hclen;
            if (!s.Bits(5, hlit) || !s.Bits(5, hdist) || !s.Bits(4, hclen)) return false;
            hlit += 257; hdist += 1; hclen += 4;
            if (hlit > 286 || hdist > 30) return false;
            static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
            uint8_t lens[320]; memset(lens, 0, sizeof lens);
            for (unsigned i = 0; i < hclen; i++) { unsigned v; if (!s.Bits(3, v)) return false; lens[order[i]] = (uint8_t)v; }
            Huff hc; if (Construct(hc, lens, 19) < 0) return false;
            uint8_t ll[320]; memset(ll, 0, sizeof ll);
            unsigned idx = 0;
            while (idx < hlit + hdist) {
                int sym = s.Decode(hc); if (sym < 0) return false;
                if (sym < 16) { ll[idx++] = (uint8_t)sym; continue; }
                unsigned rep = 0, v = 0, ex = 0; uint8_t val = 0;
                if (sym == 16) { if (idx == 0) return false; val = ll[idx - 1]; if (!s.Bits(2, ex)) return false; rep = 3 + ex; }
                else if (sym == 17) { if (!s.Bits(3, ex)) return false; rep = 3 + ex; }
                else { if (!s.Bits(7, ex)) return false; rep = 11 + ex; }
                (void)v;
                if (idx + rep > hlit + hdist) return false;
                while (rep--) ll[idx++] = val;
            }
            if (ll[256] == 0) return false;
            Huff hl, hd;
            if (Construct(hl, ll, (int)hlit) < 0 || Construct(hd, ll + hlit, (int)hdist) < 0) return false;
            if (!Codes(s, hl, hd)) return false;
        } else return false;
    } while (!last);
    return s.op == cap;
}

}  // namespace

// ---------------------------------------------------------------- 文字・小道具
static std::wstring Lower(const std::wstring& s) { std::wstring r = s; if (!r.empty()) CharLowerBuffW(&r[0], (DWORD)r.size()); return r; }
static uint16_t Le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t Le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t Le64(const uint8_t* p) { return (uint64_t)Le32(p) | (uint64_t)Le32(p + 4) << 32; }

static bool ReadAt(HANDLE h, uint64_t off, void* buf, size_t len) {
    LARGE_INTEGER p; p.QuadPart = (LONGLONG)off;
    if (!SetFilePointerEx(h, p, nullptr, FILE_BEGIN)) return false;
    uint8_t* b = (uint8_t*)buf; size_t done = 0;
    while (done < len) {
        DWORD n = (DWORD)std::min<size_t>(len - done, 1u << 24), got = 0;
        if (!ReadFile(h, b + done, n, &got, nullptr) || !got) return false;
        done += got;
    }
    return true;
}
static HANDLE OpenRead(const std::wstring& path) {
    return CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
static uint64_t DosToFileTime(uint16_t date, uint16_t time) {
    FILETIME l, u;
    if (!DosDateTimeToFileTime(date, time, &l) || !LocalFileTimeToFileTime(&l, &u)) return 0;
    return ((uint64_t)u.dwHighDateTime << 32) | u.dwLowDateTime;
}

// ---------------------------------------------------------------- Archive
struct Archive::Idx {
    std::map<std::wstring, int> files;               // 小文字のパス → ents の番号
    std::map<std::wstring, std::wstring> dirs;       // 小文字のフォルダ → 元の綴り
};

static void BuildIdx(Archive& a) {
    auto ix = std::make_shared<Archive::Idx>();
    for (size_t i = 0; i < a.ents.size(); i++) {
        const auto& e = a.ents[i];
        if (!e.dir) ix->files.emplace(Lower(e.name), (int)i);
        // 親のフォルダ（ZIP にフォルダの項目が無くても、パスから補う）
        size_t from = 0;
        for (;;) {
            size_t s = e.name.find(L'\\', from);
            if (s == std::wstring::npos) break;
            std::wstring d = e.name.substr(0, s);
            ix->dirs.emplace(Lower(d), d);
            from = s + 1;
        }
        if (e.dir) ix->dirs.emplace(Lower(e.name), e.name);
    }
    a.idx = ix;
}

int Archive::Kind(const std::wstring& inner) const {
    if (inner.empty()) return 2;
    std::wstring k = Lower(inner);
    if (idx->files.count(k)) return 1;
    if (idx->dirs.count(k)) return 2;
    return 0;
}
const Archive::Entry* Archive::Find(const std::wstring& inner) const {
    auto it = idx->files.find(Lower(inner));
    return it == idx->files.end() ? nullptr : &ents[it->second];
}

void Archive::List(const std::wstring& innerDir, std::vector<DirItem>& out) const {
    std::wstring prefix = innerDir.empty() ? std::wstring() : Lower(innerDir) + L"\\";
    std::set<std::wstring> seen;
    for (const auto& e : ents) {
        if (e.name.size() <= prefix.size()) continue;
        if (!prefix.empty() && Lower(e.name.substr(0, prefix.size())) != prefix) continue;
        std::wstring rel = e.name.substr(prefix.size());
        size_t s = rel.find(L'\\');
        DirItem it;
        if (s == std::wstring::npos && !e.dir) { it.name = rel; it.folder = false; it.size = e.usize; it.mod = e.mod; out.push_back(it); continue; }
        it.name = s == std::wstring::npos ? rel : rel.substr(0, s); it.folder = true; it.mod = e.mod;
        if (seen.insert(Lower(it.name)).second) out.push_back(it);
    }
}

bool Archive::ShallowestAudioDir(std::wstring& dir) const {
    bool found = false; int bestDepth = 0; std::wstring best;
    for (const auto& e : ents) {
        if (e.dir || !IsSupportedFile(NameOf(e.name).c_str())) continue;
        size_t s = e.name.find_last_of(L'\\');
        std::wstring d = s == std::wstring::npos ? std::wstring() : e.name.substr(0, s);
        int depth = 0; for (wchar_t c : d) if (c == L'\\') depth++;
        if (!d.empty()) depth++;
        if (!found || depth < bestDepth || (depth == bestDepth && StrCmpLogicalW(d.c_str(), best.c_str()) < 0)) { found = true; bestDepth = depth; best = d; }
    }
    if (found) dir = best;
    return found;
}

// ---------------------------------------------------------------- 目次の読み取り
static std::wstring DecodeName(const uint8_t* p, size_t n, bool utf8) {
    if (!n) return L"";
    UINT cp = utf8 ? CP_UTF8 : 932;
    int w = MultiByteToWideChar(cp, 0, (const char*)p, (int)n, nullptr, 0);
    if (w <= 0 && utf8) { cp = 932; w = MultiByteToWideChar(cp, 0, (const char*)p, (int)n, nullptr, 0); }
    if (w <= 0) return L"";
    std::wstring s(w, 0); MultiByteToWideChar(cp, 0, (const char*)p, (int)n, &s[0], w);
    return s;
}
// 区切りを `\` にそろえ、先頭の `./` や `/`、`..` を含む名前は捨てる（false）
static bool NormalizeName(std::wstring& s, bool& dir) {
    for (auto& c : s) if (c == L'/') c = L'\\';
    dir = !s.empty() && s.back() == L'\\';
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    while (!s.empty() && s[0] == L'\\') s.erase(0, 1);
    while (s.size() >= 2 && s[0] == L'.' && s[1] == L'\\') s.erase(0, 2);
    if (s.empty()) return false;
    for (wchar_t c : s) if (c == L':' || c == 0) return false;
    size_t from = 0;
    for (;;) {
        size_t e = s.find(L'\\', from);
        std::wstring part = s.substr(from, e == std::wstring::npos ? std::wstring::npos : e - from);
        if (part == L".." || part.empty()) return false;
        if (e == std::wstring::npos) break;
        from = e + 1;
    }
    return true;
}

static bool ParseArchive(const std::wstring& path, Archive& a) {
    HANDLE h = OpenRead(path);
    if (h == INVALID_HANDLE_VALUE) return false;
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{ h };
    LARGE_INTEGER sz; if (!GetFileSizeEx(h, &sz)) return false;
    uint64_t fsz = (uint64_t)sz.QuadPart;
    if (fsz < 22) return false;
    // 末尾から EOCD（終端レコード）を探す
    size_t tailLen = (size_t)std::min<uint64_t>(fsz, 22 + 65535 + 20 + 64);
    Bytes tail(tailLen);
    if (!ReadAt(h, fsz - tailLen, tail.data(), tailLen)) return false;
    int64_t eo = -1;
    for (int64_t i = (int64_t)tailLen - 22; i >= 0; i--) {
        if (Le32(&tail[i]) == 0x06054b50) { size_t cl = Le16(&tail[i + 20]); if ((size_t)i + 22 + cl <= tailLen) { eo = i; break; } }
    }
    if (eo < 0) return false;
    uint64_t eocdPos = fsz - tailLen + eo;
    const uint8_t* e = &tail[eo];
    uint64_t cdSize = Le32(e + 12), cdOff = Le32(e + 16), total = Le16(e + 10);
    uint64_t anchor = eocdPos;   // 目次の終わり（ずれの補正用）
    if (cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu || total == 0xFFFF) {
        // ZIP64：EOCD の直前に locator がある
        if (eocdPos >= 20) {
            uint8_t loc[20];
            if (ReadAt(h, eocdPos - 20, loc, 20) && Le32(loc) == 0x07064b50) {
                uint64_t z64 = Le64(loc + 8); uint8_t r[56];
                if (z64 + 56 <= fsz && ReadAt(h, z64, r, 56) && Le32(r) == 0x06064b50) {
                    total = Le64(r + 32); cdSize = Le64(r + 40); cdOff = Le64(r + 48); anchor = z64;
                } else return false;
            } else if (cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) return false;
        } else return false;
    }
    if (cdSize > (256u << 20) || cdSize > fsz) return false;
    // 先頭に余計なデータ（自己展開など）がある ZIP の補正
    uint64_t shift = 0;
    if (anchor >= cdSize + cdOff) shift = anchor - cdSize - cdOff;
    if (cdOff + shift + cdSize > fsz) return false;
    Bytes cd((size_t)cdSize);
    if (cdSize && !ReadAt(h, cdOff + shift, cd.data(), (size_t)cdSize)) return false;
    size_t pos = 0;
    while (pos + 46 <= cd.size()) {
        const uint8_t* p = &cd[pos];
        if (Le32(p) != 0x02014b50) return false;
        Archive::Entry en;
        en.flags = Le16(p + 8); en.method = Le16(p + 10);
        en.mod = DosToFileTime(Le16(p + 14), Le16(p + 12));
        en.crc = Le32(p + 16); en.csize = Le32(p + 20); en.usize = Le32(p + 24);
        size_t nl = Le16(p + 28), xl = Le16(p + 30), cl = Le16(p + 32);
        en.off = Le32(p + 42);
        if (pos + 46 + nl + xl + cl > cd.size()) return false;
        const uint8_t* nm = p + 46; const uint8_t* xt = nm + nl;
        // ZIP64 の拡張欄（0xFFFFFFFF の欄だけ、usize・csize・off の順に入っている）
        for (size_t xp = 0; xp + 4 <= xl;) {
            uint16_t id = Le16(xt + xp), sz2 = Le16(xt + xp + 2);
            if (xp + 4 + sz2 > xl) break;
            if (id == 0x0001) {
                const uint8_t* q = xt + xp + 4; size_t left = sz2;
                if (en.usize == 0xFFFFFFFFu && left >= 8) { en.usize = Le64(q); q += 8; left -= 8; }
                if (en.csize == 0xFFFFFFFFu && left >= 8) { en.csize = Le64(q); q += 8; left -= 8; }
                if (en.off == 0xFFFFFFFFu && left >= 8) { en.off = Le64(q); q += 8; left -= 8; }
            }
            xp += 4 + sz2;
        }
        en.off += shift;
        std::wstring name = DecodeName(nm, nl, (en.flags & 0x800) != 0);
        bool dir = false;
        if (NormalizeName(name, dir)) {
            en.name = name; en.dir = dir;
            a.ents.push_back(std::move(en));
        }
        pos += 46 + nl + xl + cl;
    }
    if (pos != cd.size() && !a.ents.size() && cd.size()) return false;
    a.path = path; a.fileSize = fsz;
    BuildIdx(a);
    return true;
}

// ---------------------------------------------------------------- 目次の覚え
static std::mutex g_m;
static std::map<std::wstring, std::shared_ptr<Archive>> g_arch;

static bool Stat(const std::wstring& path, uint64_t& size, uint64_t& mod) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    mod = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
    return true;
}

std::shared_ptr<const Archive> Open(const std::wstring& zipPath) {
    uint64_t sz = 0, md = 0;
    if (!Stat(zipPath, sz, md)) return nullptr;
    std::wstring key = Lower(zipPath);
    {
        std::lock_guard<std::mutex> lk(g_m);
        auto it = g_arch.find(key);
        if (it != g_arch.end() && it->second->fileSize == sz && it->second->fileMod == md) return it->second;
    }
    auto a = std::make_shared<Archive>();
    if (!ParseArchive(zipPath, *a)) return nullptr;
    a->fileSize = sz; a->fileMod = md;
    std::lock_guard<std::mutex> lk(g_m);
    if (g_arch.size() > 8) g_arch.clear();
    g_arch[key] = a;
    return a;
}

// ---------------------------------------------------------------- パス
static size_t FindZipExt(const std::wstring& p, size_t from) {
    for (size_t i = from; i + 4 <= p.size(); i++)
        if (p[i] == L'.' && (p[i + 1] | 0x20) == L'z' && (p[i + 2] | 0x20) == L'i' && (p[i + 3] | 0x20) == L'p') return i;
    return std::wstring::npos;
}

bool SplitPath(const std::wstring& p, std::wstring& zip, std::wstring& inner) {
    size_t pos = 0;
    while ((pos = FindZipExt(p, pos)) != std::wstring::npos) {
        size_t e = pos + 4;
        if (pos > 0 && (e == p.size() || p[e] == L'\\' || p[e] == L'/')) {
            std::wstring pre = p.substr(0, e);
            DWORD a = GetFileAttributesW(pre.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                zip = pre;
                inner = e < p.size() ? p.substr(e + 1) : std::wstring();
                for (auto& c : inner) if (c == L'/') c = L'\\';
                while (!inner.empty() && inner.back() == L'\\') inner.pop_back();
                return true;
            }
        }
        pos = e;
    }
    return false;
}

// ---------------------------------------------------------------- 1曲を展開
static const uint64_t kMaxEntry = 1ull << 30;   // 展開して 1GB を超える曲は扱わない

static std::shared_ptr<Bytes> Extract(const Archive& a, const Archive::Entry& e) {
    if (e.dir || (e.flags & 0x41)) return nullptr;            // パスワード付き（強い暗号を含む）
    if (e.method != 0 && e.method != 8) return nullptr;        // Deflate64・LZMA などは扱わない
    if (e.usize > kMaxEntry || e.csize > kMaxEntry) return nullptr;
    HANDLE h = OpenRead(a.path);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{ h };
    try {
        uint8_t lh[30];
        if (!ReadAt(h, e.off, lh, 30) || Le32(lh) != 0x04034b50) return nullptr;
        uint64_t data = e.off + 30 + Le16(lh + 26) + Le16(lh + 28);
        if (data + e.csize > a.fileSize) return nullptr;
        auto out = std::make_shared<Bytes>((size_t)e.usize);
        if (e.method == 0) {
            if (e.csize != e.usize) return nullptr;
            if (e.usize && !ReadAt(h, data, out->data(), (size_t)e.usize)) return nullptr;
        } else {
            Bytes in((size_t)e.csize);
            if (e.csize && !ReadAt(h, data, in.data(), in.size())) return nullptr;
            if (!Inflate(in.data(), in.size(), out->data(), out->size())) return nullptr;
        }
        if (Crc32(out->data(), out->size()) != e.crc) return nullptr;
        return out;
    } catch (const std::bad_alloc&) { return nullptr; }
}

// 直近の数曲だけ覚えておく（タグ・長さ・ジャケット・再生で同じ曲を続けて開くため）
struct CacheItem { std::wstring key; std::shared_ptr<const Bytes> data; };
static std::vector<CacheItem> g_cache;   // 新しいものが後ろ

std::shared_ptr<const Bytes> Acquire(const std::wstring& path) {
    std::wstring zip, inner;
    if (!SplitPath(path, zip, inner) || inner.empty()) return nullptr;
    auto ar = Open(zip);
    if (!ar) return nullptr;
    const Archive::Entry* en = ar->Find(inner);
    if (!en) return nullptr;
    std::wstring key = Lower(zip) + L"|" + std::to_wstring(ar->fileMod) + L"|" + Lower(inner);
    {
        std::lock_guard<std::mutex> lk(g_m);
        for (size_t i = 0; i < g_cache.size(); i++) if (g_cache[i].key == key) {
            CacheItem it = g_cache[i]; g_cache.erase(g_cache.begin() + i); g_cache.push_back(it); return it.data;
        }
    }
    std::shared_ptr<const Bytes> d = Extract(*ar, *en);
    if (!d) { Trace(L"zip: cannot extract %s", path.c_str()); return nullptr; }
    std::lock_guard<std::mutex> lk(g_m);
    g_cache.push_back({ key, d });
    size_t total = 0; for (auto& c : g_cache) total += c.data->size();
    while (g_cache.size() > 1 && (g_cache.size() > 4 || total > (192u << 20))) { total -= g_cache.front().data->size(); g_cache.erase(g_cache.begin()); }
    return d;
}

bool FileStat(const std::wstring& path, uint64_t& size, uint64_t& mod) {
    std::wstring zip, inner;
    if (SplitPath(path, zip, inner)) {
        if (inner.empty()) return Stat(zip, size, mod);
        auto ar = Open(zip); if (!ar) return false;
        const Archive::Entry* en = ar->Find(inner); if (!en) return false;
        size = en->usize; mod = en->mod; return true;
    }
    return Stat(path, size, mod);
}
bool FileExists(const std::wstring& path) { uint64_t s, m; return FileStat(path, s, m); }

}  // namespace zipfs
