#pragma once
#include "common.h"

// 「今のフォルダ」の曲の一覧を作る。サブフォルダは読まない（直下だけ）
bool IsSupportedFile(const wchar_t* path);
// 「開く」ダイアログのフィルタ（OPENFILENAME::lpstrFilter 用。対応拡張子の一覧から作る）
std::wstring OpenFileFilter();

struct FileList {
    std::wstring dir;
    std::vector<std::wstring> paths;   // フルパス（並び順どおり）
};

// dir の対応形式のファイルを並べる。今は名前の自然順（StrCmpLogicalW）だけ。
// 第2段でここに「全曲にトラック番号があれば トラック番号順」を足す
void BuildList(const std::wstring& dir, FileList& fl);
// 並べ替え済みの一覧で path の位置（無ければ -1）
int IndexOf(const FileList& fl, const std::wstring& path);
bool SamePath(const std::wstring& a, const std::wstring& b);
