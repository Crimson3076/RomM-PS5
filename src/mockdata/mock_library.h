/* Static mock PS5 library data used by the mock RomM API backend and by the
 * Milestone 1 UI smoke test. Not real game data. */
#ifndef ROMM_PS5_MOCK_LIBRARY_H
#define ROMM_PS5_MOCK_LIBRARY_H

#include "romm_api/romm_api.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const RommGame MOCK_PS5_GAMES[];
extern const size_t MOCK_PS5_GAMES_COUNT;

#ifdef __cplusplus
}
#endif

#endif /* ROMM_PS5_MOCK_LIBRARY_H */
