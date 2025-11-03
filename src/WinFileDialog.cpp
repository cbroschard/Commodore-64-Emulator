#include "WinFileDialog.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <optional>
#include <string>

static std::optional<std::string>
OpenFileSimple(const char* title, const char* filter)
{
    char fileBuf[MAX_PATH] = {0};

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = NULL;      // or your window handle if you have one
    ofn.lpstrFilter = filter;    // "Name\0*.ext\0...\0\0"
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameA(&ofn))
        return std::nullopt;     // cancel or error

    return std::string(fileBuf);
}

std::optional<std::string> OpenPrgFileDialog()
{
    static const char filters[] =
        "C64 Program\0*.prg;*.P00\0"
        "All Files\0*.*\0\0";

    return OpenFileSimple("Attach PRG image", filters);
}

std::optional<std::string> OpenCartFileDialog()
{
    static const char filters[] =
        "Cartridge\0*.crt\0"
        "All Files\0*.*\0\0";

    return OpenFileSimple("Attach Cartridge image", filters);
}

#else

// Non-Windows stubs
std::optional<std::string> OpenPrgFileDialog()  { return std::nullopt; }
std::optional<std::string> OpenCartFileDialog() { return std::nullopt; }

#endif
