#ifndef ROMM_PS5_ROMM_API_MOCK_H
#define ROMM_PS5_ROMM_API_MOCK_H

#include "romm_api/romm_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fills `out` with a mock backend bound to the built-in fixture library
 * (mockdata/mock_library.h). Safe to call repeatedly; holds no external
 * resources. */
void romm_api_mock_init(RommApi *out);

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_ROMM_API_MOCK_H */
