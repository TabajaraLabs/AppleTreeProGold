/*
** AppleTree Pro Gold  -  a tree-style file manager for ProDOS 8
** Apple II+ (64K + Videx-compatible 80-col card) / Apple IIe, IIc, IIgs
*/
#include <string.h>
#include <stdlib.h>
#include "plat.h"

#define VERSION "1.0"

#define MAXDIRS   144
#define MAXFILES  164
#define NONE      0xFF

struct dnode { char name[16]; u8 parent, child, sib, depth, flags; };
#define DF_COLL 1
#define DF_DEL  2

struct file  { char name[16]; u8 dir, type; u16 aux; u32 size;
               u16 mdate, mtime; u8 access, flags; };
#define FF_TAG 1
#define FF_SEL 2

#ifdef __CC65__
#define dirs ((struct dnode *)0x0800)     /* $0800-$13FF: free RAM under ProDOS */
#else
static struct dnode dirs[MAXDIRS];
#endif
static struct file files[MAXFILES];
u8  iobuf[1024];

static u8  ndirs, norder;
static u16 nfiles, nv;
#ifdef __CC65__
#define order ((u8 *)0x1400)              /* $1400-$148F */
#define vlist ((u16 *)0x1490)             /* $1490-$15FF  (<= 184 entries) */
#else
static u8  order[MAXDIRS];
static u16 vlist[MAXFILES];
#endif
static u8  overflow;

static char volname[16];
static char filespec[16] = "*.*";
static char dest[65];
static char pbuf[80], pbuf2[80];
static char tmp[82];
static struct fent ent;

static u8  mode;          /* 0 tree, 1 files, 2 files expanded */
static u8  showall;
static u8  tcur, ttop;
static u16 fcur, ftop;
static u8  sortkey;       /* 0 name 1 type 2 date 3 size 4 none */
static u8  fdetail;
static u8  redraw;
#define R_FRAME 1
#define R_TREE  2
#define R_FILES 4
#define R_STATS 8
#define R_MENU  16
#define R_HDR   32
#define R_ALL   63
static u8  vdirty, sdirty;

/* tagged-command and special key codes produced by getk() */
#define KT(c)   (0x80|(c))
#define K_PGUP  0xF0
#define K_PGDN  0xF1
#define K_HOME  0xF2
#define K_END   0xF3
#define K_SORT  0xF4
#define K_FMT   0xF5

/* ------------------------------------------------------------------ */
/* small helpers                                                       */

static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

