// FilePilotUniversal.exe — one double-click adds all-language rendering to File Pilot.
//
// What it does, in place, to FPilot.exe (originals are kept as backups):
//   1. Glyphs: repurpose four spare atlas range-slots to cover Hebrew, Arabic,
//      Indic/Thai, and CJK/Japanese/Korean         (fpglyphs.c logic)
//   2. Write fphook.dll next to the exe             (embedded; bidi + Arabic shaping)
//   3. Make FPilot.exe load that DLL                (fpaddimport.c logic)
//
// Version-generic: step 1 finds the range table by a content anchor (no code edits);
// steps 2-3 install the hook only if the draw-call signature matches this build. If a
// future File Pilot changes the draw code, the hook is skipped and reported, but the
// glyph coverage still applies. Re-run after each File Pilot update.
//
// Usage:
//   double-click                       patch the default install
//   FilePilotUniversal.exe <FPilot.exe>   patch a specific copy (or drag it onto the exe)
//   FilePilotUniversal.exe --undo [path]  restore the clean original from backup
//
// Built by build.bat (which builds fphook.dll first and embeds it as fphook_dll.h).
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <stdio.h>
#include "fphook_dll.h"

// Reuse the two tools by including them; silence their own prompts so we pause once.
#define fputs(s, f) 0
#define getchar() EOF
#define pause_return gl_pause
#define wmain gl_main
#include "fpglyphs.c"
#undef wmain
#undef pause_return
#define pause_return im_pause
#define wmain im_main
#include "fpaddimport.c"
#undef wmain
#undef pause_return
#undef getchar
#undef fputs

static int done(int code) {
    fputs("\nPress Enter to close...", stdout);
    int ch; while ((ch = getchar()) != '\n' && ch != EOF) {}
    return code;
}

static void default_path(wchar_t *out) {
    wchar_t *local = _wgetenv(L"LOCALAPPDATA");
    if (local) _snwprintf(out, MAX_PATH, L"%ls\\Voidstar\\FilePilot\\FPilot.exe", local);
    else out[0] = 0;
}

static int undo(const wchar_t *path) {
    wchar_t clean[MAX_PATH], glyphs[MAX_PATH];
    _snwprintf(clean,  MAX_PATH, L"%ls.ufix-clean",  path);
    _snwprintf(glyphs, MAX_PATH, L"%ls.ufix-glyphs", path);
    if (GetFileAttributesW(clean) == INVALID_FILE_ATTRIBUTES) {
        puts("No .ufix-clean backup found next to that file; nothing to undo.");
        return done(2);
    }
    if (!CopyFileW(clean, path, FALSE)) {
        puts("Could not restore (is File Pilot closed?)."); return done(2);
    }
    DeleteFileW(glyphs);
    wprintf(L"Restored the clean original from:\n  %ls\nFile Pilot is back to as-shipped.\n", clean);
    return done(0);
}

int wmain(int argc, wchar_t **argv) {
    wchar_t src[MAX_PATH], dir[MAX_PATH], dll[MAX_PATH];

    if (argc > 1 && (!wcscmp(argv[1], L"--undo") || !wcscmp(argv[1], L"-u"))) {
        if (argc > 2) { wcsncpy(src, argv[2], MAX_PATH - 1); src[MAX_PATH-1] = 0; }
        else default_path(src);
        if (!src[0]) { puts("Could not determine FPilot.exe path."); return done(2); }
        return undo(src);
    }

    if (argc > 1) { wcsncpy(src, argv[1], MAX_PATH - 1); src[MAX_PATH - 1] = 0; }
    else default_path(src);
    if (!src[0]) { puts("LOCALAPPDATA not set; pass the path to FPilot.exe."); return done(2); }

    wcscpy(dir, src);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (!slash) { puts("Give a full path to FPilot.exe."); return done(2); }
    *slash = 0;
    _snwprintf(dll, MAX_PATH, L"%ls\\fphook.dll", dir);

    wprintf(L"File Pilot: %ls\n", src);
    puts("Originals are kept as FPilot.exe.ufix-clean (pristine) and .ufix-glyphs.");
    puts("Make sure File Pilot is fully closed (including the tray icon).\n");

    puts("[1/3] Adding world-script glyphs...");
    wchar_t *a1[2] = { L"x", src };
    int g = gl_main(2, a1);
    if (g != 0 && g != 0) {}                      // gl_main prints its own detail
    if (g == 2) { puts("\nGlyph step could not open/write the file -- stopping."); return done(2); }
    // g==3 (table not recognized) is fatal for this build; g==0 ok / already done.
    if (g == 3) { puts("\nStopping: this File Pilot build's range table was not recognized."); return done(3); }

    puts("\n[2/3] Writing fphook.dll...");
    HANDLE h = CreateFileW(dll, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD w = 0;
    if (h == INVALID_HANDLE_VALUE || !WriteFile(h, FPH_DLL, FPH_DLL_LEN, &w, NULL) || w != FPH_DLL_LEN) {
        puts("Could not write fphook.dll (close File Pilot and retry).");
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        return done(2);
    }
    CloseHandle(h);

    puts("\n[3/3] Hooking the DLL into File Pilot...");
    int im = im_main(2, a1);
    if (im == 3) {
        puts("\n== PARTIAL ==");
        puts("Glyphs are patched (Hebrew/Arabic/Indic/CJK now have glyphs), but the");
        puts("right-to-left + shaping hook was NOT installed for this build.");
        puts("Hebrew/Arabic will show but in logical (reversed, unshaped) order.");
        puts("The hook needs its signature re-derived for this File Pilot version.");
        return done(3);
    }
    if (im == 2) { puts("\nDLL hook step failed to write. Glyphs are patched; try again with File Pilot closed."); return done(2); }

    puts("\n== DONE ==");
    puts("Start File Pilot normally. Hebrew & Arabic now shape and read right-to-left;");
    puts("Indic, Thai, Chinese, Japanese and Korean filenames show their real glyphs.");
    puts("\nIf File Pilot ever fails to start, run:  FilePilotUniversal.exe --undo");
    puts("Re-run this patcher after each File Pilot update.");
    return done(0);
}
