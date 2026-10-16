# Good Egg (EggStarGalaxy 1): Wii vs PS2 port with the same inputs

2026-10-15. Both games are driven by one input timeline (`tools/dolphin/play/moves.json`): frames `g` at 60 Hz counted
from the first frame Mario is free. Result: `videos/2026-10-15_cmp-goodegg-moves.mp4` (videos/ is not in git) —
Wii (Dolphin 2609a) on the left, PS2 port (PCSX2) on the right; audio left channel Wii, right channel PS2.

## How it is made

| Step | Wii (Dolphin, `D:\dolphin`, own User folder `D:\dolphin-script\User`) | PS2 port |
|---|---|---|
| Input | `python tools/replay.py dtm play.json out.dtm`: a .dtm movie from boot (tools/dolphin/dtm.py: one entry per Wiimote poll, 187.5 Hz; Wiimote + Nunchuk) | `python tools/replay.py bin play.json out/replay.bin` (u16 buttons, s8 sticks per logic step) |
| To the level | the movie presses A+B at the title, points at file 1 (the RMGE01 121-star save), Play This File; the Gecko code `tools/dolphin/RMGE01.ini` turns the scene request {Game, AstroGalaxy} at 809A9148 into EggStarGalaxy 1 | the build's scenario |
| g = 0 | timeline frame `CONTROL_T` (4155) in `tools/replay.py` | `build.sh replay ...`: pad.c reads host:replay.bin from the first frame Mario is grounded after the ScenarioStarter flight |
| Run | `tools/dolphin/play.ps1 out.dtm 170 -Out dir` (window off screen, frame + audio dump) | `tools/record.ps1 name out/slice-packs-replay.elf 40` |

Notes: Dolphin's AVI is variable frame rate (its timestamps are VI time, frames are skipped in loads), so cut it by
time (`-ss`), not by frame index. The DSP audio dump and the video end together: audio time = video time + (audio
duration - video duration). Mapping Wii -> PS2: A CROSS, B L1, Z R1, shake SQUARE, D-pad, stick. Pointer calibration
(Dolphin's emulated IR): cursor x = 0.5 + 0.88 px, y = 0.32 - 0.6 py.

## Differences seen (moves.json, g 0..1230)

1. **HUD**: the Wii draws the star counter, LIFE meter, lives, coins and star bits; the port only its debug text.
   The "Welcome to the Galaxy!" banner and the galaxy / scenario name plates of the intro are missing too.
2. **Scenario start**: the Wii runs the intro camera tour with the galaxy name (~20 s) before the flight in; the port
   flies Mario in at once. Mario lands in the middle of the stone circle on the Wii; in the port he ends just north
   of it. From there on the same stick input takes him on a parallel but shifted path.
3. **Path divergence** (a result of 2 and of collision): by g = 200 the port's Mario walks into the rim of the
   flower bed and gets slowed by it; the Wii's passes beside it. Later (g ~ 700) the port's Mario reaches the house
   and the A press reads its sign (a talk), the Wii's is elsewhere: the two runs never meet again.
4. **Pointer**: the port always draws the star pointer (green star, at screen centre); the Wii shows it only when the
   remote points at the screen (the movie leaves IR off in play).
5. **Look**: the Wii's sun has its lens flare / bloom, the port none; the port's colours are flatter (lighting, no
   specular), the sky backdrop lighter; Takobo pale.
6. **Same**: the start camera (behind Mario facing the house), the planet, house, flower beds, Takobo placement and
   their chase, Mario's run speed and jump heights look alike while the paths are close (g 90..200).

## Next (to make the runs match)

- The flight's landing point (ScenarioStarter path end / Mario's landing): he must stop on the stone circle.
- The HUD (CounterLayoutController: PowerStarCounter, CoinCounter, StarPieceCounter, PlayerLife / LifeMeter, stock
  counter) and the scenario intro (GalaxyNamePlate, ScenarioTitle, "Welcome").
- Collision of the flower beds' rims (compare with KCL / Mario's step-up height).
- Hide the pointer while it is not used.

## Second run (after the fixes, 2026-10-15 evening): videos/2026-10-15_cmp-goodegg-moves2.mp4

Both in Spanish now (the movie's SYSCONF language 4, the port's UsSpanish). Fixed since the first run: the landing
after the ScenarioStarter flight (Mario stops at the path's end on the stone circle, LandScenarioStart, input
locked while it plays), the HUD (counters, ENERGÍA meter, show / hide rules), the opening camera with the galaxy and
scenario names, "¡Bienvenido a la Galaxia!", the pointer hidden while unused.

Result with the same inputs: the two Marios walk the same path from g 0 to ~300 (past the flower bed, into the
bushes), drift apart by up to a few metres between 300 and 700, and end at the same place facing the same way at
g 1000-1230. Left:

