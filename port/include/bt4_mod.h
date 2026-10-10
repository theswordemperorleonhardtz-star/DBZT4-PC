/* BT4 mod-build configuration.
 * Target: Brazilian "DBZT4 1.8 (V2 commissioned)" PS2 disc
 *         PS2-DBZT4-1.8-TEST(V2委托版).ISO  (volume PS2_DBZT4_1_8_TEST_V2I_)
 *
 * Measured against the clean USA release (SLUS_216.78):
 *   SLUS_219.22 : the USA ELF with 1571 bytes patched (0.08%) plus an injected
 *                 .extra section (0x6000 bytes at 0x3BE720, "ee-forge").
 *   BIN/DBZP.BIN: same size (563996), 1352 bytes different.
 *   DATA/       : BT3PUS1.AFS (1 entry), BT3PUS2.AFS (15013), BT3PUS3.AFS (65201),
 *                 BT3PUS.DIR.  The USA has PZS3US0/1/2 (1 / 3399 / 65201).
 */
#ifndef BT4_MOD_H
#define BT4_MOD_H

/* The game's own programs, as unpacked under gamedata/disc/ . */
#define BT4_MAIN_ELF      "SLUS_219.22"
#define BT4_DBZP_BIN      "BIN/DBZP.BIN"
#define BT4_FLAT_SHA1     "7e8398621de3cf458755109e59704a20d60fc082"
#define BT4_DBZP_SHA1     "c31deddb3df9048a9274185629fcb8c10b5ce61d"

/* The section the mod injects with ee-forge; all of its hooks live here. */
#define BT4_EXTRA_BASE    0x3BE720u
#define BT4_EXTRA_SIZE    0x6000u

/* ---------------------------------------------------------------------------
 * File-id mapping, decoded from the replacement File_OpenById the mod put in
 * .extra (0x3BE720 partitionOf, 0x3BE740 indexIn, 0x3BE758 openById).  The
 * four USA callers (File_LoadSyncEx, File_Request, File_UpdateRequests,
 * File_GetSize) and the two ADX ones (Adx_Play, Adx_PlayPaused) all jump there.
 *
 *   id <  0xD48              -> partition 1, index = id - 1
 *   0xD48 <= id <= 70000     -> partition 2, index = id - 0xD48     (USA rule kept)
 *   id >  70000              -> partition 1, index = id - 66640     (mod content)
 *
 * The old USA boundary (0xD48) is therefore NOT changed; only the high range
 * above 70000 is new.  It addresses the 11614 entries the mod appended to
 * BT3PUS2 (indices 3399..15012).
 * ------------------------------------------------------------------------- */
#define BT4_PT1_FIRST_ID      1u
#define BT4_PT2_FIRST_ID      0xD48u      /* 3400, unchanged from the USA release */
#define BT4_BIG_ID_FIRST      70000u      /* ids above this go to partition 1     */
#define BT4_BIG_ID_BASE       66640u      /* index = id - this                    */
#define BT4_PT1_COUNT         15013u

#endif /* BT4_MOD_H */
