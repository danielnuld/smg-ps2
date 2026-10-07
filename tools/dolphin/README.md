# Measuring SMG heaps in Dolphin (phase 0c)

1. Save: a 121-star `data.bin` (any region); `python unpack_wii_save.py data.bin out/` decrypts it (SD key/IV from
   Dolphin's source) into `GameData.bin` + `banner.bin`; copy both to
   `D:\dolphin\User\Wii\title\00010000\524d4745\data\` (RMGE). A PAL (RMGP) save loads fine in RMGE01.
2. Dolphin (portable, `D:\dolphin`): `WiimoteNew.ini` A = K, B = J, shake = H; `[Input] BackgroundInput = True`.
3. `powershell -File measure.ps1 <Stage> <Scenario> <tag>`: boots the game, presses through title and file 1, runs
   `dolmem.py warp` (rewrites the next SceneControlInfo {"Game", "AstroGalaxy"} that "Play This File" requests,
   Petari GameSequenceProgress.cpp:174-179, GameSystemSceneController.hpp:17-31) and dumps MEM1/MEM2 30 s later.
4. `python ../heapreport.py dumps/<tag>/mem1.raw dumps/<tag>/mem2.raw <label>` names every heap through the game's
   HeapMemoryWatcher table.

`dolmem.py` reads Dolphin's emulated RAM with ReadProcessMemory: MEM1 = mapped region starting with the game id,
MEM2 = the next mapped region of at least 64 MB. Window coordinates in `measure.ps1` assume the default 818×647
render window.
