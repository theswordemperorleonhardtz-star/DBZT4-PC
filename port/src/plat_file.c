/*
 * The game's file access on PC.
 *
 * The game reads everything through CRI's ADXF library: three AFS archives ("partitions" 0..2), files addressed by
 * (partition, index), read in 2048-byte sectors, asynchronously. Here an archive is a FOLDER of loose files made by
 * port/tools/extract_disc.py (<root>/pzs3us1/00042.bin is partition 1, index 42) and a read finishes at once.
 * <root> is the environment variable BT3_DATA, or "gamedata". A file in <root>/mods/<same relative path> is used
 * instead of the original: that is the whole modding mechanism for now.
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plat_stages.h"
#include "plat_songs.h"

#define SECTOR 2048
enum { STAT_STOP = 1, STAT_READING = 2, STAT_READEND = 3, STAT_ERROR = 4 };

/* What the game holds for an open file. Only what can be saved and restored with the game's state: which file, its
   size, the read position, the status. The host's own file objects are in plat_fcache.c. */
typedef struct PortFile {
    char rel[128];
    int32_t sizeSct;
    int32_t posSct;
    int32_t stat;
} PortFile;

extern long Port_FileSize(const char *rel); /* plat_fcache.c */
extern size_t Port_FileReadAt(const char *rel, long offset, void *buf, size_t bytes);

static char sPartDir[8][64];
char gPortMoviePath[512]; /* host path of the movie file opened last */
int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size);
extern void *Port_LowAlloc(size_t size); /* plat_mem.c */
extern void Port_LowFree(void *addr);

static const char *root(void) {
    const char *r = getenv("BT3_DATA");
    return r != NULL ? r : "gamedata";
}
const char *Port_FileRoot(void) { return root(); }

/* BT3_FILE_ALIAS="rel=target;rel=target": a file the game asks for (its path under the data root) served from
   somewhere else. A missing archive entry (a new id) gets a host file this way. The manifest of added stages
   (port/plat_stages.c) is checked first. */
static const char *alias_target(const char *rel) {
    static char buf[512];
    const char *list = getenv("BT3_FILE_ALIAS");
    const char *p, *eq, *end;
    size_t rl = strlen(rel);

    if (PortStages_Alias(rel, buf, sizeof(buf))) {
        return buf;
    }
    if (PortSongs_Alias(rel, buf, sizeof(buf))) {
        return buf;
    }
    if (list == NULL) {
        return NULL;
    }
    for (p = list; *p != '\0'; p = end + 1) {
        eq = strchr(p, '=');
        end = strchr(p, ';');
        if (eq == NULL || (end != NULL && eq > end)) {
            if (end == NULL) break;
            continue;
        }
        if ((size_t)(eq - p) == rl && strncmp(p, rel, rl) == 0) {
            size_t n = end != NULL ? (size_t)(end - (eq + 1)) : strlen(eq + 1);
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, eq + 1, n);
            buf[n] = '\0';
            return buf;
        }
        if (end == NULL) break;
    }
    return NULL;
}

/* BT3_FILE_TRACE=1: every file the game opens and how big it is. For sizing the port's own buffers from the
   disc's files: the game's buffers are constants (battle_work.h), and a disc whose files are bigger overruns
   them when the file is read into the caller's buffer. */
static void file_trace(const char *rel, long size) {
    static int on = -1;

    if (on < 0) {
        on = getenv("BT3_FILE_TRACE") != NULL;
    }
    if (on) {
        fprintf(stderr, "bt3: file %s -> %ld bytes\n", rel, size);
        fflush(stderr);
    }
}

static PortFile *open_rel(const char *rel) {
    PortFile *f;
    long size = Port_FileSize(rel); /* (says so itself when the file is missing) */

    file_trace(rel, size);
    if (size < 0) {
        return NULL;
    }
    f = Port_LowAlloc(sizeof(PortFile)); /* the game keeps the handle in a 4-byte pointer */
    snprintf(f->rel, sizeof(f->rel), "%s", rel);
    f->sizeSct = (int32_t)((size + SECTOR - 1) / SECTOR);
    f->stat = STAT_STOP;
    return f;
}

