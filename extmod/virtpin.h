#ifndef MICROPY_INCLUDED_EXTMOD_VIRTPIN_H
#define MICROPY_INCLUDED_EXTMOD_VIRTPIN_H

#include "py/obj.h"

typedef struct _mp_obj_pin_obj_t {
    mp_obj_base_t base;
} mp_obj_pin_obj_t;

typedef struct _mp_pin_p_t {
    mp_uint_t (*ioctl)(mp_obj_t obj, mp_uint_t request, uintptr_t arg, int *errcode);
} mp_pin_p_t;

#endif