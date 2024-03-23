#ifndef ERRNO_H_
#define ERRNO_H_

#ifndef __error_t_defined
typedef int error_t;
#define __error_t_defined 1
#endif

#include <sys/errno.h>

#define ERTOS (__ELASTERROR + 1)
#define ERESOURCE (ERTOS + 2)

extern error_t errno_from_rtos(int rtos);

#endif
