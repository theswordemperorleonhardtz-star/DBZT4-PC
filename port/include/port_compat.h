/*
 * Forced into every game source of the PC build (-include port_compat.h), in front of the game's own headers.
 * The game sources under src/ and include/ are shared with the matching decompilation and are not edited for the
 * port unless there is no other way; differences between the PS2 compiler and a host compiler are bridged here.
 */
#ifndef PORT_COMPAT_H
#define PORT_COMPAT_H

#define PORT 1

/* This fork builds the PC port for the Brazilian "DBZT4 1.8 (V2)" disc instead
 * of the clean USA release.  It adds guarded branches to the shared game sources
 * (roughly 54 blocks in 20 files: the file-id map in include/sys/file.h and
 * src/sys/file.c, the resource helpers of src/battle/*, the menu rules of
 * src/menu/*), plus the new headers port/include/bt4_mod.h and the modded file
 * layer; removing this define builds the USA path again.  What each branch ports
 * and the measured evidence are in _bt4pc/worklist/DBZQ_OVERLAY.md,
 * _bt4pc/worklist/EXTRA_SEMANTICS.md and _bt4pc/worklist/FILE_ID_MAPPING.md. */
#define BT3_MOD_DBZT4 1

/* The game's types: on the PS2 `long` is 64 bits. Take over include/types.h. */
#define TYPES_H
typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef long long s64;
typedef unsigned long long u64;
typedef float f32;

/* Functions the matching build takes from assembly: the port supplies them elsewhere (see docs/port/). */
#define INCLUDE_ASM_H
#define INCLUDE_ASM(FOLDER, NAME)
#define INCLUDE_RODATA(FOLDER, NAME)
#define RODATA_ALIGN16()
#define LIT4_WORD(NAME, HEX)
#define ASM_STUB_BEGIN() extern int asm_stub_begin_
#define ASM_STUB_END() extern int asm_stub_end_

/* C library calls of the game that must not go to the host's versions (see port/src/plat_mem.c). */
#define malloc Port_Malloc

#endif