static u8 upc(u8 c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static void strupr_(char *s) { while (*s) { *s = upc(*s); ++s; } }

static void putw(u8 x, u8 y, const char *s, u8 w)
{
    u8 i = 0;
    while (i < w && s[i]) { tmp[i] = s[i]; ++i; }
    while (i < w) tmp[i++] = ' ';
    tmp[w] = 0;
    scr_puts(x, y, tmp);
}

static void putinv(u8 x, u8 y, const char *s, u8 w)
{
    scr_inv = 1; putw(x, y, s, w); scr_inv = 0;
}

/* unsigned to decimal with thousands separators */
static char *fmtnum(char *out, u32 v)
{
    char t[14]; u8 n = 0, i = 0, d = 0;
    do { if (d == 3) { t[n++] = ','; d = 0; }
         t[n++] = '0' + (u8)(v % 10); v /= 10; ++d; } while (v);
    while (n) out[i++] = t[--n];
    out[i] = 0;
    return out;
}

/* right-aligned number ending at column x+w-1 */
static void putnum(u8 x, u8 y, u32 v, u8 w)
{
    char t[14]; u8 l;
    fmtnum(t, v); l = strlen(t);
    if (l > w) l = w;
    scr_fill(x, y, w - l, ' ');
    scr_puts(x + w - l, y, t);
}

static void fmtdate(char *o, u16 d)
{
    u8 day = d & 31, mon = (d >> 5) & 15, yr = (d >> 9) % 100;
    if (!d || !mon || mon > 12) { strcpy(o, "<no date>"); return; }
    o[0] = '0' + day / 10; o[1] = '0' + day % 10; o[2] = '-';
    memcpy(o + 3, months + (mon - 1) * 3, 3); o[6] = '-';
    o[7] = '0' + yr / 10; o[8] = '0' + yr % 10; o[9] = 0;
}

static void fmttime(char *o, u16 t)
{
    u8 h = t >> 8, m = t & 0xFF;
    if (h > 23 || m > 59) { strcpy(o, "     "); return; }
    o[0] = '0' + h / 10; o[1] = '0' + h % 10; o[2] = ':';
    o[3] = '0' + m / 10; o[4] = '0' + m % 10; o[5] = 0;
}

static const char hexd[] = "0123456789ABCDEF";
static const u8 tcodes[] = { 0x00,0x01,0x04,0x06,0x08,0x0F,0x19,0x1A,0x1B,0xB3,
                             0xC0,0xC1,0xE0,0xEF,0xF0,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF };
static const char tnames[] = "UNKBADTXTBINFOTDIRADBAWPASPS16PNTPICLBRPASCMDINTIVRBASVARRELSYS";

static void typestr(char *o, u8 t)
{
    u8 i;
    for (i = 0; i < sizeof(tcodes); ++i)
        if (tcodes[i] == t) { memcpy(o, tnames + i * 3, 3); o[3] = 0; return; }
    o[0] = '$'; o[1] = hexd[t >> 4]; o[2] = hexd[t & 15]; o[3] = 0;
}

static u8 wmatch(const char *p, const char *s)
{
    while (*p) {
        if (*p == '*') {
            ++p; if (!*p) return 1;
            while (*s) { if (wmatch(p, s)) return 1; ++s; }
            return 0;
        }
        if (!*s) return 0;
        if (*p != '?' && *p != *s) return 0;
        ++p; ++s;
    }
    return *s == 0;
}

static u8 specmatch(const char *n)
{
    if (filespec[0] == '*' && (!filespec[1] || !strcmp(filespec, "*.*"))) return 1;
    return wmatch(filespec, n);
}

static u8 validname(const char *n)
{
    u8 i = 0;
    if (*n < 'A' || *n > 'Z') return 0;
    while (n[i]) {
        u8 c = n[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.')) return 0;
        if (++i > 15) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* model                                                               */

static void dir_path(u8 d, char *out)
{
    u8 chain[40]; u8 n = 0;
    while (d != NONE && n < 40) { chain[n++] = d; d = dirs[d].parent; }
    out[0] = 0;
    while (n) { strcat(out, "/"); strcat(out, dirs[chain[--n]].name); }
}

static void file_path(u16 fi, char *out)
{
    dir_path(files[fi].dir, out);
    strcat(out, "/");
    strcat(out, files[fi].name);
}

static void link_dir(u8 d)
{
    u8 *lnk = &dirs[dirs[d].parent].child;
    while (*lnk != NONE && strcmp(dirs[*lnk].name, dirs[d].name) < 0)
        lnk = &dirs[*lnk].sib;
    dirs[d].sib = *lnk;
    *lnk = d;
}

static void unlink_dir(u8 d)
{
    u8 *lnk = &dirs[dirs[d].parent].child;
    while (*lnk != NONE && *lnk != d) lnk = &dirs[*lnk].sib;
    if (*lnk == d) *lnk = dirs[d].sib;
    dirs[d].sib = NONE;
}

static u8 add_dir(const char *name, u8 parent)
{
    u8 d;
    if (ndirs >= MAXDIRS) { overflow |= 1; return NONE; }
    d = ndirs++;
    strcpy(dirs[d].name, name);
    dirs[d].parent = parent;
    dirs[d].child = dirs[d].sib = NONE;
    dirs[d].flags = 0;
    dirs[d].depth = (parent == NONE) ? 0 : dirs[parent].depth + 1;
    if (parent != NONE) link_dir(d);
    return d;
}

static void set_file(u16 i, u8 dir, const struct fent *e)
{
    struct file *f = &files[i];
    strcpy(f->name, e->name);
    f->dir = dir; f->type = e->type; f->aux = e->aux; f->size = e->size;
    f->mdate = e->mdate; f->mtime = e->mtime; f->access = e->access;
}

static void add_file(u8 dir, const struct fent *e)
{
    if (nfiles >= MAXFILES) { overflow |= 2; return; }
    files[nfiles].flags = 0;
    set_file(nfiles++, dir, e);
}

static void remove_file(u16 i)
{
    memmove(&files[i], &files[i + 1], (nfiles - i - 1) * sizeof(struct file));
    --nfiles;
}

/* find a directory node by full path ("/VOL/A/B") */
static u8 find_dir(const char *p)
{
    char comp[16]; u8 d = 0, n, c;
    if (!ndirs || *p != '/') return NONE;
    ++p; n = 0;
    while (*p && *p != '/' && n < 15) comp[n++] = *p++;
    comp[n] = 0;
    if (strcmp(comp, dirs[0].name)) return NONE;
    while (*p == '/') {
        ++p; n = 0;
        while (*p && *p != '/' && n < 15) comp[n++] = *p++;
        comp[n] = 0;
        if (!n) break;
        c = dirs[d].child;
        while (c != NONE && strcmp(dirs[c].name, comp)) c = dirs[c].sib;
        if (c == NONE) return NONE;
        d = c;
    }
    return d;
}

static void build_order(void)
{
    u8 n = 0, d = 0;
    while (d != NONE) {
        order[n++] = d;
        if (dirs[d].child != NONE && !(dirs[d].flags & DF_COLL)) d = dirs[d].child;
        else {
            while (d != NONE && dirs[d].sib == NONE) d = dirs[d].parent;
            if (d != NONE) d = dirs[d].sib;
        }
    }
    norder = n;
    if (tcur >= norder) tcur = norder - 1;
}

static u8 curdir(void) { return order[tcur]; }

static void select_dir(u8 d)
{
    u8 i;
    for (i = 0; i < norder; ++i) if (order[i] == d) { tcur = i; return; }
}

static int fcmp(const void *a, const void *b)
{
    const struct file *x = &files[*(const u16 *)a], *y = &files[*(const u16 *)b];
    switch (sortkey) {
    case 1: if (x->type != y->type) return x->type < y->type ? -1 : 1; break;
    case 2: if (x->mdate != y->mdate) return x->mdate < y->mdate ? -1 : 1;
            if (x->mtime != y->mtime) return x->mtime < y->mtime ? -1 : 1;
            break;
    case 3: if (x->size != y->size) return x->size < y->size ? -1 : 1; break;
    case 4: return *(const u16 *)a < *(const u16 *)b ? -1 : 1;
    }
    return strcmp(x->name, y->name);
}

static void build_vlist(void)
{
    u16 i; u8 cd = curdir();
    nv = 0;
    for (i = 0; i < nfiles; ++i)
        if ((showall || files[i].dir == cd) && specmatch(files[i].name))
            vlist[nv++] = i;
    if (nv > 1) qsort(vlist, nv, sizeof(u16), fcmp);
    if (fcur >= nv) fcur = nv ? nv - 1 : 0;
    vdirty = 0;
}

/* statistics */
static u32 st_tb, st_mb, st_gb, st_cb;
static u16 st_tn, st_mn, st_gn, st_cn;
static u16 vol_total, vol_used;

static void calc_stats(void)
{
    u16 i; u8 cd = curdir(); struct file *f;
    st_tb = st_mb = st_gb = st_cb = 0; st_tn = st_mn = st_gn = st_cn = 0;
    for (i = 0, f = files; i < nfiles; ++i, ++f) {
        ++st_tn; st_tb += f->size;
        if (specmatch(f->name)) {
            ++st_mn; st_mb += f->size;
            if (f->dir == cd) { ++st_cn; st_cb += f->size; }
        }
        if (f->flags & FF_TAG) { ++st_gn; st_gb += f->size; }
    }
    sdirty = 0;
}

/* ------------------------------------------------------------------ */
/* screen layout                                                       */
/* col 0 | tree/files 1..57 | 58 | stats 59..78 | 79                    */
#define LX 1
#define LW 57
#define SX 59
#define SW 20
#define TY 2
#define TH 11
#define FY 14
#define FH 6
#define EY 2
#define EH 18

/* G_* glyphs (plat.h) become MouseText lines when available, else - | + */
static void hline(u8 y, u8 h, u8 c, u8 j)
{
    scr_fill(0, y, 1, c);
    scr_fill(1, y, 57, h);
    scr_fill(58, y, 1, j);
    scr_fill(59, y, 20, h);
    scr_fill(79, y, 1, c);
}

static void vbars(u8 y0, u8 y1)
{
    u8 y;
    for (y = y0; y <= y1; ++y) {
        scr_fill(0, y, 1, G_VL); scr_fill(58, y, 1, G_VR); scr_fill(79, y, 1, G_VR);
    }
}

static void draw_frame(void)
{
    hline(1, G_HT, G_CR, G_JT);
    scr_puts(3, 1, " AppleTree Pro Gold ");
    vbars(2, 19);
    if (mode != 2) {
        scr_fill(0, 13, 1, G_JL);
        scr_fill(1, 13, 57, G_HM);
        scr_fill(58, 13, 1, G_JR);
    }
    hline(20, G_HB, G_CR, G_JB);
    scr_fill(58, 4, 1, G_JM); scr_fill(SX, 4, SW, G_HM);
    scr_fill(58, 7, 1, G_JM); scr_fill(SX, 7, SW, G_HM);
}

static void draw_hdr(void)
{
    u16 d, t; char s[12];
    scr_puts(0, 0, "Path: ");
    if (showall && mode && nv) dir_path(files[vlist[fcur]].dir, pbuf);
    else dir_path(curdir(), pbuf);
    putw(6, 0, pbuf, 57);
    clk_get(&d, &t);
    if (d) {
        fmtdate(s, d); scr_puts(63, 0, s);
        scr_puts(72, 0, "  "); fmttime(s, t); scr_puts(74, 0, s);
        scr_fill(79, 0, 1, ' ');
    } else scr_fill(63, 0, 17, ' ');
}

static void draw_stats(void)
{
    char s[20]; u8 y = 2; u32 freeb;
    if (sdirty) calc_stats();
    scr_puts(SX, y, "FILE "); putw(SX + 5, y++, filespec, 15);
    scr_puts(SX, y, "DISK "); s[0] = '/'; strcpy(s + 1, volname); putw(SX + 5, y++, s, 15);
    ++y;
    scr_puts(SX, y, " Available         ");
    scr_puts(SX, ++y, "  Bytes");
    freeb = (u32)(vol_total - vol_used) * 512;
    putnum(SX + 7, y, freeb, 13);
    y = 8;
    putw(SX, y++, "DISK Statistics", SW);
    scr_puts(SX, y, " Total Files   "); putnum(SX + 15, y++, st_tn, 5);
    scr_puts(SX, y, "  Bytes"); putnum(SX + 7, y++, st_tb, 13);
    scr_puts(SX, y, " Matching Files"); putnum(SX + 15, y++, st_mn, 5);
    scr_puts(SX, y, "  Bytes"); putnum(SX + 7, y++, st_mb, 13);
    scr_puts(SX, y, " Tagged Files  "); putnum(SX + 15, y++, st_gn, 5);
    scr_puts(SX, y, "  Bytes"); putnum(SX + 7, y++, st_gb, 13);
    putw(SX, y++, showall ? " Showall" : " Current Directory", SW);
    s[0] = ' '; s[1] = ' ';
    if (curdir() == 0) { s[2] = '/'; strcpy(s + 3, volname); } else strcpy(s + 2, dirs[curdir()].name);
    putw(SX, y++, s, SW);
    scr_puts(SX, y, "  Bytes"); putnum(SX + 7, y++, showall ? st_mb : st_cb, 13);
}

static void draw_tree(void)
{
    u8 r, i, d, a, k, dd, len, nl, y;
    if (tcur < ttop) ttop = tcur;
    if (tcur >= ttop + TH) ttop = tcur - TH + 1;
    for (r = 0; r < TH; ++r) {
        y = TY + r; i = ttop + r;
        if (i >= norder) { scr_fill(LX, y, LW, ' '); continue; }
        d = order[i]; dd = dirs[d].depth;
        len = dd * 3;
        if (dd) {
            k = (dd - 1) * 3;
            tmp[k] = dirs[d].sib != NONE ? G_TT : G_TL; tmp[k + 1] = tmp[k + 2] = G_HM;
            a = dirs[d].parent;
            while (k) { k -= 3; tmp[k] = dirs[a].sib != NONE ? G_TV : ' '; tmp[k + 1] = tmp[k + 2] = ' '; a = dirs[a].parent; }
        }
        if (len > LW - 4) len = LW - 4;
        tmp[len] = 0;
        scr_puts(LX, y, tmp);
        if (d == 0) { pbuf[0] = '/'; strcpy(pbuf + 1, volname); }
        else strcpy(pbuf, dirs[d].name);
        if ((dirs[d].flags & DF_COLL) && dirs[d].child != NONE) strcat(pbuf, " +");
        nl = strlen(pbuf);
        if (len + nl > LW) nl = LW - len;
        pbuf[nl] = 0;
        scr_inv = (i == tcur);
        scr_puts(LX + len, y, pbuf);
        scr_inv = 0;
        scr_fill(LX + len + nl, y, LW - len - nl, ' ');
    }
}

static void file_entry(char *o, u16 fi, u8 detail)
{
    struct file *f = &files[fi]; u8 n;
    memset(o, ' ', 57);
    o[0] = (f->flags & FF_TAG) ? '+' : ' ';
    n = strlen(f->name); memcpy(o + 1, f->name, n);
    typestr(o + 16, f->type); o[19] = ' ';
    if (detail) {
        char t[14]; u8 l;
        fmtnum(t, f->size); l = strlen(t); memcpy(o + 31 - l, t, l);
        fmtdate(o + 32, f->mdate); o[41] = ' ';
        fmttime(o + 42, f->mtime); o[47] = ' ';
        o[48] = (f->access & 0xC2) == 0xC2 ? ' ' : 'L';
        o[49] = 0;
    } else o[19] = 0;
}

static void draw_files(void)
{
    u8 y0, h, cols, per, r, c, x, y; u16 idx;
    y0 = (mode == 2) ? EY : FY; h = (mode == 2) ? EH : FH;
    cols = fdetail ? 1 : 3; per = h * cols;
    if (vdirty) build_vlist();
    if (fcur < ftop) ftop = fcur - fcur % cols;
    if (fcur >= ftop + per) ftop = (fcur / cols - h + 1) * cols;
    for (r = 0; r < h; ++r) {
        y = y0 + r;
        if (!nv) {
            scr_fill(LX, y, LW, ' ');
            if (r == h / 2) scr_puts(LX + 18, y, "No files!");
            continue;
        }
        for (c = 0; c < cols; ++c) {
            idx = ftop + r * cols + c;
            x = LX + c * 19;
            if (idx >= nv) { scr_fill(x, y, fdetail ? LW : 19, ' '); continue; }
            file_entry(tmp, vlist[idx], fdetail);
            scr_inv = (mode && idx == fcur);
            scr_puts(x, y, tmp);
            scr_inv = 0;
            if (fdetail) scr_fill(x + 49, y, LW - 49, ' ');
        }
    }
}

/* menu strings: '~' marks a highlighted key letter */
static void menu_line(u8 y, const char *s)
{
    u8 x = 0; char c[2]; c[1] = 0;
    while (*s && x < 80) {
        if (*s == '~') { ++s; scr_inv = 1; }
        c[0] = *s++;
        scr_puts(x++, y, c);
        scr_inv = 0;
    }
    scr_fill(x, y, 80 - x, ' ');
}

static void draw_menu(void)
{
    if (mode == 0) {
        menu_line(21, "DIR      ~Avail ~Delete ~Filespec ~Global ~Invert ~Log ~Makedir ~Print ~Rename");
        menu_line(22, "COMMANDS ~Showall ~Tag ~Untag  ~-~+ collapse/expand  ^T/^X tag/untag disk  ~Quit");
        menu_line(23, " ~R~E~T file window  ~?help  ^S sort  ^A/OA+key = all tagged                   ");
    } else {
        menu_line(21, "FILE     ~Attributes ~Copy ~Delete ~Filespec ~Invert ~Move ~Print ~Rename ~Tag");
        menu_line(22, "COMMANDS ~Untag ~View e~Xecute  ^A/OA+key = tagged  ^T/^X tag/untag all  ~Quit");
        menu_line(23, " ~R~E~T expand  ~E~S~C tree  ~?help  ^S sort  ^F format                       ");
    }
}

static void do_redraw(void)
{
    if (redraw & R_FRAME) draw_frame();
    if (redraw & R_HDR) draw_hdr();
    if ((redraw & R_TREE) && mode != 2) draw_tree();
    if (redraw & R_FILES) draw_files();
    if (redraw & R_STATS) draw_stats();
    if (redraw & R_MENU) draw_menu();
    redraw = 0;
}

/* ------------------------------------------------------------------ */
/* keyboard / prompts                                                  */

static u8 getk(void)
{
    u8 k = key_get();
    if (key_oa) {
        switch (k) {
        case K_UP: return K_PGUP;
        case K_DOWN: return K_PGDN;
        case K_LEFT: return K_HOME;
        case K_RIGHT: return K_END;
        }
        k = upc(k);
        if (k >= 'A' && k <= 'Z') return KT(k);
        return k;
    }
    switch (k) {
    case 0x01: k = upc(key_get()); return (k >= 'A' && k <= 'Z') ? KT(k) : k;
    case 0x03: return KT('C');
    case 0x04: return KT('D');
    case 0x0F: return KT('M');
    case 0x12: return KT('R');
    case 0x14: return KT('T');
    case 0x18: return KT('U');
    case 0x10: return KT('P');
    case 0x13: return K_SORT;
    case 0x06: return K_FMT;
    case 0x17: return K_PGUP;
    case 0x1A: return K_PGDN;
    case 0x11: return K_HOME;
    case 0x05: return K_END;
    }
    return upc(k);
}

static void clr_bottom(void)
{
    scr_fill(0, 21, 80, ' '); scr_fill(0, 22, 80, ' '); scr_fill(0, 23, 80, ' ');
    redraw |= R_MENU;
}

static void status(const char *s)
{
    clr_bottom();
    putw(1, 22, s, 78);
}

static u8 input(const char *prompt, char *buf, u8 max)
{
    u8 x0, l, k, w;
    clr_bottom();
    scr_puts(1, 22, prompt);
    scr_puts(1, 23, "RET accept   ESC cancel   <- erase");
    x0 = 1 + strlen(prompt); w = 78 - x0;
    for (;;) {
        l = strlen(buf);
        putw(x0, 22, l >= w ? buf + l - w + 1 : buf, w);
        scr_inv = 1; scr_fill(x0 + (l >= w ? w - 1 : l), 22, 1, ' '); scr_inv = 0;
        k = key_get();
        if (k == K_RET) return 1;
        if (k == K_ESC) return 0;
        if (k == K_LEFT || k == K_DEL) { if (l) buf[l - 1] = 0; }
        else if (k >= 0x20 && k < 0x7F && l < max) { buf[l] = upc(k); buf[l + 1] = 0; }
    }
}

/* returns 'Y', 'N', 'A' (if allowed) or 0 for ESC */
static u8 ask(const char *q, const char *keys)
{
    u8 k;
    clr_bottom();
    putw(1, 22, q, 78);
    for (;;) {
        k = upc(key_get());
        if (k == K_ESC) return 0;
        if (strchr(keys, k)) return k;
    }
}

static u8 confirm(const char *q)
{
    strcpy(pbuf2, q); strcat(pbuf2, "  (Y/N)");
    return ask(pbuf2, "YN") == 'Y';
}

static const u8 ecode[] = { 0x27,0x28,0x2B,0x2E,0x40,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,
                            0x4B,0x4E,0x50,0x52,0x53,0x56,0xF0,0xF1,0xF2 };
static const char *const emsg[] = {
    "I/O error", "No device connected", "Disk write protected", "Disk switched",
    "Invalid pathname", "Path not found", "Volume not found", "File not found",
    "Duplicate filename", "Disk full", "Directory full", "Incompatible format",
    "Unsupported storage type", "File is locked", "File is open", "Not a ProDOS disk",
    "Invalid parameter", "Bad buffer address", "Invalid name", "Too many files",
    "Out of memory" };

static void error(u8 code, const char *what)
{
    u8 i; char h[4];
    strcpy(pbuf2, "Error: ");
    for (i = 0; i < sizeof(ecode); ++i) if (ecode[i] == code) break;
    if (i < sizeof(ecode)) strcat(pbuf2, emsg[i]);
    else { h[0] = '$'; h[1] = hexd[code >> 4]; h[2] = hexd[code & 15]; h[3] = 0; strcat(pbuf2, h); }
    if (what) { strcat(pbuf2, " - "); strcat(pbuf2, what); }
    clr_bottom();
    putinv(1, 22, pbuf2, strlen(pbuf2) > 78 ? 78 : strlen(pbuf2));
    scr_puts(1, 23, "Press any key");
    key_get();
}

static void message(const char *m)
{
    clr_bottom();
    putw(1, 22, m, 78);
    scr_puts(1, 23, "Press any key");
    key_get();
}

/* popup box */
static void box(u8 x, u8 y, u8 w, u8 h)
{
    u8 i;
    scr_fill(x, y, 1, G_CR); scr_fill(x + 1, y, w - 2, G_HT); scr_fill(x + w - 1, y, 1, G_CR);
    for (i = 1; i < h - 1; ++i) {
        scr_fill(x, y + i, 1, G_VL); scr_fill(x + 1, y + i, w - 2, ' '); scr_fill(x + w - 1, y + i, 1, G_VR);
    }
    scr_fill(x, y + h - 1, 1, G_CR); scr_fill(x + 1, y + h - 1, w - 2, G_HB); scr_fill(x + w - 1, y + h - 1, 1, G_CR);
}

/* ------------------------------------------------------------------ */
/* logging                                                             */

/* ------------------------------------------------------------------ */
/* start-up screen and logging progress                                */

static void box(u8 x, u8 y, u8 w, u8 h);

/* 5x5 block letters (bit 4 = leftmost pixel), in the style of the classic tree file-manager logos */
static const char logo_l[] = "APLETR";
static const u8 logo_f[6][5] = {
    { 0x0E, 0x11, 0x1F, 0x11, 0x11 },   /* A */
    { 0x1E, 0x11, 0x1E, 0x10, 0x10 },   /* P */
    { 0x10, 0x10, 0x10, 0x10, 0x1F },   /* L */
    { 0x1F, 0x10, 0x1E, 0x10, 0x1F },   /* E */
    { 0x1F, 0x04, 0x04, 0x04, 0x04 },   /* T */
    { 0x1E, 0x11, 0x1E, 0x12, 0x11 } }; /* R */
static u8 splash_on;            /* 1 while the start-up screen shows the progress */
#define PBX 15                  /* progress bar: column, width */
#define PBW 50

/* centre text in the left pane (columns 1..57) */
static void pane_centre(u8 y, const char *t)
{
    scr_puts(1 + (57 - strlen(t)) / 2, y, t);
}

/* big word: 5x5 letters, each pixel = 2 cells of inverse '_' (the cell is lit except its bottom
   dot row: bars solid across, cut horizontally between rows); 1 cell between letters */
static void logo_word(u8 y, const char *w)
{
    u8 i, r, c, x, b, n = strlen(w);
    const u8 *g;
    x = 1 + (57 - (n * 11 - 1)) / 2;
    for (i = 0; i < n; ++i, x += 11) {
        g = logo_f[strchr(logo_l, w[i]) - logo_l];
        for (r = 0; r < 5; ++r)
            for (c = 0, b = 0x10; c < 5; ++c, b >>= 1) {
                scr_inv = (g[r] & b) != 0;
                scr_fill(x + c * 2, y + r, 2, scr_inv ? '_' : ' ');
            }
    }
    scr_inv = 0;
}

/* start-up screen: the normal frame, logo in the tree pane, stats filled in after logging */
static void splash(void)
{
    static const char freel[] =
        "FREE - Gratuito - Gratis - Gratuit - Kostenlos - Darmowy - Ilmainen - Ingyenes";
    u8 i;
    for (i = 0; i < 24; ++i) scr_fill(0, i, 80, ' ');
    mode = 2;                               /* frame without the tree/file divider */
    draw_frame();
    logo_word(3, "APPLE");
    logo_word(9, "TREE");
    pane_centre(15, "P  R  O     G  O  L  D");
    pane_centre(17, "(c) Alexandre Souza - Tabajara Labs - Nov. 2026");
    pane_centre(18, "AppleTree Pro Gold  -  Version 1.0");
    scr_puts((80 - (sizeof freel - 1)) / 2, 23, freel);
    splash_on = 1;
}

/* y = row of the bar; pct 0..100 */
static void progress(u8 y, u8 pct)
{
    u8 n = pct / 2; char s[6];
    scr_inv = 1; scr_fill(PBX, y, n, ' '); scr_inv = 0;
    scr_fill(PBX + n, y, PBW - n, G_PE);
    s[0] = pct >= 100 ? '1' : ' ';
    s[1] = pct >= 10 ? '0' + (pct / 10) % 10 : ' ';
    s[2] = '0' + pct % 10; s[3] = '%'; s[4] = 0;
    scr_puts(PBX + PBW + 2, y, s);
}

static void log_progress(const char *path, u16 nf, u32 done)
{
    u8 pct = vol_used ? (done >= vol_used ? 100 : (u8)(done * 100 / vol_used)) : 0;
    if (splash_on) {
        scr_puts(1, 21, "Logging "); putw(9, 21, path, 70);
        putnum(1, 22, nf, 6); scr_puts(8, 22, "files");
        progress(22, pct);
    } else {
        putw(17, 22, path, 62);
        putnum(1, 23, nf, 6); scr_puts(8, 23, "files");
        progress(23, pct);
    }
}

static void log_volume(const char *path)
{
    u8 i, n, e, err = 0, errdir = 0;
    u32 done = 6;                       /* volume directory + bitmap, roughly */
    const char *p = path + 1;
    n = 0;
    while (*p && *p != '/' && n < 15) volname[n++] = *p++;
    volname[n] = 0;
    ndirs = 0; nfiles = 0; overflow = 0;
    tcur = ttop = 0; fcur = ftop = 0; showall = 0; mode = 0;
    add_dir(volname, NONE);
    vol_total = vol_used = 0;
    fs_volinfo(volname, &vol_total, &vol_used);
    if (!splash_on) status("Logging disk...");
    for (i = 0; i < ndirs; ++i) {
        dir_path(i, pbuf);
        log_progress(pbuf, nfiles, done);
        if ((e = fs_opendir(pbuf)) == 0) {
            while (fs_readdir(&ent)) {
                done += ent.blocks;
                if (ent.type == T_DIR) add_dir(ent.name, i);
                else add_file(i, &ent);
            }
            fs_closedir();
        } else if (!err) { err = e; errdir = i; }
    }
    log_progress(pbuf, nfiles, vol_used);
    build_order();
    i = find_dir(path);
    if (i != NONE) select_dir(i);
    vdirty = sdirty = 1;
    redraw = R_ALL;
    if (err) { dir_path(errdir, pbuf); error(err, pbuf); }
    if (overflow) message(overflow & 2 ? "Warning: too many files - not all were logged"
                                       : "Warning: too many directories - not all were logged");
}

static void do_log(void)
{
#ifdef __CC65__
#define vols ((char (*)[16])0x1600)       /* $1600-$16DF */
#else
    static char vols[14][16];
#endif
    u8 n, i, sel = 0, k;
    n = fs_vols(vols, 14);
    if (!n) { message("No volumes online"); return; }
    box(20, 4, 30, n + 4);
    scr_puts(22, 5, "Log which volume?");
    for (;;) {
        for (i = 0; i < n; ++i) {
            pbuf[0] = '/'; strcpy(pbuf + 1, vols[i]);
            scr_inv = (i == sel); putw(23, 7 + i, pbuf, 24); scr_inv = 0;
        }
        k = getk();
        if (k == K_UP && sel) --sel;
        else if (k == K_DOWN && sel + 1 < n) ++sel;
        else if (k == K_ESC) { redraw = R_ALL; return; }
        else if (k == K_RET) break;
    }
    pbuf[0] = '/'; strcpy(pbuf + 1, vols[sel]);
    strcpy(pbuf2, pbuf);
    log_volume(pbuf2);
}

/* ------------------------------------------------------------------ */
/* path entry                                                          */

static void normpath(char *p)
{
    u8 l;
    strupr_(p);
    if (p[0] != '/') {
        dir_path(curdir(), pbuf2);
        if (p[0]) { strcat(pbuf2, "/"); strcat(pbuf2, p); }
        strcpy(p, pbuf2);
    }
    l = strlen(p);
    while (l > 1 && p[l - 1] == '/') p[--l] = 0;
}

/* ------------------------------------------------------------------ */
/* commands                                                            */

static u16 mark_targets(u8 tagged)
{
    u16 i, n = 0;
    for (i = 0; i < nfiles; ++i) files[i].flags &= ~FF_SEL;
    if (!nv) return 0;
    if (!tagged) { files[vlist[fcur]].flags |= FF_SEL; return 1; }
    for (i = 0; i < nv; ++i)
        if (files[vlist[i]].flags & FF_TAG) { files[vlist[i]].flags |= FF_SEL; ++n; }
    return n;
}

static void to_fent(u16 i, struct fent *e)
{
    struct file *f = &files[i];
    strcpy(e->name, f->name); e->type = f->type; e->aux = f->aux; e->size = f->size;
    e->mdate = f->mdate; e->mtime = f->mtime; e->access = f->access;
}

/* register a newly written file in the model */
static void model_put(const char *dpath, const struct fent *e)
{
    u8 d = find_dir(dpath); u16 i;
    if (d == NONE) return;
    for (i = 0; i < nfiles; ++i)
        if (files[i].dir == d && !strcmp(files[i].name, e->name)) { set_file(i, d, e); return; }
    add_file(d, e);
}

static u8 user_abort(void)
{
    if (key_avail() && key_get() == K_ESC) return confirm("Abort operation?");
    return 0;
}

static void do_copy(u8 tagged, u8 move)
{
    static char asname[16];
    u16 i, n; u8 all = 0, err, k;
    struct fent e;
    n = mark_targets(tagged);
    if (!n) { if (tagged) message("No tagged files"); return; }
    if (!tagged) {
        strcpy(asname, files[vlist[fcur]].name);
        if (!input(move ? "Move file as: " : "Copy file as: ", asname, 15)) return;
        if (!validname(asname)) { error(0xF0, asname); return; }
    }
    if (!dest[0]) dir_path(curdir(), dest);
    if (!input(move ? "Move to: " : "Copy to: ", dest, 64)) return;
    normpath(dest);
    if (fs_getinfo(dest, &e) || e.type != T_DIR) {
        if (!(dest[0] == '/' && !strchr(dest + 1, '/'))) { error(0x44, dest); return; }
    }
    for (i = nfiles; i-- > 0;) {
        if (!(files[i].flags & FF_SEL)) continue;
        files[i].flags &= ~FF_SEL;
        file_path(i, pbuf);
        strcpy(pbuf2, dest); strcat(pbuf2, "/");
        strcat(pbuf2, tagged ? files[i].name : asname);
        if (!strcmp(pbuf, pbuf2)) { message("A file cannot be copied onto itself"); continue; }
        if (fs_getinfo(pbuf2, &e) == 0) {
            if (e.type == T_DIR) { error(0x47, pbuf2); continue; }
            if (!all) {
                strcpy(tmp, "Replace "); strcat(tmp, pbuf2); strcat(tmp, " ?  (Yes/No/All)");
                k = ask(tmp, "YNA");
                if (!k) break;
                if (k == 'N') continue;
                if (k == 'A') all = 1;
            }
            if ((err = fs_delete(pbuf2)) != 0) { error(err, pbuf2); continue; }
            file_path(i, pbuf);
            strcpy(pbuf2, dest); strcat(pbuf2, "/");
            strcat(pbuf2, tagged ? files[i].name : asname);
        }
        status(move ? "Moving " : "Copying ");
        scr_puts(move ? 8 : 9, 22, pbuf);
        scr_puts(1, 23, "to "); scr_puts(4, 23, pbuf2);
        to_fent(i, &e);
        strcpy(e.name, tagged ? files[i].name : asname);
        err = fs_copy(pbuf, pbuf2, &e);
        if (err) {
            error(err, pbuf2);
            if (!confirm("Continue?")) break;
            continue;
        }
        model_put(dest, &e);
        if (move) {
            if ((err = fs_delete(pbuf)) != 0) error(err, pbuf);
            else remove_file(i);
        } else files[i].flags &= ~FF_TAG;
        if (user_abort()) break;
    }
    vdirty = sdirty = 1;
    redraw = R_ALL;
    vol_used = 0; fs_volinfo(volname, &vol_total, &vol_used);
}

static void do_delete(u8 tagged)
{
    u16 i, n; u8 each = 0, err, k;
    n = mark_targets(tagged);
    if (!n) { if (tagged) message("No tagged files"); return; }
    if (tagged) {
        if (!confirm("Delete ALL tagged files?")) return;
        k = ask("Confirm each deletion?  (Y/N)", "YN");
        if (!k) return;
        each = (k == 'Y');
    } else {
        strcpy(tmp, "Delete "); strcat(tmp, files[vlist[fcur]].name); strcat(tmp, " ?");
        if (!confirm(tmp)) return;
    }
    for (i = nfiles; i-- > 0;) {
        if (!(files[i].flags & FF_SEL)) continue;
        files[i].flags &= ~FF_SEL;
        file_path(i, pbuf);
        if (each) {
            strcpy(tmp, "Delete "); strcat(tmp, pbuf); strcat(tmp, " ?  (Y/N)");
            k = ask(tmp, "YN");
            if (!k) break;
            if (k == 'N') continue;
        }
        status("Deleting "); scr_puts(10, 22, pbuf);
        if ((err = fs_delete(pbuf)) != 0) error(err, pbuf);
        else remove_file(i);
        if (user_abort()) break;
    }
    vdirty = sdirty = 1;
    redraw = R_ALL;
    fs_volinfo(volname, &vol_total, &vol_used);
}

static void do_rename_file(u8 tagged)
{
    u16 fi; char nn[16]; u8 err;
    if (!nv) return;
    if (tagged) { message("Rename tagged: use R on each file"); return; }
    fi = vlist[fcur];
    strcpy(nn, files[fi].name);
    if (!input("Rename file as: ", nn, 15)) return;
    if (!strcmp(nn, files[fi].name)) return;
    if (!validname(nn)) { error(0xF0, nn); return; }
    file_path(fi, pbuf);
    dir_path(files[fi].dir, pbuf2); strcat(pbuf2, "/"); strcat(pbuf2, nn);
    if ((err = fs_rename(pbuf, pbuf2)) != 0) { error(err, pbuf2); return; }
    strcpy(files[fi].name, nn);
    vdirty = 1; redraw = R_ALL;
}

static void do_attrib(u8 tagged)
{
    u16 i, n; u8 k, acc, err;
    n = mark_targets(tagged);
    if (!n) { if (tagged) message("No tagged files"); return; }
    k = ask("Attributes:  L = Lock (read only)   U = Unlock", "LU");
    if (!k) { for (i = 0; i < nfiles; ++i) files[i].flags &= ~FF_SEL; return; }
    for (i = 0; i < nfiles; ++i) {
        if (!(files[i].flags & FF_SEL)) continue;
        files[i].flags &= ~FF_SEL;
        acc = (k == 'L') ? ((files[i].access & 0x3C) | 0x01) : ((files[i].access & 0x3C) | 0xC3);
        file_path(i, pbuf);
        if ((err = fs_setaccess(pbuf, acc)) != 0) { error(err, pbuf); break; }
        files[i].access = acc;
    }
    redraw = R_ALL;
}

static void tag_range(u8 how, u8 alldisk)  /* how: 0 untag 1 tag 2 invert */
{
    u16 i; u8 cd = curdir(); struct file *f;
    for (i = 0, f = files; i < nfiles; ++i, ++f) {
        if (!specmatch(f->name)) continue;
        if (!alldisk && !(mode ? 1 : f->dir == cd)) continue;
        if (mode && !alldisk && !showall && f->dir != cd) continue;
        if (how == 2) f->flags ^= FF_TAG;
        else if (how) f->flags |= FF_TAG; else f->flags &= ~FF_TAG;
    }
    sdirty = 1; redraw |= R_FILES | R_STATS;
}

static void do_filespec(void)
{
    char s[16];
    strcpy(s, filespec);
    if (!input("Filespec (wildcards * ?): ", s, 15)) return;
    if (!s[0]) strcpy(s, "*.*");
    strcpy(filespec, s);
    vdirty = sdirty = 1; fcur = ftop = 0; redraw = R_ALL;
}

static void do_sort(void)
{
    u8 k = ask("Sort by:  N=Name  T=Type  D=Date  S=Size  U=Unsorted", "NTDSU");
    if (k) {
        sortkey = (u8)(strchr("NTDSU", k) - "NTDSU");
        vdirty = 1; fcur = ftop = 0;
    }
    redraw = R_ALL;
}

static void do_avail(void)
{
    u16 t = 0, u = 0; char s[16];
    fs_volinfo(volname, &t, &u);
    vol_total = t; vol_used = u;
    box(16, 6, 44, 9);
    scr_puts(18, 7, "Volume:");  s[0] = '/'; strcpy(s + 1, volname); scr_puts(32, 7, s);
    scr_puts(18, 9, "Total blocks:"); putnum(32, 9, t, 8); putnum(42, 9, (u32)t * 512, 14);
    scr_puts(18, 10, "Used blocks:");  putnum(32, 10, u, 8); putnum(42, 10, (u32)u * 512, 14);
    scr_puts(18, 11, "Free blocks:");  putnum(32, 11, t - u, 8); putnum(42, 11, (u32)(t - u) * 512, 14);
    scr_puts(45, 8, "Bytes");
    scr_puts(18, 13, "Press any key");
    key_get();
    redraw = R_ALL;
}

static void do_makedir(void)
{
    char nn[16]; u8 err, d;
    nn[0] = 0;
    if (!input("Make directory: ", nn, 15) || !nn[0]) return;
    if (!validname(nn)) { error(0xF0, nn); return; }
    dir_path(curdir(), pbuf); strcat(pbuf, "/"); strcat(pbuf, nn);
    if ((err = fs_mkdir(pbuf)) != 0) { error(err, pbuf); return; }
    d = curdir();
    dirs[d].flags &= ~DF_COLL;
    d = add_dir(nn, d);
    build_order();
    fs_volinfo(volname, &vol_total, &vol_used);
    redraw = R_ALL;
}

static void do_deldir(void)
{
    u8 d = curdir(), err; u16 i;
    if (d == 0) { message("The root directory cannot be deleted"); return; }
    if (dirs[d].child != NONE) { message("Directory is not empty (has subdirectories)"); return; }
    for (i = 0; i < nfiles; ++i) if (files[i].dir == d) { message("Directory is not empty - delete its files first"); return; }
    dir_path(d, pbuf);
    strcpy(tmp, "Delete directory "); strcat(tmp, pbuf); strcat(tmp, " ?");
    if (!confirm(tmp)) return;
    if ((err = fs_delete(pbuf)) != 0) { error(err, pbuf); return; }
    unlink_dir(d);
    dirs[d].flags |= DF_DEL;
    fs_volinfo(volname, &vol_total, &vol_used);     /* its key block is free again */
    if (tcur) --tcur;
    build_order();
    vdirty = sdirty = 1; redraw = R_ALL;
}

static void do_rendir(void)
{
    u8 d = curdir(), err; char nn[16];
    strcpy(nn, dirs[d].name);
    if (!input(d ? "Rename directory as: " : "Rename volume as: ", nn, 15)) return;
    if (!strcmp(nn, dirs[d].name)) return;
    if (!validname(nn)) { error(0xF0, nn); return; }
    dir_path(d, pbuf);
    if (d) { dir_path(dirs[d].parent, pbuf2); strcat(pbuf2, "/"); }
    else strcpy(pbuf2, "/");
    strcat(pbuf2, nn);
    if ((err = fs_rename(pbuf, pbuf2)) != 0) { error(err, pbuf2); return; }
    if (d) { unlink_dir(d); strcpy(dirs[d].name, nn); link_dir(d); }
    else { strcpy(dirs[0].name, nn); strcpy(volname, nn); }
    build_order(); select_dir(d);
    redraw = R_ALL;
}

static void collapse(u8 how)  /* 0 collapse, 1 expand, 2 expand all */
{
    u8 i, d = curdir();
    if (how == 2) { for (i = 0; i < ndirs; ++i) dirs[i].flags &= ~DF_COLL; }
    else if (how) dirs[d].flags &= ~DF_COLL;
    else dirs[d].flags |= DF_COLL;
    build_order(); select_dir(d);
    redraw |= R_TREE;
}

static void do_print(void)
{
    u16 i; char *p; u8 c;
    if (!nv && mode) return;
    if (!confirm(mode ? "Print file list to printer (slot 1)?" : "Print directory tree to printer (slot 1)?")) return;
    status("Printing...  ");
    prn_begin();
    dir_path(curdir(), pbuf);
    for (p = "AppleTree Pro Gold - "; *p; ++p) prn_char(*p);
    for (p = pbuf; *p; ++p) prn_char(*p);
    prn_char('\r'); prn_char('\r');
    if (mode) {
        for (i = 0; i < nv; ++i) {
            file_entry(tmp, vlist[i], 1);
            for (p = tmp; *p; ++p) prn_char(*p);
            prn_char('\r');
        }
    } else {
        for (c = 0; c < norder; ++c) {
            memset(tmp, ' ', dirs[order[c]].depth * 2);
            tmp[dirs[order[c]].depth * 2] = 0;
            strcat(tmp, dirs[order[c]].name);
            for (p = tmp; *p; ++p) prn_char(*p);
            prn_char('\r');
        }
    }
    prn_end();
    redraw = R_ALL;
}

static void do_exec(void)
{
    u16 fi; u8 err;
    if (!nv) return;
    fi = vlist[fcur];
    if (files[fi].type != T_SYS && files[fi].type != T_BAS) {
        message("Only SYS and BAS files can be executed");
        return;
    }
    file_path(fi, pbuf);
    strcpy(tmp, "Execute "); strcat(tmp, pbuf); strcat(tmp, " ?");
    if (!confirm(tmp)) return;
    err = sys_exec(pbuf);
    scr_init();
    error(err, pbuf);
    redraw = R_ALL;
}

/* ------------------------------------------------------------------ */
/* file viewer                                                         */

static int vh;
static u32 vsize, cbase, toff;
static u16 clen;
static u8  vhex;
#define NCP 64
#ifdef __CC65__
#define cp ((u32 *)0x1700)                /* $1700-$17FF */
#else
static u32 cp[NCP];
#endif
static u8  ncp;

static u16 vbyte(u32 off)
{
    if (off < cbase || off >= cbase + clen) {
        cbase = off & ~(u32)511;
        clen = 0;
        if (fs_seek(vh, cbase) == 0) {
            int r = fs_read(vh, iobuf, 512);
            if (r > 0) clen = r;
        }
        if (off >= cbase + clen) return 0x100;
    }
    return iobuf[(u16)(off - cbase)];
}

#ifdef __CC65__
#pragma code-name (push, "LC")
#endif

/* scan one text line starting at off; copy printable text to o (may be NULL) */
static u32 tline(u32 off, char *o)
{
    u8 n = 0; u16 c;
    for (;;) {
        if (off >= vsize) break;
        c = vbyte(off);
        if (c > 0xFF) { off = vsize; break; }
        ++off; c &= 0x7F;
        if (c == 13) { if (off < vsize && (vbyte(off) & 0x7F) == 10) ++off; break; }
        if (c == 10) break;
        if (o) o[n] = (c < 32 || c == 127) ? ' ' : c;
        if (++n >= 80) {
            if (off < vsize) {
                c = vbyte(off) & 0x7F;
                if (c == 13) { ++off; if (off < vsize && (vbyte(off) & 0x7F) == 10) ++off; }
                else if (c == 10) ++off;
            }
            break;
        }
    }
    if (o) o[n] = 0;
    return off;
}

static u32 line_off(u16 n)
{
    u8 k = n >> 4, j; u32 off; u16 l;
    while (ncp <= k && ncp < NCP) {
        off = cp[ncp - 1];
        for (j = 0; j < 16; ++j) off = tline(off, 0);
        cp[ncp++] = off;
    }
    if (k >= ncp) k = ncp - 1;
    off = cp[k];
    for (l = (u16)k << 4; l < n; ++l) off = tline(off, 0);
    return off;
}

static void viewer(u16 fi)
{
    u16 vtop = 0; u8 r, k, i, x; u32 off, eoff; u16 c;
    file_path(fi, pbuf);
    vh = fs_open(pbuf);
    if (vh < 0) { error(0x46, pbuf); return; }
    vsize = files[fi].size; cbase = 0; clen = 0;
    cp[0] = 0; ncp = 1; toff = 0; vhex = files[fi].type != 0x04 && files[fi].type != 0xB0;
    for (r = 0; r < 24; ++r) scr_fill(0, r, 80, ' ');
    for (;;) {
        scr_inv = 1;
        putw(0, 0, " View: ", 7); putw(7, 0, pbuf, 57);
        putw(64, 0, vhex ? "  HEX" : " TEXT", 5);
        fmtnum(tmp, vsize); putw(69, 0, "", 11); scr_puts(79 - strlen(tmp), 0, tmp);
        scr_inv = 0;
        off = toff;
        for (r = 1; r < 23; ++r) {
            if (off >= vsize) { scr_fill(0, r, 80, ' '); continue; }
            if (vhex) {
                memset(tmp, ' ', 80);
                for (i = 0; i < 6; ++i) tmp[i] = hexd[(off >> (20 - i * 4)) & 15];
                for (i = 0; i < 16; ++i) {
                    if (off + i >= vsize) break;
                    c = vbyte(off + i);
                    if (c > 0xFF) break;
                    x = 8 + i * 3 + (i > 7);
                    tmp[x] = hexd[c >> 4]; tmp[x + 1] = hexd[c & 15];
                    c &= 0x7F;
                    tmp[58 + i] = (c < 32 || c == 127) ? '.' : c;
                }
                tmp[74] = 0;
                putw(0, r, tmp, 80);
                off += 16;
            } else {
                off = tline(off, tmp);
                putw(0, r, tmp, 80);
            }
        }
        eoff = off;
        menu_line(23, " ~V~I~E~W   Arrows/^W/^Z scroll   ~Hex/text toggle   ^Q/^E top/end   ~E~S~C exit ");
        k = getk();
        if (k == K_ESC || k == K_RET) break;
        if (k == 'H') { vhex ^= 1; toff = vhex ? (toff & ~(u32)15) : 0; vtop = 0; continue; }
        if (vhex) {
            if (k == K_DOWN && eoff < vsize) toff += 16;
            else if (k == K_UP && toff) toff -= 16;
            else if (k == K_PGDN && eoff < vsize) toff += 16 * 22;
            else if (k == K_PGUP) toff = toff > 16 * 22 ? toff - 16 * 22 : 0;
            else if (k == K_HOME) toff = 0;
            else if (k == K_END) toff = vsize > 16 * 22 ? ((vsize - 16 * 22 + 15) & ~(u32)15) : 0;
        } else {
            if (k == K_DOWN && eoff < vsize) { toff = tline(toff, 0); ++vtop; }
            else if (k == K_UP && vtop) toff = line_off(--vtop);
            else if (k == K_PGDN && eoff < vsize) { vtop += 22; toff = line_off(vtop); }
            else if (k == K_PGUP) { vtop = vtop > 22 ? vtop - 22 : 0; toff = line_off(vtop); }
            else if (k == K_HOME) { vtop = 0; toff = 0; }
            else if (k == K_END) {
                status("Searching end of file...");
                while (tline(line_off(vtop + 22), 0) < vsize || line_off(vtop + 22) < vsize) {
                    vtop += 22; if (key_avail()) break;
                }
                toff = line_off(vtop);
            }
        }
    }
    fs_close(vh);
    redraw = R_ALL;
    for (r = 0; r < 24; ++r) scr_fill(0, r, 80, ' ');
}

#ifdef __CC65__
#pragma code-name (pop)
#endif

/* ------------------------------------------------------------------ */
/* help                                                                */

static const char *const helptxt[] = {
    "AppleTree Pro Gold " VERSION "  -  tree-style file manager for ProDOS 8",
    "",
    "MOVING AROUND                         DIRECTORY WINDOW",
    "  Arrows / ^K ^J ^H ^U  cursor          A  Avail - volume space",
    "  OA+Up / ^W   page up                  D  Delete empty directory",
    "  OA+Down / ^Z page down                F  Filespec (wildcards * and ?)",
    "  OA+Left / ^Q top                      G  Global / S Showall files on disk",
    "  OA+Right / ^E end                     I  Invert tags   T/U Tag/Untag dir",
    "  RETURN  open / expand file window     L  Log another volume",
    "  ESC     back to tree                  M  Make directory   R Rename",
    "                                        -  collapse  +  expand  *  expand all",
    "FILE WINDOW                             P  Print tree / file list (slot 1)",
    "  A  Attributes (lock / unlock)",
    "  C  Copy        M  Move              TAGGED-FILE COMMANDS",
    "  D  Delete      R  Rename              Hold Open-Apple, or press ^A first,",
    "  T  Tag         U  Untag               then the command letter. Also:",
    "  I  Invert      V  View (text/hex)     ^C copy ^D delete ^O move  ^T tag all",
    "  X  eXecute SYS/BAS                    ^X untag all   ^P print",
    "  ^S Sort order  ^F Format (names/full)",
    "",
    "Independent program, not affiliated with any commercial file manager.",
    "Q  Quit",
    0 };

static void help(void)
{
    u8 i;
    for (i = 0; i < 24; ++i) scr_fill(0, i, 80, ' ');
    for (i = 0; helptxt[i]; ++i) scr_puts(1, i, helptxt[i]);
    putinv(1, 23, " Press any key ", 15);
    key_get();
    for (i = 0; i < 24; ++i) scr_fill(0, i, 80, ' ');
    redraw = R_ALL;
}

/* ------------------------------------------------------------------ */
/* key dispatch                                                        */

static void tree_key(u8 k)
{
    u8 old = tcur;
    switch (k) {
    case K_UP: if (tcur) --tcur; break;
    case K_DOWN: if (tcur + 1 < norder) ++tcur; break;
    case K_LEFT:
        if (dirs[curdir()].parent != NONE) select_dir(dirs[curdir()].parent); break;
    case K_RIGHT:
        if (dirs[curdir()].child != NONE) { dirs[curdir()].flags &= ~DF_COLL; build_order(); ++tcur; redraw |= R_TREE; }
        break;
    case K_PGUP: tcur = tcur > TH ? tcur - TH : 0; break;
    case K_PGDN: tcur = tcur + TH < norder ? tcur + TH : norder - 1; break;
    case K_HOME: tcur = 0; break;
    case K_END: tcur = norder - 1; break;
    case K_RET: case K_TAB:
        mode = 1; fcur = ftop = 0; redraw = R_ALL; break;
    case 'A': do_avail(); break;
    case 'D': do_deldir(); break;
    case 'F': do_filespec(); break;
    case 'G': case 'S':
        showall = 1; mode = 1; vdirty = sdirty = 1; fcur = ftop = 0; redraw = R_ALL; break;
    case 'I': tag_range(2, 0); break;
    case 'L': do_log(); break;
    case 'M': do_makedir(); break;
    case 'P': case KT('P'): do_print(); break;
    case 'R': do_rendir(); break;
    case 'T': tag_range(1, 0); break;
    case 'U': tag_range(0, 0); break;
    case KT('T'): tag_range(1, 1); break;
    case KT('U'): tag_range(0, 1); break;
    case KT('I'): tag_range(2, 1); break;
    case '-': collapse(0); break;
    case '+': collapse(1); break;
    case '*': collapse(2); break;
    case K_SORT: do_sort(); break;
    case '?': case '/': help(); break;
    }
    if (tcur != old) { vdirty = sdirty = 1; fcur = ftop = 0; redraw |= R_TREE | R_FILES | R_STATS | R_HDR; }
}

static void file_key(u8 k)
{
    u16 old = fcur; u8 cols = fdetail ? 1 : 3; u16 per = (mode == 2 ? EH : FH) * cols;
    switch (k) {
    case K_UP: if (fcur >= cols) fcur -= cols; break;
    case K_DOWN: if (fcur + cols < nv) fcur += cols; else if (fcur + 1 < nv && cols > 1 && (fcur / cols) < (nv - 1) / cols) fcur = nv - 1; break;
    case K_LEFT: if (fcur) --fcur; break;
    case K_RIGHT: if (fcur + 1 < nv) ++fcur; break;
    case K_PGUP: fcur = fcur > per ? fcur - per : 0; break;
    case K_PGDN: fcur = fcur + per < nv ? fcur + per : (nv ? nv - 1 : 0); break;
    case K_HOME: fcur = 0; break;
    case K_END: fcur = nv ? nv - 1 : 0; break;
    case K_RET: mode = (mode == 1) ? 2 : 1; redraw = R_ALL; ftop = 0;
        { u8 r; for (r = 2; r < 20; ++r) scr_fill(1, r, 57, ' '); }
        break;
    case K_ESC: case K_TAB:
        mode = 0; showall = 0; vdirty = sdirty = 1; fcur = ftop = 0; redraw = R_ALL; break;
    case 'A': do_attrib(0); break;
    case KT('A'): do_attrib(1); break;
    case 'C': do_copy(0, 0); break;
    case KT('C'): do_copy(1, 0); break;
    case 'M': do_copy(0, 1); break;
    case KT('M'): do_copy(1, 1); break;
    case 'D': do_delete(0); break;
    case KT('D'): do_delete(1); break;
    case 'R': do_rename_file(0); break;
    case KT('R'): do_rename_file(1); break;
    case 'F': do_filespec(); break;
    case 'I': case KT('I'): tag_range(2, 0); break;
    case 'T': if (nv) { files[vlist[fcur]].flags |= FF_TAG; if (fcur + 1 < nv) ++fcur; sdirty = 1; redraw |= R_FILES | R_STATS; } break;
    case 'U': if (nv) { files[vlist[fcur]].flags &= ~FF_TAG; if (fcur + 1 < nv) ++fcur; sdirty = 1; redraw |= R_FILES | R_STATS; } break;
    case KT('T'): tag_range(1, 0); break;
    case KT('U'): tag_range(0, 0); break;
    case 'V': if (nv) viewer(vlist[fcur]); break;
    case 'X': do_exec(); break;
    case 'P': case KT('P'): do_print(); break;
    case K_SORT: do_sort(); break;
    case K_FMT: fdetail ^= 1; ftop = 0; redraw |= R_FILES; break;
    case '?': case '/': help(); break;
    }
    if (fcur != old) redraw |= R_FILES | (showall ? R_HDR : 0);
}

#ifndef __CC65__
#define main atpg_main
#endif
int main(void)
{
    u8 k, i;
    scr_init();
    for (i = 0; i < 24; ++i) scr_fill(0, i, 80, ' ');
    if (fs_startdir(pbuf2) || pbuf2[0] != '/') {
        static char v[1][16];
        if (!fs_vols(v, 1)) { message("No ProDOS volume found"); scr_done(); return 1; }
        pbuf2[0] = '/'; strcpy(pbuf2 + 1, v[0]);
    }
    strcpy(dest, pbuf2);
    splash();
    log_volume(dest);
    draw_stats();                                            /* like the original: stats appear */
    { u16 w; for (w = 0; w < 20000 && !key_avail(); ++w) ; } /* ~1.5 s, any key skips */
    splash_on = 0;
    for (i = 0; i < 24; ++i) scr_fill(0, i, 80, ' ');
    dest[0] = 0;
    for (;;) {
        if (vdirty && (redraw & (R_FILES | R_STATS))) build_vlist();
        do_redraw();
        k = getk();
        if (k == 'Q') {
            if (confirm("Quit AppleTree Pro Gold?")) break;
            redraw |= R_MENU;
            continue;
        }
        if (mode) file_key(k); else tree_key(k);
    }
    scr_done();
    return 0;
}
