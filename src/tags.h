#pragma once
#include "common.h"

// タグ（曲の情報）とジャケットの読み取り。見るだけで、書き換えはしない。自前のパーサー。
// ID3v2.3/2.4（MP3）・FLAC の Vorbis comment と PICTURE・MP4 の ilst・WAV の LIST INFO・Ogg Vorbis/Opus の comment。
// WMA は Shell のプロパティ（IPropertyStore）で読む
struct Tags {
    std::wstring title, artist, album, albumArtist, year, genre;
    int track = 0, disc = 0;          // 0 = タグに無い
    std::vector<uint8_t> picture;     // 埋め込み画像のそのままのバイト列（wantPicture のときだけ）
    bool any = false;                 // 何か1つでも読めた
};

bool ReadTags(const std::wstring& path, Tags& t, bool wantPicture);
double ProbeDuration(const std::wstring& path);
// 形式・サンプルレート・長さをまとめて知る（デコーダーを開いて確かめる。ogg / opus の長さもここで分かる）。開けなければ false
struct ProbeResult { std::wstring codec; int rate = 0; double dur = 0; };
bool ProbeInfo(const std::wstring& path, ProbeResult& r);   // 秒（Media Foundation に聞く。デコードはしない。分からなければ 0）

// 画像（WIC で読んだもの。プリマルチの BGRA、上から下）
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    bool empty() const { return px.empty(); }
};
bool DecodeImage(const void* data, size_t len, Image& im);
// 曲の埋め込み画像 → 同じフォルダの cover / folder / front（.jpg .jpeg .png）の順に探す。無ければ false
bool LoadCover(const std::wstring& trackPath, Image& im);
