/* A stand-in for the PS2SDK's loadfile.h (see audsrv.h here): the two module loaders only. */
#ifndef STUB_LOADFILE_H
#define STUB_LOADFILE_H

extern int SifLoadModule(const char *path, int arg_len, const char *args);
extern int SifExecModuleBuffer(void *ptr, unsigned int size, unsigned int arg_len, const char *args, int *mod_res);

#endif
