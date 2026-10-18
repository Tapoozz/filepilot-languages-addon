// fpglyphs — add world-script glyph coverage to File Pilot.
//
// File Pilot builds its font atlas from a hardcoded, zero-terminated table of Unicode
// ranges. Blocks are allocated on demand, and a single range can span up to 65,535
// code points, so a few spare slots can cover most living scripts. This tool
// repurposes four rarely-used Cyrillic-extension slots:
//
//   0500-052F Cyrillic Supplement   -> 0590-06FF  Hebrew + Arabic (+ Supplement)
//   2DE0-2DFF Cyrillic Extended-A   -> FE70-FEFF  Arabic Presentation Forms-B (shaping)
//   A640-A69F Cyrillic Extended-B   -> 0900-0E7F  Devanagari ... Thai (Indic + SE Asian)
//   1C80-1C8F Cyrillic Extended-C   -> 3040-D7AF  Kana + CJK + Hangul (CJK / JP / KR)
//
// No code is modified: only the 32 bytes of those four {first,last} pairs. That makes
// it robust across File Pilot updates.
//
// Safety:
//   * Locates the table by a 48-byte anchor: the Cyrillic block flanked by Cyrillic
//     (0400-04FF) before and Combining Diacriticals (0300-036F) after. Not a fixed offset.
//   * Patches ONLY if that anchor is found EXACTLY ONCE; otherwise refuses, no changes.
//   * Backs up the clean file to <exe>.ufix-clean before patching.
//   * Idempotent: if the ranges are already present, reports and exits 0.
//
// Build: gcc -O2 -municode -o fpglyphs.exe fpglyphs.c
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// {first,last} as little-endian u32 pairs, exactly as they sit in .data.
#define PAIR(lo,hi) (lo)&0xff,((lo)>>8)&0xff,((lo)>>16)&0xff,((lo)>>24)&0xff, \
                    (hi)&0xff,((hi)>>8)&0xff,((hi)>>16)&0xff,((hi)>>24)&0xff
static const uint8_t BEFORE[8] = { PAIR(0x0400,0x04FF) };   // Cyrillic
static const uint8_t AFTER [8] = { PAIR(0x0300,0x036F) };   // Combining Diacriticals
static const uint8_t OLD[32] = {                            // the four spare slots
    PAIR(0x0500,0x052F), PAIR(0x2DE0,0x2DFF), PAIR(0xA640,0xA69F), PAIR(0x1C80,0x1C8F)
};
static const uint8_t NEW[32] = {                            // our replacements
    PAIR(0x0590,0x06FF), PAIR(0xFE70,0xFEFF), PAIR(0x0900,0x0E7F), PAIR(0x3040,0xD7AF)
};

static int count(const uint8_t *hay, size_t hn, const uint8_t *needle, size_t nn, size_t *first) {
    int c = 0;
    for (size_t i = 0; i + nn <= hn; i++)
        if (!memcmp(hay + i, needle, nn)) { if (c == 0 && first) *first = i; c++; i += nn - 1; }
    return c;
}
static int pause_return(int code) {
    fputs("\nPress Enter to close...", stdout);
    int ch; while ((ch = getchar()) != '\n' && ch != EOF) {}
    return code;
}

int wmain(int argc, wchar_t **argv) {
    wchar_t path[MAX_PATH];
    if (argc > 1) { wcsncpy(path, argv[1], MAX_PATH - 1); path[MAX_PATH - 1] = 0; }
    else {
        wchar_t *local = _wgetenv(L"LOCALAPPDATA");
        if (!local) { puts("LOCALAPPDATA not set; pass the path to FPilot.exe."); return pause_return(2); }
        _snwprintf(path, MAX_PATH, L"%ls\\Voidstar\\FilePilot\\FPilot.exe", local);
    }
    wprintf(L"Target: %ls\n", path);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_SHARING_VIOLATION) puts("File Pilot is running. Close it fully (incl. tray icon) and retry.");
        else puts("Could not open FPilot.exe. Pass its path as an argument.");
        return pause_return(2);
    }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart < 0x1000 || sz.QuadPart > 0x10000000) { puts("File size looks wrong; refusing."); CloseHandle(h); return pause_return(2); }
    size_t n = (size_t)sz.QuadPart;
    uint8_t *buf = (uint8_t *)malloc(n);
    DWORD got = 0;
    if (!buf || !ReadFile(h, buf, (DWORD)n, &got, NULL) || got != n) { puts("Read failed."); CloseHandle(h); free(buf); return pause_return(2); }
    CloseHandle(h);
    if (buf[0] != 'M' || buf[1] != 'Z') { puts("Not a Windows executable; refusing."); free(buf); return pause_return(2); }

    // Full 48-byte anchors: [BEFORE][four slots][AFTER], clean vs patched.
    uint8_t anchor[48], patched[48];
    memcpy(anchor,       BEFORE, 8); memcpy(anchor + 8,  OLD, 32); memcpy(anchor + 40,  AFTER, 8);
    memcpy(patched,      BEFORE, 8); memcpy(patched + 8, NEW, 32); memcpy(patched + 40, AFTER, 8);

    size_t at = 0;
    int have_patched = count(buf, n, patched, 48, NULL);
    int have_anchor  = count(buf, n, anchor,  48, &at);

    if (have_patched > 0 && have_anchor == 0) {
        puts("\nAlready patched: world-script ranges are present. Nothing to do.");
        free(buf); return pause_return(0);
    }
    if (have_anchor == 0) {
        puts("\nREFUSING: the Unicode range table was not found in the expected form.");
        puts("This File Pilot version differs from what this tool understands, so it will");
        puts("NOT modify the file. (No changes were made.)");
        free(buf); return pause_return(3);
    }
    if (have_anchor > 1) {
        puts("\nREFUSING: the range-table anchor matched more than once (ambiguous). No changes.");
        free(buf); return pause_return(3);
    }

    wchar_t bak[MAX_PATH]; _snwprintf(bak, MAX_PATH, L"%ls.ufix-clean", path);
    if (!CopyFileW(path, bak, FALSE)) { puts("Could not write backup; refusing to patch."); free(buf); return pause_return(2); }
    wprintf(L"Backup written: %ls\n", bak);

    memcpy(buf + at + 8, NEW, 32);                          // overwrite the four slots

    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { puts("Could not open FPilot.exe for writing. Close File Pilot and retry."); free(buf); return pause_return(2); }
    DWORD wrote = 0; BOOL ok = WriteFile(h, buf, (DWORD)n, &wrote, NULL) && wrote == n;
    CloseHandle(h); free(buf);
    if (!ok) { puts("Write failed; restore from the .ufix-clean file."); return pause_return(2); }

    printf("\nPatched glyph ranges at file offset 0x%zx (32 bytes).\n", at + 8);
    puts("Hebrew, Arabic, Indic/Thai, and CJK/Japanese/Korean glyphs are now covered.");
    return pause_return(0);
}
