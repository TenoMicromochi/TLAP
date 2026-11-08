#include "folder.h"
#include "util.h"
#include "lang.h"
#include "zipfs.h"
#include <shlwapi.h>
#include <algorithm>

// 対応形式（SPEC「対応形式」）。.ogg .oga .opus は第2段までデコーダーが無く、MF が開けなければ「再生できない」扱いになる
static const wchar_t* kExts[] = { L".mp3", L".m4a", L".aac", L".flac", L".wav", L".wma", L".ogg", L".oga", L".opus" };

bool IsSupportedFile(const wchar_t* path) {
    const wchar_t* e = PathFindExtensionW(path);
    for (auto x : kExts) if (!_wcsicmp(e, x)) return true;
    return false;
}

// "Audio files\0*.mp3;...\0All files\0*.*\0"
std::wstring OpenFileFilter() {
    std::wstring pat;
    for (auto x : kExts) { if (!pat.empty()) pat += L';'; pat += L'*'; pat += x; }
    pat += L";*.zip";   // ZIP の中を直接読める（第5c段。関連付けはしない。ここで選べるだけ）
    std::wstring r = lang::T(lang::FILTER_AUDIO); r += L'\0'; r += pat; r += L'\0';
    r += lang::T(lang::FILTER_ALL); r += L'\0'; r += L"*.*"; r += L'\0';
    return r;   // c_str() の終端と合わせて、末尾は NUL が二重になる
}

bool SamePath(const std::wstring& a, const std::wstring& b) { return !_wcsicmp(a.c_str(), b.c_str()); }

void BuildList(const std::wstring& dir, FileList& fl) {
    fl = FileList();
    fl.dir = dir;
    {   // ZIP の中のフォルダ
        std::wstring zip, inner;
        if (zipfs::SplitPath(dir, zip, inner)) {
            if (auto ar = zipfs::Open(zip)) {
                std::vector<zipfs::DirItem> items; ar->List(inner, items);
                for (auto& it : items) if (!it.folder && IsSupportedFile(it.name.c_str())) fl.paths.push_back(dir + L"\\" + it.name);
            }
            std::sort(fl.paths.begin(), fl.paths.end(), [](const std::wstring& a, const std::wstring& b) {
                return StrCmpLogicalW(NameOf(a).c_str(), NameOf(b).c_str()) < 0;
            });
            return;
        }
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;   // サブフォルダは読まない
            if (IsSupportedFile(fd.cFileName)) fl.paths.push_back(dir + L"\\" + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(fl.paths.begin(), fl.paths.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(NameOf(a).c_str(), NameOf(b).c_str()) < 0;
    });
}

int IndexOf(const FileList& fl, const std::wstring& path) {
    for (size_t i = 0; i < fl.paths.size(); i++) if (SamePath(fl.paths[i], path)) return (int)i;
    return -1;
}
