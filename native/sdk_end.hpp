// Included after the Source SDK headers: under GCC they redefine standard names
// (tier1/strtools.h turns isdigit and its family into errors, tier0/platform.h makes
// offsetof a null-pointer expression that is no constant). MSVC builds see neither.
#ifndef _WIN32
#undef isdigit
#undef isalpha
#undef isalnum
#undef isprint
#undef isxdigit
#undef ispunct
#undef isgraph
#undef isupper
#undef islower
#undef iscntrl
#undef isspace
#undef offsetof
#define offsetof(type,member) __builtin_offsetof(type,member)
#endif
