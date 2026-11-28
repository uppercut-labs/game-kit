/* A stand-in for the PS2SDK's iopcontrol.h (see libpad.h here): the IOP reset and sync only. */
#ifndef STUB_IOPCONTROL_H
#define STUB_IOPCONTROL_H

extern int SifIopReset(const char *arg, int mode);
extern int SifIopSync(void);

#endif
