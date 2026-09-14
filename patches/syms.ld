/*
 * Addresses for functions the patches call that have no MIPS code.
 *
 * N64Recomp treats a zero-sized symbol between 0x8F000000 and 0x90000000 as a
 * manually placed patch symbol, and a call to one becomes a direct call to the
 * host function of the same name. Each needs a C++ definition in the port with
 * the recompiled signature, void name(uint8_t* rdram, recomp_context* ctx).
 * The addresses only have to be unique.
 */

/* libultra, reimplemented by the runtime (see the #defines in patches.h). */
osSendMesg_recomp = 0x8F000000;
osRecvMesg_recomp = 0x8F000004;
osViSwapBuffer_recomp = 0x8F000008;
osGetTime_recomp = 0x8F00000C;
osWritebackDCache_recomp = 0x8F000010;
osInvalDCache_recomp = 0x8F000014;

/* The port's own, in src/patch_host.cpp. */
pw64_frame_presented = 0x8F001000;
