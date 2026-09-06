/* Upstream's reader-only configuration retains two unused writer helpers.
 * Keep vendor sources intact and scope Clang's exception to this one unit. */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "vendor/miniz/miniz_zip.c"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
