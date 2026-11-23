/* AppleTree Pro Gold - ProDOS 8 platform layer (cc65) */
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <apple2.h>
#include <device.h>
#include "plat.h"

u8 __fastcall__ mli(u8 cmd, void *parm);

/* The only malloc() user is cc65's opendir() (DIR = ~590 bytes), called only while logging a
   volume, never while the viewer or copy use iobuf. Hand it iobuf instead of a heap: the heap
   is whatever is left between BSS and the C stack and shrinks every time the program grows
   (an empty heap made every volume look empty, 2026-11-22). */
static u8 mused;
void *malloc(size_t n)
{
    if (mused || n > sizeof iobuf) { errno = ENOMEM; return 0; }
    mused = 1;
    return iobuf;
}
void free(void *p) { if (p == iobuf) mused = 0; }

static char ppath[66];
static void topas(const char *s)
{
    u8 n = strlen(s); if (n > 64) n = 64;
    ppath[0] = n; memcpy(ppath + 1, s, n);
}

static struct { u8 cnt; char *path; u8 access, type; u16 aux; u8 stype;
                u16 blocks, mdate, mtime, cdate, ctime; } gfi;
static struct { u8 cnt; char *path; u8 access, type; u16 aux; u8 n1, n2, n3;
                u16 mdate, mtime; } sfi;

/* cc65 calls that fail before reaching ProDOS (e.g. opendir's malloc) leave _oserror at 0 */
static u8 oserr(void) { return _oserror ? _oserror : errno == ENOMEM ? 0xF2 : 0x27; }

u8 fs_startdir(char *buf)
{
    u8 l;
    if (!getcwd(buf, 65)) return 1;
    l = strlen(buf);
    while (l > 1 && buf[l - 1] == '/') buf[--l] = 0;
    return l < 2;
}

u8 fs_vols(char (*list)[16], u8 max)
{
    u8 n = 0, dev; char b[20]; char *p;
    for (dev = getfirstdevice(); dev != INVALID_DEVICE && n < max; dev = getnextdevice(dev)) {
        if (!getdevicedir(dev, b, sizeof b)) continue;
        p = b; if (*p == '/') ++p;
        strncpy(list[n], p, 15); list[n][15] = 0;
        p = strchr(list[n], '/'); if (p) *p = 0;
        if (list[n][0]) ++n;
    }
    return n;
}

static u8 getinfo(const char *path)
{
    topas(path);
    gfi.cnt = 10; gfi.path = ppath;
    return mli(0xC4, &gfi);
}

u8 fs_volinfo(const char *vol, u16 *total, u16 *used)
{
    char b[18]; u8 e;
    b[0] = '/'; strcpy(b + 1, vol);
    if ((e = getinfo(b)) != 0) return e;
    *total = gfi.aux; *used = gfi.blocks;
    return 0;
}

static DIR *dp;
u8 fs_opendir(const char *path)
{
    _oserror = 0;
    dp = opendir(path);
    return dp ? 0 : oserr();
}

u8 fs_readdir(struct fent *e)
{
    struct dirent *d = readdir(dp);
    if (!d) return 0;
    strcpy(e->name, d->d_name);
    e->type = d->d_type; e->aux = d->d_auxtype; e->size = d->d_size;
    /* cc65 snapshot (2.19-git): date and time are both in d_mtime (older 2.19: d_mdate/d_mtime) */
    memcpy(&e->mdate, &d->d_mtime.date, 2);
    memcpy(&e->mtime, &d->d_mtime.time, 2);
    e->access = d->d_access; e->blocks = d->d_blocks;
    return 1;
}

void fs_closedir(void) { closedir(dp); }

u8 fs_getinfo(const char *path, struct fent *e)
{
    u8 r = getinfo(path); const char *n;
    if (r) return r;
    n = strrchr(path, '/');
    strncpy(e->name, n ? n + 1 : path, 15); e->name[15] = 0;
    e->type = gfi.type; e->aux = gfi.aux; e->access = gfi.access;
    e->mdate = gfi.mdate; e->mtime = gfi.mtime; e->size = 0; e->blocks = gfi.blocks;
    return 0;
}

static u8 setinfo(const char *path, u8 access, u8 type, u16 aux, u16 md, u16 mt)
{
    topas(path);
    sfi.cnt = 7; sfi.path = ppath; sfi.access = access; sfi.type = type; sfi.aux = aux;
    sfi.n1 = sfi.n2 = sfi.n3 = 0; sfi.mdate = md; sfi.mtime = mt;
    return mli(0xC3, &sfi);
}

u8 fs_setaccess(const char *path, u8 access)
{
    u8 r = getinfo(path);
    if (r) return r;
    return setinfo(path, access, gfi.type, gfi.aux, gfi.mdate, gfi.mtime);
}

u8 fs_copy(const char *src, const char *dst, const struct fent *e)
{
    int in, out, n; u8 err = 0;
    _oserror = 0;
    in = open(src, O_RDONLY);
    if (in < 0) return oserr();
    _filetype = e->type; _auxtype = e->aux;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (out < 0) { err = oserr(); close(in); return err; }
    for (;;) {
        n = read(in, iobuf, sizeof iobuf);
        if (n < 0) { err = oserr(); break; }
        if (n == 0) break;
        if (write(out, iobuf, n) != n) { err = oserr(); break; }
    }
    close(out); close(in);
    if (err) { remove(dst); return err; }
    return setinfo(dst, e->access, e->type, e->aux, e->mdate, e->mtime);
}

u8 fs_delete(const char *path) { _oserror = 0; return remove(path) ? oserr() : 0; }
u8 fs_rename(const char *f, const char *t) { _oserror = 0; return rename(f, t) ? oserr() : 0; }
u8 fs_mkdir(const char *path) { _oserror = 0; return mkdir(path, 0) ? oserr() : 0; }
int fs_open(const char *path) { return open(path, O_RDONLY); }
int fs_read(int h, void *buf, u16 n) { return read(h, buf, n); }
u8 fs_seek(int h, u32 off) { return lseek(h, off, SEEK_SET) == -1L; }
void fs_close(int h) { close(h); }

void clk_get(u16 *date, u16 *time)
{
    mli(0x82, 0);
    *date = *(u16 *)0xBF90;
    *time = ((u16)*(u8 *)0xBF93 << 8) | *(u8 *)0xBF92;
}

u8 sys_exec(const char *path)
{
    scr_done();
    _oserror = 0;
    exec(path, "");
    return oserr();
}
