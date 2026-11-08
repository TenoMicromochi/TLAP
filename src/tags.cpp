#include "tags.h"
#include "util.h"
#include "decode.h"
#include "zipfs.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <algorithm>
#include <cstring>

typedef std::vector<uint8_t> Bytes;

// ---------------------------------------------------------------- ファイル
static bool ReadRange(const std::wstring& path, uint64_t off, size_t len, Bytes& out) {
    if (zipfs::IsInside(path)) {   // ZIP の中の曲：展開した1曲（直近のものは覚えてある）から切り出す
        auto d = zipfs::Acquire(path);
        if (!d || off >= d->size()) { out.clear(); return false; }
        len = (size_t)std::min<uint64_t>(len, d->size() - off);
        out.assign(d->begin() + off, d->begin() + off + len);
        return len > 0;
    }
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if ((uint64_t)off >= (uint64_t)sz.QuadPart) { CloseHandle(h); out.clear(); return false; }
    len = (size_t)std::min<uint64_t>(len, (uint64_t)sz.QuadPart - off);
    LARGE_INTEGER p; p.QuadPart = (LONGLONG)off; SetFilePointerEx(h, p, nullptr, FILE_BEGIN);
    out.resize(len); DWORD got = 0; size_t done = 0;
    while (done < len) { DWORD n = (DWORD)std::min<size_t>(len - done, 1 << 24); if (!ReadFile(h, out.data() + done, n, &got, nullptr) || !got) break; done += got; }
    out.resize(done); CloseHandle(h);
    return done > 0;
}
static uint64_t FileSize(const std::wstring& path) {
    if (zipfs::IsInside(path)) { uint64_t s = 0, m = 0; return zipfs::FileStat(path, s, m) ? s : 0; }
    WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

// ---------------------------------------------------------------- 文字
static std::wstring FromUtf8(const char* p, size_t n) {
    if (!n) return L"";
    int w = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, nullptr, 0);
    std::wstring s(w, 0); MultiByteToWideChar(CP_UTF8, 0, p, (int)n, &s[0], w); return s;
}
static bool ValidUtf8(const char* p, size_t n) { return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p, (int)n, nullptr, 0) > 0; }
// Latin-1 と書かれていても、Shift_JIS として読めるものは Shift_JIS として読む（古い日本の MP3 対策）
static std::wstring FromLatin1OrSjis(const uint8_t* p, size_t n) {
    bool high = false; for (size_t i = 0; i < n; i++) if (p[i] >= 0x80) high = true;
    if (high) {
        int w = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, (const char*)p, (int)n, nullptr, 0);
        if (w > 0) { std::wstring s(w, 0); MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, (const char*)p, (int)n, &s[0], w); return s; }
    }
    std::wstring s; for (size_t i = 0; i < n; i++) s += (wchar_t)p[i]; return s;
}
static std::wstring CutNul(std::wstring s) { size_t z = s.find(L'\0'); if (z != std::wstring::npos) s.resize(z); return s; }
// ID3 のテキスト（enc: 0 Latin-1, 1 UTF-16(BOM), 2 UTF-16BE, 3 UTF-8）。最初の値だけ
static std::wstring DecodeId3Text(const uint8_t* p, size_t n, int enc) {
    if (enc == 1 || enc == 2) {
        bool be = enc == 2;
        if (n >= 2 && enc == 1) { if (p[0] == 0xFF && p[1] == 0xFE) { p += 2; n -= 2; } else if (p[0] == 0xFE && p[1] == 0xFF) { be = true; p += 2; n -= 2; } }
        std::wstring s;
        for (size_t i = 0; i + 1 < n; i += 2) { wchar_t c = be ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]); if (!c) break; s += c; }
        return s;
    }
    size_t z = 0; while (z < n && p[z]) z++;
    if (enc == 3) return FromUtf8((const char*)p, z);
    return FromLatin1OrSjis(p, z);
}
static int ParseIntPrefix(const std::wstring& s) { return _wtoi(s.c_str()); }

