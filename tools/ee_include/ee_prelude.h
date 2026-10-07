// Force-included after galaxy-macos prelude.h: names the decomp uses that newlib/GCC-MIPS define as macros.
#pragma once
#include <ctype.h>
#undef _U
#undef _L
#undef _N
#undef _S
#undef _P
#undef _C
#undef _X
#undef _B
// GCC rejects attributes after a function definition (clang accepts); NO_INLINE becomes a hint we drop.
#ifdef __cplusplus
#include <Inline.hpp>
#undef NO_INLINE
#define NO_INLINE
#endif
