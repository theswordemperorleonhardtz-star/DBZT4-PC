#include "common.h"
#include "sys/heap.h"
#ifdef PORT
/* For the heap checker below.  <stdio.h> for fprintf/stderr/fflush -- stderr is a macro on mingw, so
   declaring it by hand links against something that is not a FILE pointer and the first message
   crashes.  <stdlib.h> stays out: this file declares the game's own malloc (u32), which the port's
   macro turns into Port_Malloc, and stdlib's malloc would clash with that declaration.  The two
   below are declared by hand for the same reason. */
#include <stdio.h>
extern char *getenv(const char *name);
extern void exit(int status);
#endif

extern void *malloc(u32 size);
s32 Mem_Align4Pad(u32 value);
s32 Mem_Align4Rem(u32 value);

extern HeapBlock *gHeapStart[HEAP_COUNT];
extern HeapBlock *gHeapEnd[HEAP_COUNT];
extern s32 gHeapSize[HEAP_COUNT];
/* Written by the setters below and cleared by Heap_Init, but never read: leftovers of a debug feature. */
extern s32 gHeapDebugValue;
extern u8 gHeapDebugFlag;
extern s32 gHeapDebugUnused;
extern u8 D_3BE71C[];
extern s32 gHeapUnk70[HEAP_COUNT];
extern s32 gHeapUnk78[HEAP_COUNT];

void Heap_ReportBadFree(void);

u32 Heap_BlockData(HeapBlock *block) {
    return (u32)block + sizeof(HeapBlock);
}

void Heap_InitFreeBlock(HeapBlock *block, s32 size) {
    block->magic = HEAP_MAGIC;
    block->align = 0x20;
    block->dataSize = size - 0x20;
    block->used = 0;
    block->fromTail = 0;
    block->size = size;
    block->data = 0;
    *(s32 *)((u8 *)block + size - 4) = size;
}

void Heap_InitUsedBlock(HeapBlock *block, s32 size, s32 dataSize, u32 align, s32 fromTail) {
    u32 data = Heap_BlockData(block);

    block->magic = HEAP_MAGIC;
    block->used = 1;
    block->fromTail = fromTail;
    block->align = align;
    block->size = size;
    block->data = data + (align - data % align) % align;
    block->dataSize = dataSize;
    *(s32 *)((u8 *)block + size - 4) = size;
}

HeapBlock *Heap_PrevBlock(HeapBlock *block) {
    HeapBlock *prev = NULL;

    if (block != NULL) {
        if (block != gHeapStart[0] && block != gHeapStart[1]) {
            prev = (HeapBlock *)((u8 *)block - *(s32 *)((u8 *)block - 4));
        }
    }
    return prev;
}

HeapBlock *Heap_NextBlock(HeapBlock *block) {
    HeapBlock *next = NULL;

    if (block != NULL) {
        block = (HeapBlock *)((u8 *)block + block->size);
        if (block != gHeapEnd[0] && block != gHeapEnd[1]) {
            next = block;
        }
    }
    return next;
}

void Heap_Create(void) {
    s32 size = 0x1EFB000 - (u32)D_3BE71C;
    HeapBlock *start;

    size += Mem_Align4Pad(size);
    start = malloc(size);
    gHeapSize[0] = size;
    gHeapEnd[0] = (HeapBlock *)((u8 *)start + size);
    gHeapStart[0] = start;
    Heap_InitFreeBlock(start, size);
}

HeapBlock *Heap_FindBlock(u32 data) {
    HeapBlock *found = NULL;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 1 && block->data == data) {
                found = block;
                break;
            }
        }
    }
    return found;
}

HeapBlock *Heap_FindFreeFromHead(s32 heap, s32 size);

HeapBlock *Heap_SearchFromHead(s32 size, s32 heap) {
    HeapBlock *block = NULL;
    s32 i;

    if (heap != HEAP_ANY) {
        block = Heap_FindFreeFromHead(heap, size);
    } else {
        for (i = 0; i < HEAP_COUNT; i++) {
            block = Heap_FindFreeFromHead(i, size);
            if (block != NULL) {
                break;
            }
        }
    }
    return block;
}

HeapBlock *Heap_FindFreeFromHead(s32 heap, s32 size) {
    HeapBlock *found = NULL;
    HeapBlock *block;

    for (block = gHeapStart[heap]; block != NULL; block = Heap_NextBlock(block)) {
        if (block->used == 0 && block->size >= size) {
            found = block;
            break;
        }
    }
    return found;
}