// ID3v1 のジャンル名（0〜147）。ID3v2 の TCON の (17) のような数字・MP4 の gnre もこれに直す
static const wchar_t* const kId3v1Genres[148] = {
    L"Blues", L"Classic Rock", L"Country", L"Dance", L"Disco", L"Funk",
    L"Grunge", L"Hip-Hop", L"Jazz", L"Metal", L"New Age", L"Oldies",
    L"Other", L"Pop", L"R&B", L"Rap", L"Reggae", L"Rock",
    L"Techno", L"Industrial", L"Alternative", L"Ska", L"Death Metal", L"Pranks",
    L"Soundtrack", L"Euro-Techno", L"Ambient", L"Trip-Hop", L"Vocal", L"Jazz+Funk",
    L"Fusion", L"Trance", L"Classical", L"Instrumental", L"Acid", L"House",
    L"Game", L"Sound Clip", L"Gospel", L"Noise", L"Alternative Rock", L"Bass",
    L"Soul", L"Punk", L"Space", L"Meditative", L"Instrumental Pop", L"Instrumental Rock",
    L"Ethnic", L"Gothic", L"Darkwave", L"Techno-Industrial", L"Electronic", L"Pop-Folk",
    L"Eurodance", L"Dream", L"Southern Rock", L"Comedy", L"Cult", L"Gangsta",
    L"Top 40", L"Christian Rap", L"Pop/Funk", L"Jungle", L"Native American", L"Cabaret",
    L"New Wave", L"Psychedelic", L"Rave", L"Showtunes", L"Trailer", L"Lo-Fi",
    L"Tribal", L"Acid Punk", L"Acid Jazz", L"Polka", L"Retro", L"Musical",
    L"Rock & Roll", L"Hard Rock", L"Folk", L"Folk-Rock", L"National Folk", L"Swing",
    L"Fast Fusion", L"Bebop", L"Latin", L"Revival", L"Celtic", L"Bluegrass",
    L"Avantgarde", L"Gothic Rock", L"Progressive Rock", L"Psychedelic Rock", L"Symphonic Rock", L"Slow Rock",
    L"Big Band", L"Chorus", L"Easy Listening", L"Acoustic", L"Humour", L"Speech",
    L"Chanson", L"Opera", L"Chamber Music", L"Sonata", L"Symphony", L"Booty Bass",
    L"Primus", L"Porn Groove", L"Satire", L"Slow Jam", L"Club", L"Tango",
    L"Samba", L"Folklore", L"Ballad", L"Power Ballad", L"Rhythmic Soul", L"Freestyle",
    L"Duet", L"Punk Rock", L"Drum Solo", L"A Cappella", L"Euro-House", L"Dance Hall",
    L"Goa", L"Drum & Bass", L"Club-House", L"Hardcore", L"Terror", L"Indie",
    L"BritPop", L"Negerpunk", L"Polsk Punk", L"Beat", L"Christian Gangsta Rap", L"Heavy Metal",
    L"Black Metal", L"Crossover", L"Contemporary Christian", L"Christian Rock", L"Merengue", L"Salsa",
    L"Thrash Metal", L"Anime", L"JPop", L"Synthpop" };

// TCON：「(17)」「(17)Rock」「17」「Rock」のどれでも読む。数字は ID3v1 のジャンル名に直す
static std::wstring Id3Genre(const std::wstring& s0) {
    std::wstring s = s0;
    while (!s.empty() && s[0] == L'(') {
        size_t e = s.find(L')'); if (e == std::wstring::npos) break;
        std::wstring in = s.substr(1, e - 1), rest = s.substr(e + 1);
        bool num = !in.empty(); for (wchar_t c : in) if (c < L'0' || c > L'9') num = false;
        if (!rest.empty() && rest[0] != L'(') return rest;           // 「(17)Rock」→ Rock
        if (num) { int g = _wtoi(in.c_str()); if (g >= 0 && g < 148) return kId3v1Genres[g]; return L""; }
        if (rest.empty()) return L"";                                // (RX) (CR) など
        s = rest;
    }
    bool num = !s.empty(); for (wchar_t c : s) if (c < L'0' || c > L'9') num = false;
    if (num) { int g = _wtoi(s.c_str()); return (g >= 0 && g < 148) ? std::wstring(kId3v1Genres[g]) : std::wstring(); }
    return s;
}

