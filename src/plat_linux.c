/* Linux test harness: fake 80x24 screen, scripted keys, host dir = volume */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include "plat.h"

u8 scr_inv, scr_mach, scr_mtext, key_oa;
/* G_* glyphs: ASCII, or MouseText look-alikes with ATPG_MT=1 */
static char gl(u8 c) { if (c >= 1 && c <= 15) return (getenv("ATPG_MT") ? " _~[]= _~[]=[[L:" : " --||-++++++|+`.")[c]; return c; }
static char S[24][81]; static u8 I[24][80];
static const char *root, *keys; static char vol[16];

static void snap(void)
{
    int y, x, inv = getenv("ATPG_INV") != 0;
    printf("+--------------------------------------------------------------------------------+\n");
    for (y = 0; y < 24; ++y) {
        printf("|");
        for (x = 0; x < 80; ++x) {
            char c = S[y][x];
            if (inv && I[y][x]) c = (c == ' ') ? '#' : (isalpha((unsigned char)c) ? tolower(c) : c);
            putchar(c);
        }
        printf("|\n");
    }
    printf("+--------------------------------------------------------------------------------+\n");
}

u8 scr_init(void) { memset(S, ' ', sizeof S); for (int y = 0; y < 24; ++y) S[y][80] = 0; return 0; }
void scr_done(void) {}
void scr_puts(u8 x, u8 y, const char *s) { while (*s && x < 80) { S[y][x] = gl(*s++); I[y][x++] = scr_inv; } }
void scr_fill(u8 x, u8 y, u8 n, u8 c) { while (n-- && x < 80) { S[y][x] = gl(c); I[y][x++] = scr_inv; } }

static void snap(void);
/* ATPG_SPLASH=1: show the start-up screen (main polls key_avail while it is up) */
u8 key_avail(void) { static int once; if (getenv("ATPG_SPLASH") && !once++) snap(); return 0; }
u8 key_get(void)
{
    key_oa = 0;
    for (;;) {
        if (!*keys) { snap(); exit(0); }
        if (*keys == '{') {
            const char *e = strchr(keys, '}'); char t[16]; int n = e - keys - 1;
            memcpy(t, keys + 1, n); t[n] = 0; keys = e + 1;
            if (!strcmp(t, "SNAP")) { snap(); continue; }
            if (!strcmp(t, "UP")) return K_UP;
            if (!strcmp(t, "DOWN")) return K_DOWN;
            if (!strcmp(t, "LEFT")) return K_LEFT;
            if (!strcmp(t, "RIGHT")) return K_RIGHT;
            if (!strcmp(t, "RET")) return K_RET;
            if (!strcmp(t, "ESC")) return K_ESC;
            if (t[0] == '^') return t[1] & 0x1F;
            if (t[0] == '@') { key_oa = 1; return t[1]; }
            continue;
        }
        return *keys++;
    }
}

static void host(const char *p, char *out)
{
    const char *q = strchr(p + 1, '/');
    strcpy(out, root); if (q) strcat(out, q);
}

u8 fs_startdir(char *b) { sprintf(b, "/%s", vol); return 0; }
u8 fs_vols(char (*l)[16], u8 max) { strcpy(l[0], vol); strcpy(l[1], "OTHER"); return 2; }
u8 fs_volinfo(const char *v, u16 *t, u16 *u) { *t = 65535; *u = 12345; return 0; }

static u16 pdate(time_t t) { struct tm *m = localtime(&t); return ((m->tm_year % 100) << 9) | ((m->tm_mon + 1) << 5) | m->tm_mday; }
static u16 ptime(time_t t) { struct tm *m = localtime(&t); return (m->tm_hour << 8) | m->tm_min; }

static u8 typeof_(const char *n, int dir)
{
    const char *d = strrchr(n, '.');
    if (dir) return T_DIR;
    if (d && !strcmp(d, ".SYSTEM")) return T_SYS;
    if (d && !strcmp(d, ".TXT")) return 0x04;
    if (d && !strcmp(d, ".BAS")) return T_BAS;
    return 0x06;
}

