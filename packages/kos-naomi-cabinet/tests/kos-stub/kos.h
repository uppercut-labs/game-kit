/* A stand-in for <kos.h>, for the host test of src/kos.c only. It declares the few names the glue uses; the test
 * defines the functions as fakes. It is not KallistiOS and proves nothing about it beyond the glue's own mapping. */
#ifndef STUB_KOS_H
#define STUB_KOS_H

#include <stdint.h>

#define _arch_sub_naomi 1

typedef struct maple_device maple_device_t;
#define MAPLE_FUNC_MIE 0x01000000u

maple_device_t *maple_enum_type(int n, uint32_t func);
void *maple_dev_status(maple_device_t *dev);

/* the glue only compares these; the numbers here are the stand-in's own */
#define HW_TYPE_RETAIL 1
#define HW_TYPE_NAOMI 2
#define HW_TYPE_OTHER 3

#endif