// ---------------------------------------------------------------- ID3v2
static uint32_t SyncSafe(const uint8_t* p) { return (p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F); }
static uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint32_t Le32(const uint8_t* p) { return (uint32_t)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

static size_t Id3Size(const Bytes& h) { return (h.size() >= 10 && !memcmp(h.data(), "ID3", 3)) ? 10 + SyncSafe(&h[6]) : 0; }

static void Unsync(Bytes& b) { Bytes o; o.reserve(b.size()); for (size_t i = 0; i < b.size(); i++) { o.push_back(b[i]); if (b[i] == 0xFF && i + 1 < b.size() && b[i + 1] == 0) i++; } b.swap(o); }

static void ParseId3(const std::wstring& path, Tags& t, bool wantPic) {
    Bytes h; if (!ReadRange(path, 0, 10, h) || h.size() < 10 || memcmp(h.data(), "ID3", 3)) return;
    int ver = h[3], flags = h[5]; uint32_t sz = SyncSafe(&h[6]);
    if (ver < 2 || ver > 4) return;
    Bytes d; if (!ReadRange(path, 10, std::min<uint32_t>(sz, 64u << 20), d)) return;
    if ((flags & 0x80) && ver != 4) Unsync(d);
    size_t pos = 0;
    if (flags & 0x40) { if (d.size() < 4) return; pos = ver == 4 ? SyncSafe(&d[0]) : 4 + Be32(&d[0]); }
    if (ver == 2) return;   // 2.2 は読まない（SPEC は 2.3 / 2.4）
    while (pos + 10 <= d.size()) {
        const uint8_t* f = &d[pos];
        if (!f[0]) break;
        char id[5] = { (char)f[0], (char)f[1], (char)f[2], (char)f[3], 0 };
        uint32_t fs = ver == 4 ? SyncSafe(f + 4) : Be32(f + 4);
        int ff = f[9];
        pos += 10;
        if (fs > d.size() - pos) break;
        const uint8_t* p = &d[pos]; size_t n = fs;
        Bytes tmp;
        if (ver == 4) {
            if (ff & 0x01) { if (n >= 4) { p += 4; n -= 4; } }
            if (ff & 0x02) { tmp.assign(p, p + n); Unsync(tmp); p = tmp.data(); n = tmp.size(); }
        }
        if (id[0] == 'T' && strcmp(id, "TXXX") && n >= 1) {
            std::wstring s = DecodeId3Text(p + 1, n - 1, p[0]);
            if (!strcmp(id, "TIT2")) t.title = s;
            else if (!strcmp(id, "TPE1")) t.artist = s;
            else if (!strcmp(id, "TALB")) t.album = s;
            else if (!strcmp(id, "TPE2")) t.albumArtist = s;
            else if (!strcmp(id, "TCON")) t.genre = Id3Genre(s);
            else if (!strcmp(id, "TRCK")) t.track = ParseIntPrefix(s);
            else if (!strcmp(id, "TPOS")) t.disc = ParseIntPrefix(s);
            else if (!strcmp(id, "TYER") || (!strcmp(id, "TDRC") && t.year.empty())) t.year = s.substr(0, 4);
        } else if (!strcmp(id, "APIC") && wantPic && n > 4) {
            int enc = p[0]; size_t i = 1;
            while (i < n && p[i]) i++;       // mime
            i++;
            if (i < n) {
                int type = p[i]; i++;
                if (enc == 1 || enc == 2) { while (i + 1 < n && (p[i] || p[i + 1])) i += 2; i += 2; }
                else { while (i < n && p[i]) i++; i++; }
                if (i < n && (t.picture.empty() || type == 3)) t.picture.assign(p + i, p + n);
            }
        }
        pos += fs;
    }
}

// ---------------------------------------------------------------- Vorbis comment（FLAC / Ogg）
static Bytes Base64Decode(const std::string& s) {
    Bytes o; int val = 0, bits = -8;
    for (unsigned char c : s) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A'; else if (c >= 'a' && c <= 'z') d = c - 'a' + 26; else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '+') d = 62; else if (c == '/') d = 63; else continue;
        val = (val << 6) | d; bits += 6;
        if (bits >= 0) { o.push_back((uint8_t)((val >> bits) & 0xFF)); bits -= 8; }
    }
    return o;
}
static void PictureFromFlacBlock(const uint8_t* p, size_t n, Tags& t) {   // FLAC の PICTURE ブロック（Vorbis の METADATA_BLOCK_PICTURE も同じ形）
    if (n < 32) return;
    uint32_t type = Be32(p); size_t i = 4;
    uint32_t ml = Be32(p + i); i += 4 + ml; if (i + 4 > n) return;
    uint32_t dl = Be32(p + i); i += 4 + dl; if (i + 20 > n) return;
    i += 16; uint32_t len = Be32(p + i); i += 4;
    if (i + len > n) return;
    if (t.picture.empty() || type == 3) t.picture.assign(p + i, p + i + len);
}
static void ParseVorbisComment(const uint8_t* p, size_t n, Tags& t, bool wantPic) {
    if (n < 8) return;
    size_t i = 0; uint32_t vl = Le32(p); i = 4 + vl; if (i + 4 > n) return;
    uint32_t cnt = Le32(p + i); i += 4;
    for (uint32_t k = 0; k < cnt && i + 4 <= n; k++) {
        uint32_t l = Le32(p + i); i += 4;
        if (l > n - i) break;
        std::string kv((const char*)p + i, l); i += l;
        size_t eq = kv.find('='); if (eq == std::string::npos) continue;
        std::string key = kv.substr(0, eq); for (auto& c : key) c = (char)toupper((unsigned char)c);
        const char* v = kv.c_str() + eq + 1; size_t vn = kv.size() - eq - 1;
        if (key == "METADATA_BLOCK_PICTURE") { if (wantPic) { Bytes b = Base64Decode(std::string(v, vn)); PictureFromFlacBlock(b.data(), b.size(), t); } continue; }
        std::wstring w = ValidUtf8(v, vn) ? FromUtf8(v, vn) : FromLatin1OrSjis((const uint8_t*)v, vn);
        if (key == "TITLE") t.title = w; else if (key == "ARTIST") t.artist = w; else if (key == "ALBUM") t.album = w;
        else if (key == "ALBUMARTIST") t.albumArtist = w;
        else if (key == "GENRE") { if (t.genre.empty()) t.genre = w; }
        else if (key == "DATE" || key == "YEAR") { if (t.year.empty()) t.year = w.substr(0, 4); }
        else if (key == "TRACKNUMBER") t.track = ParseIntPrefix(w); else if (key == "DISCNUMBER") t.disc = ParseIntPrefix(w);
    }
}

