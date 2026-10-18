// fprtl.c — logical-to-visual text transform for File Pilot's LTR glyph renderer.
//
// File Pilot draws glyphs strictly left-to-right in memory order, with no bidi and
// no shaping. This turns a logical-order UTF-8 string into the bytes that, drawn
// that way, read correctly:
//   * Arabic: contextual shaping (isolated/initial/medial/final + lam-alef ligatures)
//     into Arabic Presentation Forms-B, then RTL reorder.
//   * Hebrew: RTL reorder (no shaping needed).
//   * Latin/digits and everything else: untouched, in order.
//
// Pure and side-effect-free: no process, no files, no allocation beyond fixed stack
// buffers. Delivery into File Pilot is a separate, deliberate step (fphook.c).
//
// Self-test:  gcc -O2 -DFPRTL_TEST -o fprtl_test.exe fprtl.c && ./fprtl_test.exe
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define MAX_CP 1024

// ---------------- Arabic shaping ----------------
// Per base letter: the four presentation forms and join capability.
// canL = can connect to the FOLLOWING logical letter (its left, in RTL).
// canR = can receive a connection from the PRECEDING logical letter (its right).
typedef struct { uint16_t iso, fin, ini, med; uint8_t canL, canR; } ArInfo;

#define AR_LO 0x0621
#define AR_HI 0x064A
// Direct-indexed table over U+0621..U+064A. Zeroed entries = not a shaping letter.
static const ArInfo AR[AR_HI - AR_LO + 1] = {
    [0x0621 - AR_LO] = {0xFE80,0,0,0, 0,0},                 // hamza (isolated only)
    [0x0622 - AR_LO] = {0xFE81,0xFE82,0,0, 0,1},            // alef madda
    [0x0623 - AR_LO] = {0xFE83,0xFE84,0,0, 0,1},            // alef hamza above
    [0x0624 - AR_LO] = {0xFE85,0xFE86,0,0, 0,1},            // waw hamza
    [0x0625 - AR_LO] = {0xFE87,0xFE88,0,0, 0,1},            // alef hamza below
    [0x0626 - AR_LO] = {0xFE89,0xFE8A,0xFE8B,0xFE8C, 1,1},  // yeh hamza
    [0x0627 - AR_LO] = {0xFE8D,0xFE8E,0,0, 0,1},            // alef
    [0x0628 - AR_LO] = {0xFE8F,0xFE90,0xFE91,0xFE92, 1,1},  // beh
    [0x0629 - AR_LO] = {0xFE93,0xFE94,0,0, 0,1},            // teh marbuta
    [0x062A - AR_LO] = {0xFE95,0xFE96,0xFE97,0xFE98, 1,1},  // teh
    [0x062B - AR_LO] = {0xFE99,0xFE9A,0xFE9B,0xFE9C, 1,1},  // theh
    [0x062C - AR_LO] = {0xFE9D,0xFE9E,0xFE9F,0xFEA0, 1,1},  // jeem
    [0x062D - AR_LO] = {0xFEA1,0xFEA2,0xFEA3,0xFEA4, 1,1},  // hah
    [0x062E - AR_LO] = {0xFEA5,0xFEA6,0xFEA7,0xFEA8, 1,1},  // khah
    [0x062F - AR_LO] = {0xFEA9,0xFEAA,0,0, 0,1},            // dal
    [0x0630 - AR_LO] = {0xFEAB,0xFEAC,0,0, 0,1},            // thal
    [0x0631 - AR_LO] = {0xFEAD,0xFEAE,0,0, 0,1},            // reh
    [0x0632 - AR_LO] = {0xFEAF,0xFEB0,0,0, 0,1},            // zain
    [0x0633 - AR_LO] = {0xFEB1,0xFEB2,0xFEB3,0xFEB4, 1,1},  // seen
    [0x0634 - AR_LO] = {0xFEB5,0xFEB6,0xFEB7,0xFEB8, 1,1},  // sheen
    [0x0635 - AR_LO] = {0xFEB9,0xFEBA,0xFEBB,0xFEBC, 1,1},  // sad
    [0x0636 - AR_LO] = {0xFEBD,0xFEBE,0xFEBF,0xFEC0, 1,1},  // dad
    [0x0637 - AR_LO] = {0xFEC1,0xFEC2,0xFEC3,0xFEC4, 1,1},  // tah
    [0x0638 - AR_LO] = {0xFEC5,0xFEC6,0xFEC7,0xFEC8, 1,1},  // zah
    [0x0639 - AR_LO] = {0xFEC9,0xFECA,0xFECB,0xFECC, 1,1},  // ain
    [0x063A - AR_LO] = {0xFECD,0xFECE,0xFECF,0xFED0, 1,1},  // ghain
    [0x0641 - AR_LO] = {0xFED1,0xFED2,0xFED3,0xFED4, 1,1},  // feh
    [0x0642 - AR_LO] = {0xFED5,0xFED6,0xFED7,0xFED8, 1,1},  // qaf
    [0x0643 - AR_LO] = {0xFED9,0xFEDA,0xFEDB,0xFEDC, 1,1},  // kaf
    [0x0644 - AR_LO] = {0xFEDD,0xFEDE,0xFEDF,0xFEE0, 1,1},  // lam
    [0x0645 - AR_LO] = {0xFEE1,0xFEE2,0xFEE3,0xFEE4, 1,1},  // meem
    [0x0646 - AR_LO] = {0xFEE5,0xFEE6,0xFEE7,0xFEE8, 1,1},  // noon
    [0x0647 - AR_LO] = {0xFEE9,0xFEEA,0xFEEB,0xFEEC, 1,1},  // heh
    [0x0648 - AR_LO] = {0xFEED,0xFEEE,0,0, 0,1},            // waw
    [0x0649 - AR_LO] = {0xFEEF,0xFEF0,0,0, 0,1},            // alef maksura (right-join form)
    [0x064A - AR_LO] = {0xFEF1,0xFEF2,0xFEF3,0xFEF4, 1,1},  // yeh
};