static u8 fill(const char *hp, const char *name, struct fent *e)
{
    struct stat st;
    if (stat(hp, &st)) return 0x46;
    snprintf(e->name, 16, "%s", name);
    for (char *c = e->name; *c; ++c) *c = toupper(*c);
    e->type = typeof_(e->name, S_ISDIR(st.st_mode));
    e->aux = 0; e->size = S_ISDIR(st.st_mode) ? 512 : st.st_size;
    e->mdate = pdate(st.st_mtime); e->mtime = ptime(st.st_mtime);
    e->access = (st.st_mode & S_IWUSR) ? 0xE3 : 0x21;
    e->blocks = (u16)((e->size + 511) / 512) + 1;
    return 0;
}

static DIR *dp; static char dhp[512];
u8 fs_opendir(const char *p) { host(p, dhp); dp = opendir(dhp); return dp ? 0 : 0x44; }
u8 fs_readdir(struct fent *e)
{
    struct dirent *d; char hp[600];
    while ((d = readdir(dp))) {
        if (d->d_name[0] == '.') continue;
        snprintf(hp, sizeof hp, "%s/%s", dhp, d->d_name);
        if (!fill(hp, d->d_name, e)) return 1;
    }
    return 0;
}
void fs_closedir(void) { closedir(dp); }
u8 fs_getinfo(const char *p, struct fent *e)
{
    char hp[512]; const char *n = strrchr(p, '/');
    host(p, hp);
    return fill(hp, n + 1, e);
}
u8 fs_setaccess(const char *p, u8 a) { char hp[512]; host(p, hp); return chmod(hp, (a & 2) ? 0644 : 0444) ? 0x46 : 0; }
static u8 perr(void) { return errno == ENOENT ? 0x46 : errno == EEXIST ? 0x47 : errno == ENOTEMPTY ? 0x4E : 0x27; }
u8 fs_copy(const char *s, const char *d, const struct fent *e)
{
    char a[512], b[512]; FILE *i, *o; size_t n;
    host(s, a); host(d, b);
    if (!(i = fopen(a, "rb"))) return 0x46;
    if (!(o = fopen(b, "wb"))) { fclose(i); return 0x44; }
    while ((n = fread(iobuf, 1, sizeof iobuf, i)) > 0) fwrite(iobuf, 1, n, o);
    fclose(i); fclose(o); return 0;
}
u8 fs_delete(const char *p) { char hp[512]; struct stat st; host(p, hp); stat(hp, &st);
    if (S_ISDIR(st.st_mode) ? rmdir(hp) : unlink(hp)) return perr(); return 0; }
u8 fs_rename(const char *f, const char *t) { char a[512], b[512]; host(f, a); host(t, b); return rename(a, b) ? perr() : 0; }
u8 fs_mkdir(const char *p) { char hp[512]; host(p, hp); return mkdir(hp, 0755) ? perr() : 0; }
int fs_open(const char *p) { char hp[512]; host(p, hp); return open(hp, O_RDONLY); }
int fs_read(int h, void *b, u16 n) { return read(h, b, n); }
u8 fs_seek(int h, u32 o) { return lseek(h, o, SEEK_SET) < 0; }
void fs_close(int h) { close(h); }
void clk_get(u16 *d, u16 *t) { *d = (26 << 9) | (11 << 5) | 22; *t = (14 << 8) | 5; }
u8 sys_exec(const char *p) { printf("EXEC %s\n", p); return 0x46; }
void prn_begin(void) { printf("PRN:"); }
void prn_char(char c) { putchar(c == '\r' ? '\n' : c); }
void prn_end(void) {}

int atpg_main(void);
int main(int argc, char **argv)
{
    root = argv[1]; keys = argc > 2 ? argv[2] : "";
    const char *b = strrchr(root, '/'); snprintf(vol, 16, "%s", b ? b + 1 : root);
    for (char *c = vol; *c; ++c) *c = toupper(*c);
    return atpg_main();
}