static void ParseFlac(const std::wstring& path, Tags& t, bool wantPic) {
    Bytes h; if (!ReadRange(path, 0, 10, h)) return;
    uint64_t off = Id3Size(h);
    Bytes m; if (!ReadRange(path, off, 4, m) || memcmp(m.data(), "fLaC", 4)) return;
    off += 4;
    for (int guard = 0; guard < 64; guard++) {
        Bytes bh; if (!ReadRange(path, off, 4, bh) || bh.size() < 4) return;
        bool last = bh[0] & 0x80; int type = bh[0] & 0x7F; uint32_t len = bh[1] << 16 | bh[2] << 8 | bh[3];
        if (type == 4 || (type == 6 && wantPic)) {
            Bytes b; if (ReadRange(path, off + 4, len, b)) { if (type == 4) ParseVorbisComment(b.data(), b.size(), t, false); else PictureFromFlacBlock(b.data(), b.size(), t); }
        }
        off += 4 + len;
        if (last) break;
    }
}

// ---------------------------------------------------------------- Ogg（Vorbis / Opus）
static void ParseOgg(const std::wstring& path, Tags& t, bool wantPic) {
    Bytes d; if (!ReadRange(path, 0, 16u << 20, d) || d.size() < 27) return;
    size_t pos = 0; int pktIndex = 0; Bytes cur; bool vorbis = false, opus = false;
    while (pos + 27 <= d.size() && !memcmp(&d[pos], "OggS", 4)) {
        int nseg = d[pos + 26]; if (pos + 27 + nseg > d.size()) return;
        size_t dp = pos + 27 + nseg;
        for (int i = 0; i < nseg; i++) {
            int l = d[pos + 27 + i];
            if (dp + l > d.size()) return;
            cur.insert(cur.end(), d.begin() + dp, d.begin() + dp + l); dp += l;
            if (l < 255) {   // パケットの終わり
                if (pktIndex == 0) { vorbis = cur.size() >= 7 && !memcmp(cur.data(), "\x01vorbis", 7); opus = cur.size() >= 8 && !memcmp(cur.data(), "OpusHead", 8); if (!vorbis && !opus) return; }
                else if (pktIndex == 1) {
                    if (vorbis && cur.size() > 7 && !memcmp(cur.data(), "\x03vorbis", 7)) ParseVorbisComment(cur.data() + 7, cur.size() - 7, t, wantPic);
                    else if (opus && cur.size() > 8 && !memcmp(cur.data(), "OpusTags", 8)) ParseVorbisComment(cur.data() + 8, cur.size() - 8, t, wantPic);
                    return;
                }
                pktIndex++; cur.clear();
            }
        }
        pos = dp;
    }
}