static const ArInfo *ar(uint32_t c) {
    if (c < AR_LO || c > AR_HI) return 0;
    const ArInfo *p = &AR[c - AR_LO];
    return p->iso ? p : 0;  // zeroed entry = not a shaping letter
}

// Combining marks / transparent: do not break joining and carry no form change.
static int ar_transparent(uint32_t c) {
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670 ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x06D6 && c <= 0x06DC) ||
           (c >= 0x06DF && c <= 0x06E4) || (c >= 0x06E7 && c <= 0x06E8) ||
           (c >= 0x06EA && c <= 0x06ED);
}

// Lam-Alef ligature: lam (0644) + one of these alef variants -> one glyph.
// ponytail: standard mandatory ligature; the only multi-char Arabic ligature worth handling.
static uint16_t lam_alef(uint32_t alef, int joinPrev) {
    switch (alef) {
        case 0x0622: return joinPrev ? 0xFEF6 : 0xFEF5;
        case 0x0623: return joinPrev ? 0xFEF8 : 0xFEF7;
        case 0x0625: return joinPrev ? 0xFEFA : 0xFEF9;
        case 0x0627: return joinPrev ? 0xFEFC : 0xFEFB;
    }
    return 0;
}

static int prev_sig(const uint32_t *in, int i) {           // previous non-transparent
    for (int j = i - 1; j >= 0; j--) if (!ar_transparent(in[j])) return j;
    return -1;
}
static int next_sig(const uint32_t *in, int n, int i) {    // next non-transparent
    for (int j = i + 1; j < n; j++) if (!ar_transparent(in[j])) return j;
    return -1;
}

