#pragma once
#include "common.h"

// ZIP の中を「フォルダのように」扱う（第5c段）。目次（セントラルディレクトリ）だけを読み、曲を開くときに1曲だけをメモリに展開する。
// 一時ファイルは作らない。ZIP には書き込まない。展開（Deflate）は自前（inflate）。
// パスの形：`D:\dl\sfx.zip\Hits\01.wav`（ZIP のパスの後ろに、中のパスを `\` でつなぐ）
namespace zipfs {

typedef std::vector<uint8_t> Bytes;

struct DirItem { std::wstring name; bool folder = false; uint64_t size = 0; uint64_t mod = 0; };   // mod: FILETIME（UTC）

class Archive {
public:
    struct Entry {
        std::wstring name;                 // 中のパス（`\` 区切り、末尾に区切りなし）
        bool dir = false;
        uint16_t method = 0, flags = 0;
        uint64_t csize = 0, usize = 0, off = 0;   // off: ローカルヘッダーの位置（ファイルの先頭から）
        uint32_t crc = 0;
        uint64_t mod = 0;
    };
    std::wstring path;                     // ZIP のパス
    uint64_t fileSize = 0, fileMod = 0;
    std::vector<Entry> ents;

    // inner（中のパス。""は根）の種類：0 無い / 1 ファイル / 2 フォルダ
    int Kind(const std::wstring& inner) const;
    const Entry* Find(const std::wstring& inner) const;   // ファイルだけ
    // innerDir の直下のファイルとフォルダを並べる（並び順は決めない）
    void List(const std::wstring& innerDir, std::vector<DirItem>& out) const;
    // 対応形式の曲が直下にあるいちばん浅いフォルダ（同じ深さなら自然順で先）。無ければ false。根なら dir は ""
    bool ShallowestAudioDir(std::wstring& dir) const;

    struct Idx;
    std::shared_ptr<Idx> idx;
};

// path が「ZIP ファイル（実在するファイル）＋中のパス」の形なら true。inner は ZIP そのものなら ""。
// 軽い判定（".zip" を含まなければすぐ false）
bool SplitPath(const std::wstring& path, std::wstring& zip, std::wstring& inner);
inline bool IsInside(const std::wstring& path) { std::wstring z, i; return SplitPath(path, z, i) && !i.empty(); }

// 目次を読む（読んだものは覚えておき、ZIP が変わったら読み直す）。壊れていれば nullptr
std::shared_ptr<const Archive> Open(const std::wstring& zipPath);

// path（ZIP の中の曲）を1つメモリに展開する。パスワード付き・未対応の方式・1GB 超・CRC 不一致・壊れ → nullptr
std::shared_ptr<const Bytes> Acquire(const std::wstring& path);

// 実ファイルでも ZIP の中でも、大きさと更新日時（FILETIME）を返す。無ければ false
bool FileStat(const std::wstring& path, uint64_t& size, uint64_t& mod);
// ファイルとして存在するか（実ファイル or ZIP の中のファイル）
bool FileExists(const std::wstring& path);

}  // namespace zipfs