// ---------------------------------------------------------------- MP4（ilst）
static bool FindBox(const Bytes& d, size_t from, size_t to, const char* type, size_t& bs, size_t& be) {   // 子の中身の範囲を返す
    size_t pos = from;
    while (pos + 8 <= to) {
        uint32_t sz = Be32(&d[pos]); size_t hdr = 8;
        if (sz == 1 && pos + 16 <= to) { sz = (uint32_t)(((uint64_t)Be32(&d[pos + 8]) << 32 | Be32(&d[pos + 12])) & 0xFFFFFFFF); hdr = 16; }
        if (sz < hdr || pos + sz > to) return false;
        if (!memcmp(&d[pos + 4], type, 4)) { bs = pos + hdr; be = pos + sz; return true; }
        pos += sz;
    }
    return false;
}
static void ParseMp4(const std::wstring& path, Tags& t, bool wantPic) {
    uint64_t off = 0, fsz = FileSize(path);
    Bytes moov;
    while (off + 8 <= fsz) {
        Bytes h; if (!ReadRange(path, off, 16, h) || h.size() < 8) return;
        uint64_t sz = Be32(h.data()); size_t hdr = 8;
        if (sz == 1 && h.size() >= 16) { sz = (uint64_t)Be32(&h[8]) << 32 | Be32(&h[12]); hdr = 16; }
        if (sz == 0) sz = fsz - off;
        if (sz < hdr) return;
        if (!memcmp(&h[4], "moov", 4)) { if (sz > (256u << 20) || !ReadRange(path, off + hdr, (size_t)(sz - hdr), moov)) return; break; }
        off += sz;
    }
    if (moov.empty()) return;
    size_t bs, be;
    size_t ls = 0, le = 0;
    if (FindBox(moov, 0, moov.size(), "udta", bs, be)) {
        size_t ms, me; if (FindBox(moov, bs, be, "meta", ms, me) && me >= ms + 4) { if (FindBox(moov, ms + 4, me, "ilst", ls, le)) {} }
    }
    if (!le && FindBox(moov, 0, moov.size(), "meta", bs, be) && be >= bs + 4) FindBox(moov, bs + 4, be, "ilst", ls, le);
    if (!le) return;
    size_t pos = ls;
    while (pos + 8 <= le) {
        uint32_t sz = Be32(&moov[pos]); if (sz < 8 || pos + sz > le) break;
        const uint8_t* nm = &moov[pos + 4];
        size_t ds, de;
        if (FindBox(moov, pos + 8, pos + sz, "data", ds, de) && de >= ds + 8) {
            const uint8_t* p = &moov[ds + 8]; size_t n = de - ds - 8; uint32_t dtype = Be32(&moov[ds]) & 0xFFFFFF;
            auto str = [&] { return FromUtf8((const char*)p, n); };
            if (!memcmp(nm, "\xA9" "nam", 4)) t.title = str();
            else if (!memcmp(nm, "\xA9" "ART", 4)) t.artist = str();
            else if (!memcmp(nm, "\xA9" "alb", 4)) t.album = str();
            else if (!memcmp(nm, "aART", 4)) t.albumArtist = str();
            else if (!memcmp(nm, "\xA9" "gen", 4)) t.genre = str();
            else if (!memcmp(nm, "gnre", 4) && n >= 2 && t.genre.empty()) { int g = p[0] << 8 | p[1]; if (g >= 1 && g <= 148) t.genre = kId3v1Genres[g - 1]; }
            else if (!memcmp(nm, "\xA9" "day", 4)) t.year = str().substr(0, 4);
            else if (!memcmp(nm, "trkn", 4) && n >= 4) t.track = p[2] << 8 | p[3];
            else if (!memcmp(nm, "disk", 4) && n >= 4) t.disc = p[2] << 8 | p[3];
            else if (!memcmp(nm, "covr", 4) && wantPic && dtype != 0 && t.picture.empty()) t.picture.assign(p, p + n);
        }
        pos += sz;
    }
}

