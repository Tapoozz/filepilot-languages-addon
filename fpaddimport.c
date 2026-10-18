// fpaddimport — add "fphook.dll" to a File Pilot exe's import table, so Windows
// loads the bidi/shaping hook at startup. The DLL is listed openly by its real name.
//
// Safety contract:
//   * Backs up to <exe>.ufix-glyphs before changing anything.
//   * Idempotent: refuses (exit 0) if "fphook.dll" is already imported.
//   * Installs the import ONLY if the draw-call signature is present exactly once in
//     the file. On 0 or >1 matches it refuses and changes nothing (the hook would sit
//     inert on an unrecognized build, so we don't pretend it is installed).
//   * Refuses on any unexpected PE layout. It never guesses.
//
// Build: gcc -O2 -municode -o fpaddimport.exe fpaddimport.c
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static int pause_return(int code) {
    fputs("\nPress Enter to close...", stdout);
    int ch; while ((ch = getchar()) != '\n' && ch != EOF) {}
    return code;
}

#define IMPORTED_DLL  "fphook.dll"
#define IMPORTED_SYM  "fphook_marker"

// Same 20-byte draw-call signature as fphook.c (ends in the CALL opcode).
static const uint8_t SIG[20] = {
    0x66,0x0F,0x7F,0x45,0x40, 0x0F,0x29,0x4D,0xA0, 0x66,0x0F,0x7F,0x75,0x00,
    0x48,0x89,0x44,0x24,0x20, 0xE8
};

static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }

static int count_sig(const uint8_t *buf, size_t n) {
    int c = 0;
    for (size_t i = 0; i + 20 <= n; i++) if (!memcmp(buf + i, SIG, 20)) c++;
    return c;
}