HeapBlock *Heap_FindFreeFromTail(s32 heap, s32 size);

HeapBlock *Heap_SearchFromTail(s32 size, s32 heap) {
    HeapBlock *block = NULL;
    s32 i;

    if (heap != HEAP_ANY) {
        block = Heap_FindFreeFromTail(heap, size);
    } else {
        for (i = HEAP_COUNT - 1; i >= 0; i--) {
            block = Heap_FindFreeFromTail(i, size);
            if (block != NULL) {
                break;
            }
        }
    }
    return block;
}

HeapBlock *Heap_FindFreeFromTail(s32 heap, s32 size) {
    HeapBlock *found = NULL;
    HeapBlock *block;

    for (block = Heap_PrevBlock(gHeapEnd[heap]); block != NULL; block = Heap_PrevBlock(block)) {
        if (block->used == 0 && block->size >= size) {
            found = block;
            break;
        }
    }
    return found;
}

#ifdef PORT
/* Debug help for this port.  The allocator walks its blocks by their own size fields
   (Heap_NextBlock), so one buffer that writes past its block tears the heap apart and the crash
   shows up much later, in an unrelated allocation (the Brazilian build showed it in StgFx_Init ->
   Heap_Alloc).  With BT3_HEAP_CHECK=1 every allocation and free walks every heap, and the first
   operation that finds it broken prints itself and the last one that was still fine.  Both are PC
   addresses: name them with the same build's map,
       python3 _bt4pc/tools/symbolize.py bt3.exe.map <the crash report>
   Off unless the variable is set, so a normal run pays nothing. */
static int gHeapCheckOn = -1;
static int gHeapCheckVerbose;
/* The last few operations, with their caller and its caller: the tear happens between two of them,
   and the culprit is the game code that ran in between, so the sequence names it. */
#define HEAP_HIST 16
static struct { const char *what; const void *c0; const void *c1; } gHeapHist[HEAP_HIST];
static int gHeapHistAt;
static int gHeapHistHave;

static void Heap_CheckAll(const char *what, s32 heap, s32 size, u32 align, s32 fromTail, const void *ret,
                          const void *caller1) {
    s32 h;

    if (heap != HEAP_ANY && (heap < 0 || heap >= HEAP_COUNT)) {
        fprintf(stderr, "bt3: HEAP ID OUT OF RANGE: %s asked for heap %d (0..%d, or %d for any), caller %p\n",
                what, (int)heap, (int)(HEAP_COUNT - 1), (int)HEAP_ANY, ret);
        fflush(stderr);
        exit(3);
    }
    for (h = 0; h < HEAP_COUNT; h++) {
        u8 *lo = (u8 *)gHeapStart[h];
        u8 *hi = (u8 *)gHeapEnd[h];
        HeapBlock *block = gHeapStart[h];
        s32 guard = 0;

        while (block != NULL) {
            if ((u8 *)block < lo || (u8 *)block + 0x20 > hi || block->magic != HEAP_MAGIC ||
                block->size < 0x20 || block->size > (s32)(hi - (u8 *)block) || ++guard > 200000) {
                int k;
                int w;
                fprintf(stderr,
                        "bt3: HEAP BROKEN in heap %d: %s (heap=%d size=%d align=%u tail=%d, its caller %p)\n",
                        (int)h, what, (int)heap, (int)size, align, (int)fromTail, ret);
                fprintf(stderr, "bt3: the block at %p: magic %08X (want %08X) size %d dataSize %d used %d\n",
                        (void *)block, (unsigned)block->magic, (unsigned)HEAP_MAGIC, (int)block->size,
                        (int)block->dataSize, (int)block->used);
                /* The bytes that were written over the header are the signature of whatever wrote them. */
                fprintf(stderr, "bt3: its first 48 bytes:");
                for (w = 0; w < 48; w++) {
                    fprintf(stderr, " %02X", ((u8 *)block)[w]);
                }
                fprintf(stderr, "\n");
                {   /* and the bytes just before it, often the run that ran over the end of the block */
                    int b;
                    fprintf(stderr, "bt3: the 16 bytes before the block:");
                    for (b = -16; b < 0; b++) {
                        fprintf(stderr, " %02X", ((u8 *)block)[b]);
                    }
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, "bt3: the last %d heap operations, oldest first (caller, and its caller):\n",
                        gHeapHistHave);
                for (k = 0; k < gHeapHistHave; k++) {
                    int j = (gHeapHistAt - gHeapHistHave + k + HEAP_HIST * 2) % HEAP_HIST;
                    fprintf(stderr, "bt3:   %-12s %p  %p\n", gHeapHist[j].what, gHeapHist[j].c0, gHeapHist[j].c1);
                }
                fflush(stderr);
                exit(3);
            }
            block = Heap_NextBlock(block);
        }
    }
    gHeapHist[gHeapHistAt].what = what;
    gHeapHist[gHeapHistAt].c0 = ret;
    gHeapHist[gHeapHistAt].c1 = caller1;
    gHeapHistAt = (gHeapHistAt + 1) % HEAP_HIST;
    if (gHeapHistHave < HEAP_HIST) {
        gHeapHistHave++;
    }
}