// ---------------------------------------------------------------- WAV（LIST INFO）
static void ParseWav(const std::wstring& path, Tags& t) {
    Bytes h; if (!ReadRange(path, 0, 12, h) || h.size() < 12 || memcmp(h.data(), "RIFF", 4) || memcmp(&h[8], "WAVE", 4)) return;
    uint64_t off = 12, fsz = FileSize(path);
    while (off + 8 <= fsz) {
        Bytes ch; if (!ReadRange(path, off, 8, ch) || ch.size() < 8) return;
        uint32_t sz = Le32(&ch[4]);
        if (!memcmp(ch.data(), "LIST", 4) && sz >= 4 && sz < (16u << 20)) {
            Bytes b; if (ReadRange(path, off + 8, sz, b) && b.size() >= 4 && !memcmp(b.data(), "INFO", 4)) {
                size_t i = 4;
                while (i + 8 <= b.size()) {
                    uint32_t l = Le32(&b[i + 4]); if (i + 8 + l > b.size()) break;
                    const char* v = (const char*)&b[i + 8]; size_t vn = 0; while (vn < l && v[vn]) vn++;
                    std::wstring w = ValidUtf8(v, vn) ? FromUtf8(v, vn) : FromLatin1OrSjis((const uint8_t*)v, vn);
                    if (!memcmp(&b[i], "INAM", 4)) t.title = w; else if (!memcmp(&b[i], "IART", 4)) t.artist = w;
                    else if (!memcmp(&b[i], "IPRD", 4)) t.album = w; else if (!memcmp(&b[i], "ITRK", 4)) t.track = ParseIntPrefix(w);
                    else if (!memcmp(&b[i], "ICRD", 4)) t.year = w.substr(0, 4);
                    else if (!memcmp(&b[i], "IGNR", 4)) t.genre = w;
                    i += 8 + l + (l & 1);
                }
            }
        }
        off += 8 + (uint64_t)sz + (sz & 1);
    }
}

// ---------------------------------------------------------------- WMA（Shell のプロパティ）
static std::wstring PropStr(IPropertyStore* ps, const PROPERTYKEY& k) {
    PROPVARIANT v; PropVariantInit(&v); std::wstring r;
    if (SUCCEEDED(ps->GetValue(k, &v)) && v.vt != VT_EMPTY) { PWSTR s = nullptr; if (SUCCEEDED(PropVariantToStringAlloc(v, &s)) && s) { r = s; CoTaskMemFree(s); } }
    PropVariantClear(&v); return r;
}
static void ParseWma(const std::wstring& path, Tags& t) {
    if (zipfs::IsInside(path)) return;   // Shell のプロパティは実ファイルしか読めない（ZIP の中の WMA はタグ無し）
    ComPtr<IPropertyStore> ps;
    if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT | GPS_FASTPROPERTIESONLY, IID_PPV_ARGS(&ps))) || !ps) return;
    t.title = PropStr(ps.Get(), PKEY_Title); t.artist = PropStr(ps.Get(), PKEY_Music_Artist); t.album = PropStr(ps.Get(), PKEY_Music_AlbumTitle);
    t.genre = PropStr(ps.Get(), PKEY_Music_Genre); t.albumArtist = PropStr(ps.Get(), PKEY_Music_AlbumArtist); t.year = PropStr(ps.Get(), PKEY_Media_Year).substr(0, 4);
    t.track = ParseIntPrefix(PropStr(ps.Get(), PKEY_Music_TrackNumber)); t.disc = ParseIntPrefix(PropStr(ps.Get(), PKEY_Music_PartOfSet));
}

