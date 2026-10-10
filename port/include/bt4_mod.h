/* BT4 mod-build configuration.
 * Brazilian "DBZT4 1.8 (V2 commissioned)" for PS2.  Values measured against
 * PS2-DBZT4-1.8-TEST(V2委托版).ISO and the clean USA release (SLUS_216.78).
 * The mod's main executable is the USA ELF with 1571 bytes patched plus an
 * injected .extra section at 0x3BE720; BIN/DBZP.BIN differs by 1352 bytes.
 */
#ifndef BT4_MOD_H
#define BT4_MOD_H

/* The game's own programs, as unpacked under gamedata/disc/ . */
#define BT4_MAIN_ELF      "SLUS_219.22"
#define BT4_DBZP_BIN      "BIN/DBZP.BIN"
#define BT4_FLAT_SHA1     "7e8398621de3cf458755109e59704a20d60fc082"
#define BT4_DBZP_SHA1     "c31deddb3df9048a9274185629fcb8c10b5ce61d"

/* The section the mod injects with ee-forge (code only: hooks live here). */
#define BT4_EXTRA_BASE    0x3BE720u
#define BT4_EXTRA_SIZE    0x6000u

/* Archives: bt3pus1 = 1 entry, bt3pus2 = 15013, bt3pus3 = 65201.
 * (USA: pzs3us0 = 1, pzs3us1 = 3399, pzs3us2 = 65201.)
 * NOTE: the exact id->(partition,index) rule the mod uses lives in its
 * injected mapper at 0x3BE720/0x3BE740; the value below is the plain
 * count-based guess and is verified against that mapper before use. */
#define BT4_PT1_COUNT     15013
#define BT4_PT2_FIRST_ID  (1 + BT4_PT1_COUNT)   /* 15014 = 0x3AA6 (to be confirmed) */

#endif /* BT4_MOD_H */