// Shape in logical order: in[n] -> out[], returns new count (<= n; lam-alef shrinks).
static int shape_arabic(const uint32_t *in, int n, uint32_t *out) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        uint32_t c = in[i];
        const ArInfo *info = ar(c);
        if (!info) { out[m++] = c; continue; }

        if (c == 0x0644) {                                 // lam: try lam-alef
            int q = next_sig(in, n, i);
            if (q >= 0) {
                uint16_t pre = 0;
                int p = prev_sig(in, i);
                int joinPrev = p >= 0 && ar(in[p]) && ar(in[p])->canL;
                if ((pre = lam_alef(in[q], joinPrev))) {
                    out[m++] = pre;
                    i = q;                                 // consume the alef
                    continue;                              // (marks between lam/alef dropped)
                }
            }
        }

        int p = prev_sig(in, i), q = next_sig(in, n, i);
        const ArInfo *pi = p >= 0 ? ar(in[p]) : 0;
        const ArInfo *qi = q >= 0 ? ar(in[q]) : 0;
        int prevJoins = pi && pi->canL && info->canR;
        int nextJoins = qi && info->canL && qi->canR;
        uint16_t form;
        if (prevJoins && nextJoins) form = info->med ? info->med : info->fin;
        else if (nextJoins)         form = info->ini ? info->ini : info->iso;
        else if (prevJoins)         form = info->fin;
        else                        form = info->iso;
        out[m++] = form;
    }
    return m;
}

// ---------------- bidi (base direction LTR) ----------------

static int is_rtl(uint32_t c) {
    return (c >= 0x0590 && c <= 0x05FF) ||   // Hebrew
           (c >= 0x0600 && c <= 0x06FF) ||   // Arabic
           (c >= 0x0750 && c <= 0x077F) ||   // Arabic Supplement
           (c >= 0xFB1D && c <= 0xFB4F) ||   // Hebrew presentation forms
           (c >= 0xFB50 && c <= 0xFDFF) ||   // Arabic presentation forms-A
           (c >= 0xFE70 && c <= 0xFEFF);     // Arabic presentation forms-B
}
static int is_neutral(uint32_t c) {
    if (c == ' ') return 1;
    if (c >= 0x21 && c <= 0x2F) return 1;   // ! " # $ % & ' ( ) * + , - . /
    if (c >= 0x3A && c <= 0x40) return 1;   // : ; < = > ? @
    if (c >= 0x5B && c <= 0x60) return 1;   // [ \ ] ^ _ `
    if (c >= 0x7B && c <= 0x7E) return 1;   // { | } ~
    return c >= 0x2000 && c <= 0x206F;      // general punctuation
}

// Direction class for bidi. EN = European digit (always displayed left-to-right).
enum { T_R, T_L, T_EN, T_N };
static int classify(uint32_t c) {
    if (is_rtl(c))             return T_R;
    if (c >= '0' && c <= '9')  return T_EN;
    if (is_neutral(c))         return T_N;
    return T_L;                              // letters, CJK, and other strong-LTR
}
static uint32_t mirror(uint32_t c) {
    switch (c) {
        case '(': return ')'; case ')': return '(';
        case '[': return ']'; case ']': return '[';
        case '{': return '}'; case '}': return '{';
        case '<': return '>'; case '>': return '<';
    }
    return c;
}

static size_t utf8_decode(const uint8_t *s, size_t n, uint32_t *cp) {
    uint8_t b = s[0];
    size_t len = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    if (!len || len > n) return 0;
    uint32_t c = len == 1 ? b : (uint32_t)(b & (0x7F >> len));
    for (size_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        c = (c << 6) | (s[i] & 0x3F);
    }
    *cp = c;
    return len;
}
static size_t utf8_encode(uint32_t c, uint8_t *o) {
    if (c < 0x80)    { o[0] = (uint8_t)c; return 1; }
    if (c < 0x800)   { o[0] = 0xC0 | c >> 6;  o[1] = 0x80 | (c & 63); return 2; }
    if (c < 0x10000) { o[0] = 0xE0 | c >> 12; o[1] = 0x80 | (c >> 6 & 63); o[2] = 0x80 | (c & 63); return 3; }
    o[0] = 0xF0 | c >> 18; o[1] = 0x80 | (c >> 12 & 63); o[2] = 0x80 | (c >> 6 & 63); o[3] = 0x80 | (c & 63); return 4;
}