bool ReadTags(const std::wstring& path, Tags& t, bool wantPic) {
    t = Tags();
    const wchar_t* e = PathFindExtensionW(path.c_str());
    if (!_wcsicmp(e, L".mp3")) ParseId3(path, t, wantPic);
    else if (!_wcsicmp(e, L".flac")) ParseFlac(path, t, wantPic);
    else if (!_wcsicmp(e, L".m4a") || !_wcsicmp(e, L".aac")) ParseMp4(path, t, wantPic);
    else if (!_wcsicmp(e, L".wav")) ParseWav(path, t);
    else if (!_wcsicmp(e, L".ogg") || !_wcsicmp(e, L".oga") || !_wcsicmp(e, L".opus")) ParseOgg(path, t, wantPic);
    else if (!_wcsicmp(e, L".wma")) ParseWma(path, t);
    t.any = !t.genre.empty() || !t.title.empty() || !t.artist.empty() || !t.album.empty() || t.track || !t.picture.empty();
    return t.any;
}

bool ProbeInfo(const std::wstring& path, ProbeResult& r) {
    Decoder d;
    if (!d.Open(path)) return false;
    const AudioInfo& ai = d.Info();
    r.codec = ai.codec; r.rate = ai.rate; r.dur = ai.duration;
    return true;
}

double ProbeDuration(const std::wstring& path) {
    if (zipfs::IsInside(path)) return 0;   // ZIP の中は ProbeInfo（デコーダーを開いて）の長さを使う
    ComPtr<IMFSourceReader> rd;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &rd)) || !rd) return 0;
    PROPVARIANT pv; PropVariantInit(&pv); double r = 0;
    if (SUCCEEDED(rd->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &pv)) && pv.vt == VT_UI8) r = (double)pv.uhVal.QuadPart / 1e7;
    PropVariantClear(&pv);
    return r;
}

// ---------------------------------------------------------------- 画像
bool DecodeImage(const void* data, size_t len, Image& im) {
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IStream> s; s.Attach(SHCreateMemStream((const BYTE*)data, (UINT)len));
    if (!s) return false;
    ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> fr; ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateDecoderFromStream(s.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) || FAILED(dec->GetFrame(0, &fr))) return false;
    UINT w = 0, h = 0; fr->GetSize(&w, &h);
    if (!w || !h || w > 16384 || h > 16384) return false;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(fr.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    im.px.assign((size_t)w * h, 0);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)im.px.size() * 4, (BYTE*)im.px.data()))) { im.px.clear(); return false; }
    im.w = (int)w; im.h = (int)h;
    return true;
}

bool LoadCover(const std::wstring& trackPath, Image& im) {
    im = Image();
    Tags t;
    if (ReadTags(trackPath, t, true) && !t.picture.empty() && DecodeImage(t.picture.data(), t.picture.size(), im)) return true;
    static const wchar_t* names[] = { L"cover", L"folder", L"front" };
    static const wchar_t* exts[] = { L".jpg", L".jpeg", L".png" };
    std::wstring dir = DirOf(trackPath);
    for (auto n : names) for (auto x : exts) {
        std::wstring p = dir + L"\\" + n + x;
        if (!zipfs::FileExists(p)) continue;
        Bytes b; if (ReadRange(p, 0, 64u << 20, b) && DecodeImage(b.data(), b.size(), im)) return true;
    }
    return false;
}