/* The host path of a game file, for code that reads files itself (the sound streams): file `flid` of partition
   `ptid`, or with `fname` a loose file of the disc's data directory. A file under mods/ wins. 0 = not found. */
int Port_FilePath(int ptid, int flid, const char *fname, char *out, int size) {
    char rel[256];
    FILE *fp;
    int n;

    if (fname != NULL) {
        n = snprintf(rel, sizeof(rel), "disc/DATA/");
        for (; *fname != '\0' && *fname != ';' && n < 255; fname++) {
            rel[n++] = *fname == '\\' ? '/' : (char)toupper((unsigned char)*fname);
        }
        rel[n] = '\0';
    } else {
        snprintf(rel, sizeof(rel), "%s/%05d.bin", sPartDir[ptid & 7], flid);
    }
    {
        /* The streamed audio (a song added from outside the disc) reads through this path, so the manifest's
           aliases must apply here too, not only in open_rel. */
        const char *a = alias_target(rel);
        if (a != NULL) {
            fp = fopen(a, "rb"); /* as it is (the songs folder next to the game) */
            if (fp != NULL) {
                fclose(fp);
                snprintf(out, (size_t)size, "%s", a);
                return 1;
            }
            snprintf(rel, sizeof(rel), "%s", a);
        }
    }
    snprintf(out, (size_t)size, "%s/mods/%s", root(), rel);
    fp = fopen(out, "rb");
    if (fp == NULL) {
        snprintf(out, (size_t)size, "%s/%s", root(), rel);
        fp = fopen(out, "rb");
    }
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* "pzs3us1.afs" (any case, any directory, optional ";1") -> folder "pzs3us1". */
int ADXF_LoadPartitionNw(int ptid, char *fname, void *dir, void *ptinfo) {
    const char *base = fname;
    const char *p;
    int n = 0;

    (void)dir;
    (void)ptinfo;
    for (p = fname; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    for (p = base; *p != '\0' && *p != '.' && n < 63; p++) {
        sPartDir[ptid & 7][n++] = (char)tolower((unsigned char)*p);
    }
    sPartDir[ptid & 7][n] = '\0';
    return 0;
}

int ADXF_GetPtStat(int ptid) {
    return sPartDir[ptid & 7][0] != '\0' ? STAT_READEND : STAT_STOP;
}

void *ADXF_OpenAfs(int ptid, int flid) {
    char rel[128];

    snprintf(rel, sizeof(rel), "%s/%05d.bin", sPartDir[ptid & 7], flid);
    return open_rel(rel);
}

/* A file outside the archives, named relative to the disc's data directory. */
void *ADXF_Open(char *fname, void *atr) {
    char rel[256];
    int n;

    (void)atr;
    n = snprintf(rel, sizeof(rel), "disc/DATA/");
    for (; *fname != '\0' && *fname != ';' && n < 255; fname++) {
        rel[n++] = *fname == '\\' ? '/' : (char)toupper((unsigned char)*fname);
    }
    rel[n] = '\0';
    if (n > 4 && strcmp(&rel[n - 4], ".PSS") == 0) { /* a movie: its decoder (plat_movie.c) needs the path */
        Port_FilePath(0, 0, &rel[10], gPortMoviePath, sizeof(gPortMoviePath));
    }
    return open_rel(rel);
}

extern void Heap_CheckNow(const char *what); /* heap.c, with BT3_HEAP_CHECK=1 */

/* BtlScene_GetPackEntry and BtlScene_GetCommonEntry turn a word of a resource's offset table into a pointer.
   A table that does not match the game's layout (a mod's data) gives an entry outside every region the game
   uses, and the code that reads it then walks off into nothing (Res_RelocateOffsets read 0x6E1B9F40). Say
   which table and which index produced it. */
void Port_NoteBadEntry(const char *what, int32_t idx, uint32_t word, void *out, void *base) {
    uintptr_t p = (uintptr_t)out;
    uintptr_t b = (uintptr_t)base;

    if (p >= 0x10000000u && p < 0x80000000u) {
        return; /* inside the PS2 address space: a real entry */
    }
    fprintf(stderr, "bt3: %s entry %d: base %p word %08X -> %p (outside the game's memory)\n",
            what, (int)idx, base, (unsigned)word, out);
    (void)b;
    fflush(stderr);
}

/* File_LoadSync and File_LoadSyncEx also take the caller's buffer size and ignore it (file.c). Called from
   there when a file does not fit, so the run says which buffers are too small for this disc. */
void Port_NoteTooBig(int32_t id, int32_t fileBytes, int32_t bufBytes) {
    fprintf(stderr, "bt3: file id %d is %d bytes but the buffer is %d: only %d read\n",
            (int)id, (int)fileBytes, (int)bufBytes, (int)(bufBytes - bufBytes % SECTOR));
    fflush(stderr);
}

/* battle_load.c passes the caller's buffer size as a third argument; the game's File_Request has two
   parameters and reads the whole file into whatever it is given. When the file is bigger than that, the read
   runs past the buffer and tears the game's heap -- which showed as a crash seconds into a battle, on a disc
   whose files are bigger than the ones the port's fixed buffers were chosen for. Read what fits, here and now,
   and say exactly which file and how big, so the buffers can be sized from the disc's own files. A file that
   fits goes through the ordinary queue (below), so nothing about the normal path changes. */
void *File_RequestSized(int32_t id, void *buf, int32_t size) {
    /* (this file is the port's own: the game's s32 is spelled int32_t here; the ADXF functions below are
       declared here because they are defined further down the file) */
    extern void *File_OpenById(int32_t id);   /* sys/file.h: ADXF File_OpenById(s32) */
    extern void *File_Request(int32_t id, void *buf);
    extern int32_t ADXF_GetFsizeSct(void *h);
    extern void ADXF_Close(void *h);
    extern void *Heap_Alloc(int32_t size, uint32_t align, int32_t fromTail, int32_t heap);
    extern void Heap_Free(void *ptr);
    extern uint32_t Port_HeapBlockSize(void *ptr); /* heap.c */
    void *h = File_OpenById(id);
    PortFile *f = h;
    void *dst;
    int32_t sectors;
    int32_t want;

    if (f == NULL) {
        return File_Request(id, buf); /* no handle: the queue's own open decides (it waits for a missing file) */
    }
    sectors = ADXF_GetFsizeSct(f);
    if (buf != NULL) {
        /* What the caller says is not the buffer's size: BtlRes_LoadFile passes file->size, which is the size
           of the file that was loaded before -- zero at first, then the previous file's size. The game heap
           knows the truth. A buffer that is not the heap's (a port block, a stage buffer) gives 0 and the
           caller's number stands. This is what the crash came down to: a model buffer of 843776 bytes
           (BTL_RES_MODEL_SIZE) receiving a file of megabytes. */
        int32_t cap = (int32_t)Port_HeapBlockSize(buf);

        if (cap > 0) {
            size = cap;
        }
    }
    if (buf == NULL || size <= 0 || sectors * SECTOR <= size) {
        ADXF_Close(h);
        return File_Request(id, buf);
    }
    /* Give back a buffer that holds the whole file: every caller of File_Request3 assigns the result back
       (res->stage = File_Request3(...), m->buf[1] = File_Request3(...)), so the bigger one takes effect and
       the small one goes back to the game's heap. Reading it into the caller's own buffer instead would run
       past it and tear the heap, which is what made the battle crash seconds in -- the port's buffer sizes
       are the console disc's, and this disc's files are much bigger (a member file of 680 KB into 4 KB). */
    dst = Heap_Alloc(sectors * SECTOR, 0x40, 0, 2 /* HEAP_ANY */);
    if (dst == NULL) {
        /* no room for the whole file: read what fits, so that nothing is written past the buffer */
        want = size / SECTOR;
        if (want > 0) {
            Port_FileReadAt(f->rel, 0, buf, (size_t)want * SECTOR);
        }
        fprintf(stderr, "bt3: file id %d is %d bytes, its buffer %d, no room for a bigger one: read %d\n",
                (int)id, (int)(sectors * SECTOR), (int)size, (int)(want * SECTOR));
        fflush(stderr);
        ADXF_Close(h);
        return buf;
    }
    Port_FileReadAt(f->rel, 0, dst, (size_t)sectors * SECTOR);
    fprintf(stderr, "bt3: file id %d is %d bytes but its buffer is %d: read into a %d-byte buffer\n",
            (int)id, (int)(sectors * SECTOR), (int)size, (int)(sectors * SECTOR));
    fflush(stderr);
    /* The old buffer is deliberately NOT given back: the game keeps pointers to its buffers in more than one
       place (a BtlResFile has buf and orig, the effect loader keeps its own), and freeing it here left those
       pointers dangling -- the next read then parsed another file's data as pointer offsets (a wild address in
       Res_RelocateOffsets). It is a few hundred kilobytes per file that did not fit, once. */
    (void)buf;
    ADXF_Close(h);
    return dst;
}

void ADXF_Close(void *h) {
    PortFile *f = h;

    if (f != NULL) {
        Port_LowFree(f); /* (the host's file stays open for a while: plat_fcache.c) */
        Heap_CheckNow("ADXF_Close: after the handle went back to the port's region");
    }
}

int ADXF_GetFsizeSct(void *h) {
    return ((PortFile *)h)->sizeSct;
}

/* Reads nsct sectors at the current position; the part of the last sector behind the end of the file is zero. */
int ADXF_ReadNw(void *h, int nsct, void *buf) {
    PortFile *f = h;
    size_t got;

    if (nsct > f->sizeSct - f->posSct) {
        nsct = f->sizeSct - f->posSct;
    }
    got = Port_FileReadAt(f->rel, (long)f->posSct * SECTOR, buf, (size_t)nsct * SECTOR);
    memset((uint8_t *)buf + got, 0, (size_t)nsct * SECTOR - got);
    f->posSct += nsct;
    f->stat = STAT_READEND;
    return nsct;
}

int ADXF_Stop(void *h) {
    ((PortFile *)h)->stat = STAT_STOP;
    return 0;
}

int ADXF_GetStat(void *h) {
    return ((PortFile *)h)->stat;
}

int ADXF_Tell(void *h) {
    return ((PortFile *)h)->posSct;
}

/* CRI set-up and servicing: nothing to do without a drive or a second thread. */
void ADXPS2_ExecVint(int mode) { (void)mode; }
void ADXPS2_SetupDvdFs(void *param) { (void)param; }
void ADXPS2_LoadFcacheDvd(void *param) { (void)param; }
void ADXPS2_SetupThrd(void *param, int unk) { (void)param; (void)unk; }
void ADXM_ExecMain(void) {}
/* Waits for the next vertical blank. Headless: the blank happens now (no real-time pacing). */
extern void Port_VBlank(void);
void ADXM_WaitVsync(void) { Port_VBlank(); }
void ADXERR_EntryErrFunc(void (*func)(void *obj, char *msg), void *obj) { (void)func; (void)obj; }

/* The drive: a PS2 DVD is always present and idle (sceCdGetDiskTypeSafe 0x14 = PS2 DVD, sceCdStatus 10 = paused). */
int sceCdGetDiskTypeSafe(void) { return 0x14; }
int sceCdStatus(void) { return 10; }