1. **Takobo hits**: around g 800 the port's Mario is hit by a Takobo (ENERGÍA 2); the Wii's is not.
2. **Sky**: the Wii's backdrop is a dark blue gradient with a darker sea of clouds; the port's sky texture is
   lighter and streakier (render the sky's materials / layers as the Wii does).
3. **Start camera at the landing**: the Wii shows the planet from far (startStartPosCamera) while the welcome text
   is up; the port's camera is already close behind Mario.
4. **Control after the landing**: the port gives control ~90 frames after touching down; the Wii a little sooner
   (its welcome text is still fully up at g 29), so the HUD shows earlier in the port.
5. Star counter 0 (no save) against the Wii save's 121; no sun flare / bloom in the port.

## Exact comparison through the Wii's RAM (2026-10-15 night)

`tools/dolphin/probe.ps1 -Play play.json -G g -Read <addresses>` plays the movie up to frame g (Dolphin pauses at
the movie's end) and reads float triples from the Wii's RAM (`dolmem.py read3`; `-Near "x,y,z" -R r` lists the triples
near a place to find addresses). In this movie (Spanish, moves.json) Mario's position is at 810B59A8 and the camera's
eye at 8087B1B8. The port logs both every 5 frames in `replay` builds. Findings and fixes, all after Petari:

| g | Wii Mario (x, z) | port before | port after the fixes |
|---|---|---|---|
| 120 | -3386, -15437 | -3382, -15432 | same |
| 200 | -4087, -16156 | -4090, -16160 | same |
| 215 | -4103, -16327 | -4123, -16332 | -4124, -16332 |
| 280 | -3342, -16362 | -3329, -16414 | -3367, -16383 |
| 300 | -3224, -16274 | -3119, -16264 | -3248, -16299 |

- The stick's frame: Mario::calcMoveDir uses the camera's view axes on an eased up (_398), not the camera-to-Mario
  direction (the running turn was too wide).
- The ground spin halves the target walk speed (updateWalkSpeed's mTornadoMultiply while MarioMagic runs).
- The Wii sees a shake as a swing ~5 frames after it starts: replay.py presses the PS2 spin button that much later.
- Left: the first 15 frames of a running turn are ~1.5 frames behind the Wii's (~20 units), carried on after it.

## Third run (after the camera's height arrange): videos/2026-10-15_cmp-goodegg-moves3.mp4

Cut so that g 0 is at 1.0 s on both sides: Wii AVI -ss 68.90 (g 0 at 69.90), PS2 capture
(videos/2026-10-15_2256_cmp-moves3-ps2_6f1893d.mp4) -ss 13.83 (g 0 at 14.83; found by the motion onset at g 90).

- The camera follows the triple jump's rise as the Wii's (height within ~20 units at g 350, was 79).
- Mario within ~15-20 units of the Wii's to g 400; then ~95 by g 450: the camera's horizontal swings away from
  the Wii's after the second triple-jump landing (Mario faces ~3 degrees off at g 390, and constrainAngleY turns
  the camera the other way near 180 degrees). From there on the run drifts (by g 900 Mario is elsewhere).
- Missing: the spin's sparkle trail (Wii effect on the ground spin), sun flare, cloud scroll.

## Fourth run (2026-10-16 night): videos/2026-10-16_cmp-goodegg-final.mp4

Cut: Wii AVI (tools/dolphin/out/moves4) -ss 68.91, PS2 (videos/2026-10-16_0045_cmp-final-ps2_9655cc0.mp4) -ss 14.15.
moves.json now holds the stick at (0.4, -1) from g 320: straight back (0, -1) runs Mario at the camera, where
CameraFollow's constrainAngleY aims 180 degrees round and the Wii (179.85) and the port (180.11) turned opposite
ways from a tenth of a degree; on the diagonal both turn the same way.

Fixed after the RAM probes:
- The actors move during the scenario opening (GameScene::exeScenarioOpeningCamera runs executeMovementList), and
  already while the stage finishes loading (GameScene starts in that nerve, its camera state waiting):
  OPENING_PREROLL 156 frames, measured on the Takobo patrols (now within ~3 units of the Wii's at g 0 / 40).
- Walkers clip as ClippingJudge: a sphere of 300 (ClippingActorInfo's default) off the view with its near plane at
  500 and far at 10000; Takobo 6 freezes at the Wii's exact place (-4977, -16402).
- SpinLight follows Mario's HandL while the spin clips run (MarioAnimator spinEntry / spinUpdate, MarioMagic: frame
  25 ends it in SpinLightBurst).

Same path to g ~780; at the house Mario and the Takobo meet in both, the PS2's spin hits it first (a star piece)
while on the Wii it punches Mario: a frame or two of difference, the runs part there.

Cutting a new side by side: `python tools/dolphin/align.py <wii avi> <ps2 mp4> 69.91 15 19` prints the PS2 capture's
g 0 (motion correlation from g 90; 0.9 and over is a match); cut the Wii at 68.91 and the PS2 one second before its
g 0. videos/2026-10-16_cmp-goodegg-final2.mp4 (build 6158efa: Mario's smokes, the spin's swirl, the lens flare) is
cut that way (PS2 -ss 14.50).
