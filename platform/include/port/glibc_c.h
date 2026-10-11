// Force-included into the C sources on Linux (CMakeLists.txt), after
// port/compat.h.  glibc's <math.h> declares __fabs and __fabsf, the names of
// two PowerPC compiler intrinsics that port/ppc_intrinsics.h defines as
// static inline functions for the decompiled code.  In C a static definition
// after glibc's extern declaration keeps external linkage (clang's
// -fms-extensions accepts the redeclaration), so every C file exported its
// own copy and the link failed on duplicates.  Here <math.h> comes first and
// the intrinsics get names of their own.  (In C++ the same definitions are
// inline functions, which the linker merges.)
#pragma once

#include <math.h>

#define __fabs port_fabs
#define __fabsf port_fabsf