/* The same walk, callable from elsewhere in the port (the file path) to pin the exact line. */
void Heap_CheckNow(const char *what) {
    if (gHeapCheckOn < 0) {
        const char *e = getenv("BT3_HEAP_CHECK");
        gHeapCheckOn = e != NULL;
        gHeapCheckVerbose = e != NULL && e[0] == '2'; /* 2: also print every step, so the tail says how far it got */
        if (gHeapCheckOn) { fprintf(stderr, "bt3: heap check on (every allocation and free)\n"); fflush(stderr); }
    }
    if (gHeapCheckOn) {
        if (gHeapCheckVerbose) { fprintf(stderr, "bt3: heap ok through: %s\n", what); fflush(stderr); }
        Heap_CheckAll(what, HEAP_ANY, 0, 0, 0, 0, 0);
    }
}

#define HEAP_CHECK(tag, heap, size, align, tail, ret) do { \
        if (gHeapCheckOn < 0) { \
            const char *e = getenv("BT3_HEAP_CHECK"); \
            gHeapCheckOn = e != NULL; \
            gHeapCheckVerbose = e != NULL && e[0] == '2'; \
            if (gHeapCheckOn) { fprintf(stderr, "bt3: heap check on (every allocation and free)\n"); fflush(stderr); } \
        } \
        if (gHeapCheckOn) { \
            Heap_CheckAll((tag), (heap), (size), (align), (tail), (const void *)(ret), \
                          __builtin_return_address(1)); \
        } \
    } while (0)
#else
#define HEAP_CHECK(tag, heap, size, align, tail, ret) do { } while (0)
#endif

HeapBlock *Heap_AllocBlock(s32 size, u32 align, s32 fromTail, s32 heap) {
    HeapBlock *block;
    HeapBlock *free;
    s32 need;

    /* Also here: Heap_Alloc is small enough to be inlined into its caller, and then this return
       address is the game function, which is what we want to see. */
    HEAP_CHECK("block:enter", heap, size, align, fromTail, __builtin_return_address(0));
    if (size <= 0) {
        return NULL;
    }
    need = size + align + 0x24;
    if (fromTail == 0) {
        block = Heap_SearchFromHead(need, heap);
    } else {
        block = Heap_SearchFromTail(need, heap);
    }
    if (block == NULL) {
        return NULL;
    }
    if (block->dataSize - need > 0x80) {
        if (fromTail == 0) {
            need += Mem_Align4Pad((u32)block + need);
            Heap_InitFreeBlock((HeapBlock *)((u8 *)block + need), block->size - need);
        } else {
            free = block;
            need += Mem_Align4Rem((u32)block + (block->size - need));
            block = (HeapBlock *)((u8 *)block + (free->size - need));
            free->dataSize -= need;
            free->size -= need;
            *(s32 *)((u8 *)free + free->size - 4) = free->size;
        }
    } else {
        need = block->size;
    }
    Heap_InitUsedBlock(block, need, size, align, fromTail);
    HEAP_CHECK("block:leave", heap, size, align, fromTail, __builtin_return_address(0));
    return block;
}