// Public: logical UTF-8 (srclen bytes) -> visual UTF-8 in dst (needs >= 2*srclen).
// Returns bytes written, or 0 when there is nothing to do (no RTL) / invalid UTF-8
// / too long. On 0 the caller draws src unchanged.
size_t fp_to_visual(const uint8_t *src, size_t srclen, uint8_t *dst) {
    uint32_t cp[MAX_CP], sh[MAX_CP];
    int lev[MAX_CP], typ[MAX_CP];
    int n = 0, any_rtl = 0;
    size_t pos = 0;

    while (pos < srclen) {
        if (n >= MAX_CP) return 0;
        uint32_t c;
        size_t k = utf8_decode(src + pos, srclen - pos, &c);
        if (!k) return 0;
        cp[n++] = c;
        any_rtl |= is_rtl(c);
        pos += k;
    }
    if (!any_rtl) return 0;                 // pure LTR: leave to caller

    int m = shape_arabic(cp, n, sh);        // Arabic -> presentation forms (Hebrew unchanged)
    for (int i = 0; i < m; i++) typ[i] = classify(sh[i]);

    // Base paragraph direction = direction of the FIRST strong character (Unicode P2/P3).
    // This is the "decide by the first letter of the name" rule: a name that starts with
    // Hebrew/Arabic lays out right-to-left (so trailing !/? and embedded English sit on the
    // correct side); one that starts with a Latin letter stays left-to-right.
    int baseRTL = 0;
    for (int i = 0; i < m; i++) {
        if (typ[i] == T_R) { baseRTL = 1; break; }
        if (typ[i] == T_L) { baseRTL = 0; break; }   // EN / neutral don't set the base
    }

    // Embedding levels (two levels deep is enough here): R=1; L/EN = 0 (LTR base) or 2 (RTL base).
    int baseL0 = baseRTL ? 2 : 0;
    for (int i = 0; i < m; i++) {
        if      (typ[i] == T_R)  lev[i] = 1;
        else if (typ[i] == T_N)  lev[i] = -1;         // neutral: resolve below
        else                     lev[i] = baseL0;     // L or EN
    }
    // Resolve neutral runs: a neutral takes the surrounding direction when both sides agree,
    // otherwise the base direction (and at the string ends, the base direction).
    for (int i = 0; i < m; ) {
        if (lev[i] != -1) { i++; continue; }
        int j = i; while (j < m && lev[j] == -1) j++;
        int ld = (i > 0)  ? ((typ[i - 1] == T_R || typ[i - 1] == T_EN) ? 1 : 0) : baseRTL;
        int rd = (j < m)  ? ((typ[j]     == T_R || typ[j]     == T_EN) ? 1 : 0) : baseRTL;
        int dir = (ld == rd) ? ld : baseRTL;
        int nlev = dir ? 1 : baseL0;
        for (int k = i; k < j; k++) lev[k] = nlev;
        i = j;
    }

    // Mirror paired punctuation at odd (RTL) levels.
    for (int i = 0; i < m; i++) if (lev[i] & 1) sh[i] = mirror(sh[i]);

    // Reorder for display (Unicode L2): from the highest level down to 1, reverse each
    // maximal run at that level or above. Reversing twice keeps digits (level 2) upright.
    int maxL = 0; for (int i = 0; i < m; i++) if (lev[i] > maxL) maxL = lev[i];
    for (int L = maxL; L >= 1; L--) {
        for (int i = 0; i < m; ) {
            if (lev[i] < L) { i++; continue; }
            int j = i; while (j < m && lev[j] >= L) j++;
            for (int a = i, b = j - 1; a < b; a++, b--) {
                uint32_t t = sh[a]; sh[a] = sh[b]; sh[b] = t;
                int tl = lev[a]; lev[a] = lev[b]; lev[b] = tl;
            }
            i = j;
        }
    }

    size_t out = 0;
    for (int i = 0; i < m; i++) {
        if (out + 4 > 2 * srclen) return 0; // guard dst
        out += utf8_encode(sh[i], dst + out);
    }
    return out;
}

