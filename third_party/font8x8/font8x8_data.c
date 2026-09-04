/* Provides the actual storage for font8x8_basic[] — defined directly (no
 * `static`/`extern`) in the vendored header itself, see font8x8_basic.h —
 * in its own translation unit, isolated from this project's own
 * -Wconversion/-Wsign-conversion flags. That header's own initializer
 * values (e.g. 0xFF into `char`, which is signed on this project's
 * targets) trigger those warnings; it's vendored verbatim (see
 * third_party/THIRD_PARTY_LICENSES.md) rather than something to edit, so
 * this dedicated file — not src/ps5/font_render.c, which has its own code
 * that should stay under the strict flags — is what gets the relaxed
 * build options in CMakeLists.txt.
 */
#include "font8x8_basic.h"
