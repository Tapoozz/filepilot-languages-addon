// fphook.dll — in-process bidi + Arabic-shaping hook for File Pilot.
//
// On load it finds File Pilot's single text-draw call by a BYTE SIGNATURE (not a
// fixed address), and redirects it through fp_to_visual() so Hebrew/Arabic runs are
// shaped and reordered at draw time only. Filenames in memory and on disk are never
// changed, so open/rename/copy keep working on the correct bytes.
//
// Version-generic by construction:
//   * The call site is located by scanning executable sections for a distinctive
//     20-byte pattern ending in the CALL opcode. It installs ONLY on a unique match;
//     on 0 or >1 matches it does nothing and File Pilot runs exactly as shipped.
//   * It rewrites only the 4-byte rel32 inside that existing CALL, routed through a
//     16-byte jump stub placed in the exe's own .fpheb section (added by fpaddimport).
//   * No new code pages, no IAT games, no system-DLL masquerade, no files, no network.
//
// Build (ucrt64):  gcc -O2 -shared -o fphook.dll fphook.c fprtl.c
#include <windows.h>
#include <string.h>
#include <stdint.h>

size_t fp_to_visual(const uint8_t *src, size_t srclen, uint8_t *dst);  // fprtl.c

// File Pilot's text slice: { pointer, length } UTF-8.
typedef struct { uint8_t *ptr; uint64_t len; } Slice;
typedef void *(*draw_fn)(void *arena, void *font, void *rect,
                         Slice *text, uint64_t color, void *opts);
static draw_fn g_original = 0;

// Draw call site anchor (File Pilot 0.8.5 and, by signature, compatible builds):
//   movdqa [rbp+40],xmm0 ; movaps [rbp-60],xmm1 ; movdqa [rbp+00],xmm6 ;
//   mov [rsp+20],rax ; CALL rel32
// The 20th byte (0xE8) is the CALL; rel32 follows. Distinctive enough to be unique.
static const uint8_t SIG[20] = {
    0x66,0x0F,0x7F,0x45,0x40, 0x0F,0x29,0x4D,0xA0, 0x66,0x0F,0x7F,0x75,0x00,
    0x48,0x89,0x44,0x24,0x20, 0xE8
};

// Find the unique call site across executable sections. Returns the E8 address, or 0.
static uint8_t *find_call_site(uint8_t *base) {
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    uint8_t *hit = 0; int count = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!(s[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t *p = base + s[i].VirtualAddress;
        uint32_t vs = s[i].Misc.VirtualSize ? s[i].Misc.VirtualSize : s[i].SizeOfRawData;
        if (vs < 20) continue;
        for (uint32_t off = 0; off + 20 <= vs; off++)
            if (!memcmp(p + off, SIG, 20)) { hit = p + off + 19; count++; if (count > 1) return 0; }
    }
    return count == 1 ? hit : 0;  // E8 byte address
}

static void *hooked_draw(void *arena, void *font, void *rect,
                         Slice *text, uint64_t color, void *opts) {
    if (text && text->ptr && text->len && text->len <= 4096) {
        uint8_t buf[8192];                                   // >= 2x input, on the stack
        size_t n = fp_to_visual(text->ptr, (size_t)text->len, buf);
        if (n) { Slice vis = { buf, n }; return g_original(arena, font, rect, &vis, color, opts); }
    }
    return g_original(arena, font, rect, text, color, opts);
}

static int install(void) {
    uint8_t *base = (uint8_t *)GetModuleHandleW(NULL);
    uint8_t *call = find_call_site(base);                    // points at the E8
    if (!call) return 0;                                     // 0 or >1 matches -> inert

    int32_t rel; memcpy(&rel, call + 1, 4);
    g_original = (draw_fn)(call + 5 + rel);

    // The DLL loads far (>2GB) from the exe, so rel32 can't reach hooked_draw directly.
    // Route through a jump stub in the exe's own .fpheb section (added by fpaddimport).
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    uint8_t *stub = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (!memcmp(s[i].Name, ".fpheb", 6))
            stub = base + s[i].VirtualAddress + s[i].Misc.VirtualSize - 16;
    if (!stub) return 0;                                     // not the prepared copy -> inert

    static const uint8_t jmp[6] = {0xFF, 0x25, 0, 0, 0, 0};  // jmp qword ptr [rip+0]
    void *target = (void *)hooked_draw;
    memcpy(stub, jmp, 6);
    memcpy(stub + 6, &target, 8);
    int32_t new_rel = (int32_t)(stub - (call + 5));
    DWORD old;
    if (!VirtualProtect(call + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(call + 1, &new_rel, 4);
    VirtualProtect(call + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return 1;
}

// One exported symbol for the exe's import table to bind to (that is what loads us).
__declspec(dllexport) int fphook_marker(void) { return 1; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(h); install(); }
    return TRUE;
}