#ifdef FPRTL_TEST
#include <stdio.h>
static size_t enc(uint32_t c, uint8_t *o) { return utf8_encode(c, o); }
static int eqcp(const uint8_t *buf, size_t n, const uint32_t *want, int wn) {
    uint8_t tmp[256]; size_t t = 0;
    for (int i = 0; i < wn; i++) t += enc(want[i], tmp + t);
    return n == t && !memcmp(buf, tmp, t);
}
static void show(const char *label, const char *in) {
    uint8_t out[4096];
    size_t n = fp_to_visual((const uint8_t *)in, strlen(in), out);
    if (n) { out[n] = 0; printf("  %-18s visual=%s\n", label, out); }
    else   { printf("  %-18s (unchanged)\n", label); }
}
int main(void) {
    uint8_t out[256];
    int fails = 0;

    // 1) Hebrew reverses, ASCII untouched.
    const char *shalom = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";       // שלום
    uint32_t shalom_rev[] = {0x05DD,0x05D5,0x05DC,0x05E9};         // םולש
    size_t n = fp_to_visual((const uint8_t*)shalom, strlen(shalom), out);
    if (!eqcp(out, n, shalom_rev, 4)) { printf("FAIL hebrew\n"); fails++; }
    if (fp_to_visual((const uint8_t*)"report.txt", 10, out) != 0) { printf("FAIL ascii\n"); fails++; }

    // 2) Arabic shaping: beh+teh -> [beh-initial, teh-final], reversed for display.
    const char *bt = "\xD8\xA8\xD8\xAA";                           // بت (0628 062A)
    uint32_t bt_vis[] = {0xFE96, 0xFE91};                          // teh-final, beh-initial
    n = fp_to_visual((const uint8_t*)bt, strlen(bt), out);
    if (!eqcp(out, n, bt_vis, 2)) { printf("FAIL arabic shape\n"); fails++; }

    // 3) Lam-alef ligature: lam(0644)+alef(0627) isolated -> FEFB (one glyph).
    const char *la = "\xD9\x84\xD8\xA7";                           // لا
    uint32_t la_vis[] = {0xFEFB};
    n = fp_to_visual((const uint8_t*)la, strlen(la), out);
    if (!eqcp(out, n, la_vis, 1)) { printf("FAIL lam-alef\n"); fails++; }

    // 4) Arabic-first name with European digits: RTL base, digits stay upright on the left.
    //    "م12" -> meem isolated (FEE1) on the right, "12" on the left.
    const char *md = "\xD9\x85" "12";                             // م12
    n = fp_to_visual((const uint8_t*)md, strlen(md), out);
    uint32_t md_vis[] = {'1', '2', 0xFEE1};
    if (!eqcp(out, n, md_vis, 3)) { printf("FAIL arabic+digits\n"); fails++; }

    // 5) Trailing punctuation on a Hebrew-first name lands on the LEFT (RTL base).
    const char *shbang = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D!";      // שלום!
    uint32_t shbang_vis[] = {'!', 0x05DD, 0x05D5, 0x05DC, 0x05E9};
    n = fp_to_visual((const uint8_t*)shbang, strlen(shbang), out);
    if (!eqcp(out, n, shbang_vis, 5)) { printf("FAIL hebrew+trailing !\n"); fails++; }

    // 6) Digits after Hebrew (RTL base): "123" upright on the left, Hebrew reversed on the right.
    const char *shnum = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D" "123";  // שלום123
    uint32_t shnum_vis[] = {'1','2','3', 0x05DD, 0x05D5, 0x05DC, 0x05E9};
    n = fp_to_visual((const uint8_t*)shnum, strlen(shnum), out);
    if (!eqcp(out, n, shnum_vis, 7)) { printf("FAIL hebrew+digits\n"); fails++; }

    // 7) English-FIRST name keeps LTR base: Hebrew run reverses in place, stays on the right.
    const char *enfirst = "a \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";    // "a שלום"
    uint32_t enfirst_vis[] = {'a',' ', 0x05DD, 0x05D5, 0x05DC, 0x05E9};
    n = fp_to_visual((const uint8_t*)enfirst, strlen(enfirst), out);
    if (!eqcp(out, n, enfirst_vis, 6)) { printf("FAIL english-first base\n"); fails++; }

    if (fails) { printf("\n%d test(s) FAILED\n", fails); return 1; }
    printf("OK: all transform asserts passed\n\n");
    show("pure hebrew",   "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D");
    show("hebrew + ext",  "\xD7\x93\xD7\x95\xD7\x97.pdf");
    show("arabic word",   "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7");           // مرحبا
    show("arabic hello",  "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85"); // مرحبا بالعالم
    show("ascii only",    "budget_2026.xlsx");
    return 0;
}
#endif
