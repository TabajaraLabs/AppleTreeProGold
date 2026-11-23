/* AppleTree Pro Gold - platform interface */
#ifndef PLAT_H
#define PLAT_H

#ifndef __CC65__
#include <stdint.h>
#define __fastcall__
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#else
typedef unsigned char u8;
typedef unsigned int  u16;
typedef unsigned long u32;
#endif

/* key codes (Apple II raw codes) */
#define K_LEFT  0x08
#define K_TAB   0x09
#define K_DOWN  0x0A
#define K_UP    0x0B
#define K_RET   0x0D
#define K_RIGHT 0x15
#define K_ESC   0x1B
#define K_DEL   0x7F

/* ---- screen (80x24) ---- */
extern u8 scr_inv;      /* 1 = inverse video for following output   */
extern u8 scr_mach;     /* 0 = IIe/IIc/IIgs 80 col, 1 = Videx (II+)  */
extern u8 scr_mtext;    /* 1 = MouseText available (enhanced IIe, IIc, IIgs) */

/* box-drawing glyphs: MouseText when available, else ASCII (in brackets).
   MouseText has no corners/tees, so frames use edge lines that meet at cell edges:
   top row = underscore, bottom row = overbar, left side = line on the cell's right
   edge, right side = line on the cell's left edge, corners blank.            */
#define G_HT  1   /* top edge         '_'      [-] */
#define G_HB  2   /* bottom edge      overbar  [-] */
#define G_VL  3   /* left side        |  right [|] */
#define G_VR  4   /* right side       |  left  [|] */
#define G_HM  5   /* inner divider    centre - [-] */
#define G_CR  6   /* outer corner     blank    [+] */
#define G_JT  7   /* tee on top edge  '_'      [+] */
#define G_JB  8   /* tee on bottom    overbar  [+] */
#define G_JL  9   /* tee on left side          [+] */
#define G_JR 10   /* tee on right side         [+] */
#define G_JM 11   /* tee on a divider centre - [+] */
/* tree connectors (user's choice, 2026-11-22): verticals $5A (line on the cell's right edge),
   branch = $5A + centre line $53, last branch = $54 + centre line ($54 has its vertical on the
   LEFT edge and its stroke at the BOTTOM, so it does not line up exactly; the all-$54 variant
   with '_' strokes was tried and rejected)                                                    */
#define G_TV 12   /* tree: vertical       $5A      [|] */
#define G_TT 13   /* tree: branch         $5A      [+] */
#define G_TL 14   /* tree: last branch    $54      [`] */
#define G_PE 15   /* progress bar, empty part: checkerboard $56 [.] */
u8   scr_init(void);
void scr_done(void);
void __fastcall__ scr_puts(u8 x, u8 y, const char *s);
void __fastcall__ scr_fill(u8 x, u8 y, u8 n, u8 c);

/* ---- keyboard ---- */
extern u8 key_oa;       /* Open-Apple held during last key (IIe)     */
u8 key_get(void);
u8 key_avail(void);

/* ---- file system (ProDOS semantics) ---- */
struct fent {
    char name[16];
    u8   type;
    u16  aux;
    u32  size;
    u16  mdate, mtime;   /* ProDOS packed date / time (hour<<8|min) */
    u8   access;
    u16  blocks;         /* blocks used on disk (log progress) */
};
#define T_DIR 0x0F
#define T_BAS 0xFC
#define T_SYS 0xFF

extern u8 iobuf[1024];  /* shared I/O buffer (owned by core) */

u8   fs_startdir(char *buf);                 /* "/VOL/DIR" -> buf */
u8   fs_vols(char (*list)[16], u8 max);      /* online volume names */
u8   fs_volinfo(const char *vol, u16 *total, u16 *used);
u8   fs_opendir(const char *path);
u8   fs_readdir(struct fent *e);
void fs_closedir(void);
u8   fs_getinfo(const char *path, struct fent *e);
u8   fs_setaccess(const char *path, u8 access);
u8   fs_copy(const char *src, const char *dst, const struct fent *e);
u8   fs_delete(const char *path);
u8   fs_rename(const char *from, const char *to);
u8   fs_mkdir(const char *path);
int  fs_open(const char *path);
int  fs_read(int h, void *buf, u16 n);
u8   fs_seek(int h, u32 off);
void fs_close(int h);

void clk_get(u16 *date, u16 *time);
u8   sys_exec(const char *path);            /* only returns on error */
void prn_begin(void);
void __fastcall__ prn_char(char c);
void prn_end(void);

#endif