void Heap_FreeBlock(HeapBlock *block) {
    HeapBlock *prev;
    HeapBlock *next;
    s32 merge = 0;

    Heap_InitFreeBlock(block, block->size);
    prev = Heap_PrevBlock(block);
    next = Heap_NextBlock(block);
    if (prev != NULL) {
        merge = prev->used == 0;
    }
    if (next != NULL && next->used == 0) {
        merge |= 2;
    }
    switch (merge) {
    case 0:
        break;
    case 1:
        prev->size += block->size;
        prev->dataSize += block->size;
        *(s32 *)((u8 *)prev + prev->size - 4) = prev->size;
        break;
    case 2:
        block->size += next->size;
        block->dataSize += next->size;
        *(s32 *)((u8 *)block + block->size - 4) = block->size;
        break;
    case 3:
        prev->size += block->size + next->size;
        prev->dataSize += block->size + next->size;
        *(s32 *)((u8 *)prev + prev->size - 4) = prev->size;
        break;
    }
}

void Heap_Init(void) {
    gHeapDebugValue = 0;
    gHeapDebugFlag = 0;
    gHeapDebugUnused = 0;
    Heap_Create();
}

void *Heap_Alloc(s32 size, u32 align, s32 fromTail, s32 heap) {
    HeapBlock *block;
    void *data = NULL;

    /* Here return_address(0) is the game's own call, which is what names the culprit: inside
       Heap_AllocBlock it would only be this function. */
    HEAP_CHECK("alloc:enter", heap, size, align, fromTail, __builtin_return_address(0));
    block = Heap_AllocBlock(size, align, fromTail, heap);
    if (block != NULL) {
        data = (void *)block->data;
    }
    HEAP_CHECK("alloc:leave", heap, size, align, fromTail, __builtin_return_address(0));
    return data;
}

void Heap_Free(void *ptr) {
    HeapBlock *block;

    HEAP_CHECK("free:enter", HEAP_ANY, 0, 0, 0, __builtin_return_address(0));
#ifdef PORT
    {
        /* PC build: a block that is not the heap's (a large stage buffer, battle_load.c) goes back to the port */
        extern int Port_GameBigFree(unsigned addr);
        if (Port_GameBigFree((u32)ptr)) {
            return;
        }
    }
#endif
    block = Heap_FindBlock((u32)ptr);

    if (block == NULL) {
        Heap_ReportBadFree();
    }
    Heap_FreeBlock(block);
    HEAP_CHECK("free:leave", HEAP_ANY, 0, 0, 0, __builtin_return_address(0));
}

void Heap_SetDebugValue(s32 value);
void Heap_SetDebugFlag(u8 flag);

void Heap_SetDebug(s32 value, u8 flag) {
    Heap_SetDebugValue(value);
    Heap_SetDebugFlag(flag);
}

void Heap_SetDebugValue(s32 value) {
    gHeapDebugValue = value;
}

void Heap_SetDebugFlag(u8 flag) {
    gHeapDebugFlag = flag;
}

/* Total bytes available in free blocks, across every heap. */
s32 Heap_GetFreeSize(void) {
    s32 total = 0;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 0) {
                total += block->dataSize;
            }
        }
    }
    return total;
}

/* Total bytes requested by live allocations, across every heap. */
s32 Heap_GetUsedSize(void) {
    s32 total = 0;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 1) {
                total += block->dataSize;
            }
        }
    }
    return total;
}

/* Size of the largest single free block. */
u32 Heap_GetLargestFree(void) {
    u32 largest = 0;
    HeapBlock *block;
    s32 i;

    for (i = 0; i < HEAP_COUNT; i++) {
        for (block = gHeapStart[i]; block != NULL; block = Heap_NextBlock(block)) {
            if (block->used == 0) {
                if (largest < (u32)block->dataSize) {
                    largest = block->dataSize;
                }
            }
        }
    }
    return largest;
}

s32 Heap_GetTotalSize(void) {
    return gHeapSize[0] + gHeapSize[1];
}

/* Called by Heap_Free for a pointer that is not a live block. The report itself was compiled out. */
void Heap_ReportBadFree(void) {
}

HeapBlock *Heap_GetStart(s32 heap) {
    return gHeapStart[heap];
}

HeapBlock *Heap_GetEnd(s32 heap) {
    return gHeapEnd[heap];
}

s32 Heap_GetSize(s32 heap) {
    return gHeapSize[heap];
}

s32 Heap_GetUnk70(s32 heap) {
    return gHeapUnk70[heap];
}

s32 Heap_GetUnk78(s32 heap) {
    return gHeapUnk78[heap];
}
