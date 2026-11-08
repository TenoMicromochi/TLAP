#pragma once
#include "common.h"

// エクスプローラ（左の列）の中身を読む。フォルダの中は裏のスレッドで読む（呼ぶのは main のスレッド）
namespace explorer {

struct Item { std::wstring name, path; bool folder = false; bool zip = false; };   // zip: .zip ファイル（中に入れる。folder は false のまま）

struct Listing {
    std::wstring dir;            // 見ているフォルダ（"" = ドライブの一覧）
    std::vector<Item> items;     // フォルダ（自然順）→ ZIP（自然順）→ 対応形式の曲（自然順）。ドライブの一覧のときはドライブ
    bool failed = false;         // 読めなかった（(cannot open)）
};

// dir の中身を読む（ブロックする。裏のスレッドから呼ぶ）。dir が空ならドライブの一覧
void Read(const std::wstring& dir, Listing& out);
// ひとつ上のフォルダ。ドライブの根（C:\）や UNC の共有の根の上は "" （ドライブの一覧）
std::wstring ParentOf(const std::wstring& dir);
// 見出しに出す名前（フォルダ名。根は C:\ のまま）
std::wstring TitleOf(const std::wstring& dir);

}  // namespace explorer
