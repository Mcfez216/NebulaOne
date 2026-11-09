// NebulaOne - small platform helpers: the desktop shortcut for portable copies.
// The Windows installer and the Linux packages already make one; this button
// covers the portable .zip and the Linux tarball.
#include <filesystem>
#include <fstream>
#include <string>

#include "app/widgets.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#else
#include <unistd.h>
#endif

namespace nb {
namespace fs = std::filesystem;

#if defined(_WIN32)
bool createDesktopShortcut(std::string* where, std::string* err) {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) { if (err) *err = "cannot find the game's own path"; return false; }
    wchar_t desk[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk))) {
        if (err) *err = "cannot find the desktop folder";
        return false;
    }
    fs::path link = fs::path(desk) / L"NebulaOne.lnk";
    HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* sl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl);
    if (SUCCEEDED(hr)) {
        sl->SetPath(exe);
        sl->SetWorkingDirectory(fs::path(exe).parent_path().c_str());
        sl->SetDescription(L"NebulaOne - run a far-future corporation");
        sl->SetIconLocation(exe, 0);
        IPersistFile* pf = nullptr;
        hr = sl->QueryInterface(IID_IPersistFile, (void**)&pf);
        if (SUCCEEDED(hr)) {
            hr = pf->Save(link.c_str(), TRUE);
            pf->Release();
        }
        sl->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    if (FAILED(hr)) { if (err) *err = "Windows refused to write the shortcut"; return false; }
    if (where) *where = link.string();
    return true;
}
#else
static fs::path desktopDir(const fs::path& home) {
    std::ifstream f(home / ".config" / "user-dirs.dirs");
    std::string line;
    while (std::getline(f, line)) {
        const std::string key = "XDG_DESKTOP_DIR=\"";
        if (line.rfind(key, 0) != 0) continue;
        std::string v = line.substr(key.size());
        if (!v.empty() && v.back() == '"') v.pop_back();
        if (v.rfind("$HOME", 0) == 0) v = home.string() + v.substr(5);
        return fs::path(v);
    }
    return home / "Desktop";
}

bool createDesktopShortcut(std::string* where, std::string* err) {
    const char* h = std::getenv("HOME");
    if (!h) { if (err) *err = "HOME is not set"; return false; }
    fs::path home(h);
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) { if (err) *err = "cannot find the game's own path"; return false; }
    fs::path desk = desktopDir(home);
    if (!fs::is_directory(desk)) { if (err) *err = "there is no desktop folder at " + desk.string(); return false; }
    // An installed copy already has its icon in the theme; a tarball copy needs one.
    std::string icon = "nebulaone";
    if (exe.string().rfind("/usr/", 0) != 0) {
        fs::path dir = home / ".local" / "share" / "icons" / "hicolor" / "256x256" / "apps";
        fs::create_directories(dir, ec);
        size_t n = 0;
        const unsigned char* png = embedded("icon-256.png", &n);
        std::ofstream o(dir / "nebulaone.png", std::ios::binary);
        if (png && o) { o.write((const char*)png, (std::streamsize)n); icon = (dir / "nebulaone.png").string(); }
    }
    fs::path file = desk / "nebulaone.desktop";
    std::ofstream o(file);
    if (!o) { if (err) *err = "cannot write " + file.string(); return false; }
    o << "[Desktop Entry]\nType=Application\nName=NebulaOne\nComment=Run a far-future corporation in a player-driven economy\n"
      << "Exec=\"" << exe.string() << "\"\nIcon=" << icon << "\nTerminal=false\nCategories=Game;StrategyGame;\n";
    o.close();
    fs::permissions(file, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
    // GNOME and Cinnamon only launch trusted desktop files; ignore failure elsewhere.
    std::string cmd = "gio set '" + file.string() + "' metadata::trusted true >/dev/null 2>&1";
    if (std::system(cmd.c_str())) {}
    if (where) *where = file.string();
    return true;
}
#endif

}  // namespace nb