int wmain(int argc, wchar_t **argv) {
    if (argc < 2) { puts("Usage: fpaddimport.exe <path to FPilot.exe>"); return pause_return(2); }
    const wchar_t *path = argv[1];
    wprintf(L"Target: %ls\n", path);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { puts("Could not open the file. Is the path right, and File Pilot closed?"); return pause_return(2); }
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart < 0x1000 || sz.QuadPart > 0x10000000) { puts("File size looks wrong; refusing."); CloseHandle(h); return pause_return(2); }
    size_t n = (size_t)sz.QuadPart;
    size_t cap = n + 0x2000;                               // tail room for one appended section
    uint8_t *buf = (uint8_t *)calloc(1, cap);
    DWORD got = 0;
    if (!buf || !ReadFile(h, buf, (DWORD)n, &got, NULL) || got != n) { puts("Read failed."); CloseHandle(h); free(buf); return pause_return(2); }
    CloseHandle(h);

    if (buf[0] != 'M' || buf[1] != 'Z') { puts("Not a PE; refusing."); free(buf); return pause_return(2); }
    uint32_t e_lfanew = *(uint32_t *)(buf + 0x3C);
    if (e_lfanew + 0x108 > n) { puts("Bad PE header; refusing."); free(buf); return pause_return(2); }
    uint8_t *nt = buf + e_lfanew;
    if (memcmp(nt, "PE\0\0", 4)) { puts("No PE signature; refusing."); free(buf); return pause_return(2); }
    IMAGE_FILE_HEADER *fh = (IMAGE_FILE_HEADER *)(nt + 4);
    IMAGE_OPTIONAL_HEADER64 *oh = (IMAGE_OPTIONAL_HEADER64 *)(nt + 4 + sizeof(IMAGE_FILE_HEADER));
    if (oh->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) { puts("Not PE32+ (x64); refusing."); free(buf); return pause_return(2); }

    uint32_t file_align = oh->FileAlignment, sec_align = oh->SectionAlignment;
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)((uint8_t *)oh + fh->SizeOfOptionalHeader);
    int nsec = fh->NumberOfSections;

    // Already patched?
    for (size_t i = 0; i + sizeof(IMPORTED_DLL) <= n; i++)
        if (!memcmp(buf + i, IMPORTED_DLL, sizeof(IMPORTED_DLL))) {
            puts("\nAlready imports fphook.dll. Nothing to do."); free(buf); return pause_return(0);
        }

    // The hook needs the draw-call signature to exist exactly once.
    int sc = count_sig(buf, n);
    if (sc != 1) {
        printf("\nThe draw-call signature was found %d time(s) (need exactly 1).\n", sc);
        puts("The bidi/shaping hook does NOT match this File Pilot build, so it was NOT installed.");
        puts("Glyphs are still patched; Hebrew/Arabic show but stay in logical (reversed) order.");
        puts("(No changes made by this step.)");
        free(buf); return pause_return(3);
    }

    // Room for one more section header?
    uint8_t *hdr_end = (uint8_t *)(sec + nsec);
    uint32_t first_raw = 0xFFFFFFFF;
    for (int i = 0; i < nsec; i++) if (sec[i].PointerToRawData && sec[i].PointerToRawData < first_raw) first_raw = sec[i].PointerToRawData;
    if (hdr_end + sizeof(IMAGE_SECTION_HEADER) > buf + first_raw) {
        puts("\nREFUSING: no room for a new section header in this build. (No changes made.)");
        free(buf); return pause_return(3);
    }

    IMAGE_DATA_DIRECTORY *imp_dir = &oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imp_dir->VirtualAddress || !imp_dir->Size) { puts("No import directory; refusing."); free(buf); return pause_return(3); }
    #define RVA2OFF(rva, off) do { off = 0; for (int s = 0; s < nsec; s++) { \
        uint32_t va = sec[s].VirtualAddress, vs = sec[s].Misc.VirtualSize ? sec[s].Misc.VirtualSize : sec[s].SizeOfRawData; \
        if ((rva) >= va && (rva) < va + vs) { off = sec[s].PointerToRawData + ((rva) - va); break; } } } while (0)
    uint32_t imp_off; RVA2OFF(imp_dir->VirtualAddress, imp_off);
    if (!imp_off) { puts("Import dir RVA unmapped; refusing."); free(buf); return pause_return(3); }
    IMAGE_IMPORT_DESCRIPTOR *old = (IMAGE_IMPORT_DESCRIPTOR *)(buf + imp_off);
    int ndesc = 0; while (old[ndesc].Name) ndesc++;

    // --- Build the new section payload ---
    // Layout: [descriptors (ndesc+2)] [ILT 2] [IAT 2] [hint/name] [dll name] [16B stub]
    uint32_t desc_sz = (uint32_t)((ndesc + 2) * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    uint32_t off_desc = 0;
    uint32_t off_ilt  = off_desc + desc_sz;
    uint32_t off_iat  = off_ilt + 2 * sizeof(uint64_t);
    uint32_t off_name = off_iat + 2 * sizeof(uint64_t);
    uint32_t namesym_sz = 2 + (uint32_t)sizeof(IMPORTED_SYM);
    uint32_t off_dll  = off_name + align_up(namesym_sz, 2);
    uint32_t off_stub = align_up(off_dll + (uint32_t)sizeof(IMPORTED_DLL), 16);
    uint32_t payload_sz = off_stub + 16;

    uint32_t new_va  = align_up(sec[nsec - 1].VirtualAddress +
                        (sec[nsec - 1].Misc.VirtualSize ? sec[nsec - 1].Misc.VirtualSize : sec[nsec - 1].SizeOfRawData), sec_align);
    uint32_t new_raw = align_up((uint32_t)n, file_align);
    uint32_t raw_sz  = align_up(payload_sz, file_align);
    if (new_raw + raw_sz > cap) { puts("Payload too large; refusing."); free(buf); return pause_return(3); }

    uint8_t *P = buf + new_raw;
    memcpy(P + off_desc, old, ndesc * sizeof(IMAGE_IMPORT_DESCRIPTOR));
    IMAGE_IMPORT_DESCRIPTOR *nd = (IMAGE_IMPORT_DESCRIPTOR *)(P + off_desc) + ndesc;
    nd->OriginalFirstThunk = new_va + off_ilt;
    nd->TimeDateStamp = 0; nd->ForwarderChain = 0;
    nd->Name = new_va + off_dll;
    nd->FirstThunk = new_va + off_iat;
    *(uint64_t *)(P + off_ilt) = new_va + off_name;
    *(uint64_t *)(P + off_iat) = new_va + off_name;
    *(uint16_t *)(P + off_name) = 0;
    memcpy(P + off_name + 2, IMPORTED_SYM, sizeof(IMPORTED_SYM));
    memcpy(P + off_dll, IMPORTED_DLL, sizeof(IMPORTED_DLL));

    IMAGE_SECTION_HEADER *ns = sec + nsec;
    memset(ns, 0, sizeof(*ns));
    memcpy(ns->Name, ".fpheb", 6);
    ns->Misc.VirtualSize = payload_sz;
    ns->VirtualAddress = new_va;
    ns->SizeOfRawData = raw_sz;
    ns->PointerToRawData = new_raw;
    ns->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE;

    fh->NumberOfSections = (WORD)(nsec + 1);
    oh->SizeOfImage = align_up(new_va + payload_sz, sec_align);
    imp_dir->VirtualAddress = new_va + off_desc;
    imp_dir->Size = desc_sz;
    // Do NOT touch the IAT data directory: the loader uses it to make the real
    // (read-only) IAT writable while binding. Our thunks live in the writable .fpheb.
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
    oh->DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;

    size_t out_n = new_raw + raw_sz;

    wchar_t bak[MAX_PATH]; _snwprintf(bak, MAX_PATH, L"%ls.ufix-glyphs", path);
    if (!CopyFileW(path, bak, FALSE)) { puts("Could not write backup; refusing."); free(buf); return pause_return(2); }
    wprintf(L"Backup written: %ls\n", bak);
    h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { puts("Could not open for writing (close File Pilot)."); free(buf); return pause_return(2); }
    DWORD wrote = 0; BOOL ok = WriteFile(h, buf, (DWORD)out_n, &wrote, NULL) && wrote == out_n;
    CloseHandle(h); free(buf);
    if (!ok) { puts("Write failed; restore from .ufix-glyphs."); return pause_return(2); }

    printf("\nAdded import of %s (section .fpheb).\n", IMPORTED_DLL);
    return pause_return(0);
}
