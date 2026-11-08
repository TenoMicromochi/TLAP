#include "explorer.h"
#include "folder.h"
#include "util.h"
#include "zipfs.h"
#include <shlwapi.h>
#include <algorithm>

namespace explorer {

std::wstring ParentOf(const std::wstring& dir) {
    if (dir.empty() || PathIsRootW(dir.c_str())) return L"";
    std::wstring p = dir;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    std::vector<wchar_t> b(p.begin(), p.end()); b.push_back(0);
    PathRemoveFileSpecW(b.data());
    std::wstring r = b.data();
    if (r.size() == 2 && r[1] == L':') r += L'\\';   // "C:" -> "C:\"
    return r;
}

std::wstring TitleOf(const std::wstring& dir) {
    if (dir.empty()) return L"";
    if (PathIsRootW(dir.c_str())) return dir;
    return NameOf(dir);
}

void Read(const std::wstring& dir, Listing& out) {
    out = Listing(); out.dir = dir;
    DWORD oldMode = 0; SetThreadErrorMode(SEM_FAILCRITICALERRORS, &oldMode);   // ディスクの入っていないドライブで「ディスクを挿入してください」のダイアログを出さない
    struct Restore { DWORD m; ~Restore() { SetThreadErrorMode(m, nullptr); } } restore{ oldMode };
    if (dir.empty()) {   // ドライブの一覧
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; i++) if (mask & (1u << i)) {
            Item it; it.folder = true; it.name = std::wstring(1, (wchar_t)(L'A' + i)) + L":\\"; it.path = it.name; out.items.push_back(it);
        }
        return;
    }
    std::wstring base = dir;
    if (base.back() != L'\\') base += L'\\';
    {   // ZIP の中（ZIP そのもの、または中のフォルダ）
        std::wstring zip, inner;
        if (zipfs::SplitPath(dir, zip, inner)) {
            auto ar = zipfs::Open(zip);
            if (!ar || !ar->Kind(inner)) { out.failed = true; return; }
            std::vector<zipfs::DirItem> items; ar->List(inner, items);
            for (auto& d : items) {
                if (!d.folder && !IsSupportedFile(d.name.c_str())) continue;
                Item it; it.folder = d.folder; it.name = d.name; it.path = base + d.name; out.items.push_back(it);
            }
            std::sort(out.items.begin(), out.items.end(), [](const Item& a, const Item& b) {
                if (a.folder != b.folder) return a.folder;
                return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
            });
            return;
        }
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) { out.failed = true; return; }
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;   // 隠しファイル・システムファイルは出さない
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool isZip = !isDir && !_wcsicmp(PathFindExtensionW(fd.cFileName), L".zip");   // .zip はフォルダの仲間（中に入れる）
        if (!isDir && !isZip && !IsSupportedFile(fd.cFileName)) continue;
        Item it; it.folder = isDir; it.zip = isZip; it.name = fd.cFileName; it.path = base + fd.cFileName;
        out.items.push_back(it);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.items.begin(), out.items.end(), [](const Item& a, const Item& b) {
        int ra = a.folder ? 0 : a.zip ? 1 : 2, rb = b.folder ? 0 : b.zip ? 1 : 2;   // フォルダ → ZIP → 音のファイル
        if (ra != rb) return ra < rb;
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    });
}

}  // namespace explorer
