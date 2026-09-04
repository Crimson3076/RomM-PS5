#include "mockdata/mock_library.h"

/* Fictional placeholder entries only — never real game titles or metadata.
 * Used to exercise the UI and pagination logic before a real RomM server
 * is connected (Milestone 2). */
const RommGame MOCK_PS5_GAMES[] = {
    {1, "Sample Adventure", "CUSA00001", "1.02", ROMM_FORMAT_FOLDER,
     42ULL * 1024 * 1024 * 1024, true},
    {2, "Demo Racer", "CUSA00002", "1.00", ROMM_FORMAT_FOLDER,
     58ULL * 1024 * 1024 * 1024, true},
    {3, "Placeholder Quest", "CUSA00003", "", ROMM_FORMAT_FFPKG,
     71ULL * 1024 * 1024 * 1024, false},
    {4, "Test Arena Online", "CUSA00004", "1.15", ROMM_FORMAT_FOLDER,
     35ULL * 1024 * 1024 * 1024, true},
    {5, "Example Puzzle Co-op", "CUSA00005", "1.03", ROMM_FORMAT_EXFAT,
     18ULL * 1024 * 1024 * 1024, true},
    {6, "Fixture Platformer Deluxe", "", "", ROMM_FORMAT_FOLDER,
     26ULL * 1024 * 1024 * 1024, false},
    {7, "Mockup Battle Royale", "CUSA00007", "2.01", ROMM_FORMAT_FOLDER,
     93ULL * 1024 * 1024 * 1024, true},
    {8, "Sandbox City Builder", "CUSA00008", "1.40", ROMM_FORMAT_FFPFS,
     64ULL * 1024 * 1024 * 1024, true},
};

const size_t MOCK_PS5_GAMES_COUNT =
    sizeof(MOCK_PS5_GAMES) / sizeof(MOCK_PS5_GAMES[0]);
