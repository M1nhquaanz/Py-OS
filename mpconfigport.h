#ifndef MICROPY_INCLUDED_MPCONFIGPORT_H
#define MICROPY_INCLUDED_MPCONFIGPORT_H

#include <stdint.h>
#include <stddef.h>

typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;

#define MICROPY_ENABLE_COMPILER     (1)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_NONE)

#define MICROPY_PY_SYS             (1)
#define MICROPY_PY_BUILTINS_HELP   (1)
#define MICROPY_PY_ARRAY           (1)
#define MICROPY_PY_COLLECTIONS     (1)
#define MICROPY_PY_CMATH           (0)
#define MICROPY_PY_MATH            (0)
#define MICROPY_PY_STRUCT          (1)
#define MICROPY_PY_IO              (0) 

#undef MICROPY_PY_SYS_PLATFORM
#define MICROPY_PY_SYS_PLATFORM     "pyos"

#undef MICROPY_PLATFORM_COMPILER
#define MICROPY_PLATFORM_COMPILER   "GCC " __VERSION__

#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME       "PyOS Baremetal Board"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME         "i386"
#endif

#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

#define MICROPY_ALLOC_PATH_MAX      (128)

#define MICROPY_PORT_ROOT_POINTERS \
    const char *readline_hist[8];

#endif // MICROPY_INCLUDED_MPCONFIGPORT_H