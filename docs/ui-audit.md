# UI and game flow audit against Petari

Petari's `src/Game/Screen/` (and the flow classes of `src/Game/System/` / `src/Game/Scene/`) against the port:
`engine/src/menu.c` (lyt_* layouts from the menus' packs, tools/lyt.py), `engine/src/game.c` (the HUD's counters from
tools/layout.py's quads), `engine/src/sequence.c`, `engine/src/gamedata.c`, `engine/src/main.c`.

Verdicts: **ok** the class was read against the port in this audit and matches (what is left out is named);
**fixed #n** differences fixed under that issue; **issue #n** not ported, an issue is open; **Wii-only** nothing to
port on the PS2; **ported, not re-read** the port has it (its own header comment cites Petari) but this audit has not
gone through it yet: the next pass starts there.

**ok by names** (the second pass, 2026-11-19): every SE / BGM name, every animation name and every message id the
Petari class spells is spelled in the port's code for it (scratch script: the names of the .cpp against menu.c /
game.c / sequence.c); the nerves' order and frames were not read line by line. What the check found is in the row.

Started 2026-11-18. Rows are in the order seen most. The fixes under #640, #641, #658 and #659 are PR #685.

## The HUD

| Petari class | Port | Verdict |
|---|---|---|
| CounterLayoutController | game.c hud_update | fixed #659: isPlayerMoving counts "off the ground" only out of the spring / the Boo / the water; rest ok (cJudgeNotMovingFrame 60, cShowWaitFrame 240, the Tico eat mode) |
| CounterLayoutAppearer | game.c hud_ap / hud_draw, menu.c pms_* | ok (Appear / End over the animation's 21 frames, ease out / in from the appear offset) |
| CountUpPaneRumbler | game.c hud_anim, menu.c | ok (freq 2, amp 8, 15 frames) |
| CoinCounter | game.c hud[1] | fixed #659: Wait ends by isWaitToDisappearCounter (240 frames or a pointer guidance), not by a bind or a talk. Left out: CoinCounterLight effect |
| StarCounter | game.c hud[0] | ok |
| PlayerLeft | game.c hud[3] | ok |
| PlayerMissLeft | menu.c pms_* | fixed #640: was missing (the count down over the black screen, SE_SY_PLAYER_LEFT_DEC). The next scene's load starts when it is dead (not concurrent) |
| OneUpBoard | menu.c oub_* | fixed #658: was missing; with it MR::requestOneUp's SE_SY_1UP on the coins' 1-up and ScenePlayingResult::incStarPiece's 1-up every 50 star pieces. Left out: OneUpGet effect |
| StarPieceCounter | game.c spc_* | ok (its modes, cHideFrameMin 3 / 20, the base offsets -94 / -120, the fast count down). Left out: the light effects; the Tico eat mode's guidance frame check (no pointer frame on the PS2) |
| PurpleCoinCounter | game.c pc_* | ok. Left out: PurpleCoinCounterLight effect |
| MarioMeter / MeterLayout | menu.c hm_* | ok (its twelve nerves; sAppearTime 120, sWaitStartTime 240, sBreakDelayTime 40 with SE_SY_LIFE_PLATE_DEC, sPowerUpWaitTime 60, sRecoveryCountInterval 5, sMoveToBasePositionTime 25, the alpha 0.3 .. 1.2, Danger's rates 4 / 2). Left out: the Break / Break123 effects |
| SuddenDeathMeter | menu.c hm_* (hm.sudden) | fixed #662: MarioMeter's other meter with a life of 1; MarioActor's life of 1 under the Dark comet and requestForceAppearHPMeter at the welcome came with it |
| MarioSubMeter / SubMeterLayout | menu.c subm_* | ok on its states (Appear, Wait, Danger under 0x40 with its rate, End after 60 frames full at rest); fixed #707: placed from getPlayerPos (it was Mario's centre) |
| TimeLimitLayout | menu.c tl_* | ok (sTimingTable, the scale 8 frames / 0.5, the count sounds at 600 / 360 / 120, TimeLimit at 600 left). Its Fadeout nerve is left out: the port takes DisplayModeOnNormal as always on (the comet timer's) |
| NoteCounter | game.c (its count and sounds) | ok: it has no layout (no initLayoutManager in RMGE01): nothing to draw |
| BombTimerLayout | menu.c bt_* | fixed #662 (the layout and its nerves). Its hosts are not ported: issue #732 |
| FullnessMeter | game.c (StarPieceTargetMeter / Counter) | ok by names. Left out: PicFrameShine's Shine animation (the meter is drawn from quads) |
| ScenarioTitle | game.c stitle | ok on its nerves (Appear, Wait, End); the text is drawn by game.c and Appear / End are its pane's 30-frame fade, not played from the brlan: not checked against the animation's curves |
| InformationMessage / InformationObserver | game.c info_label / info_text, io_* (quads, no Appear / End), menu.c (the result's and the staff roll's) | fixed #767: the observer's LifeUp and OneUp types (the first life-up mushroom, the first 1-up) were missing; the rest ok by names |
| CinemaFrame | menu.c ci_* | ok by names |
| GalaxyCometScreenFilter | menu.c csf_* (off) | issue #661: the layout is a filter that multiplies the frame (light colours over the whole screen, dark ones in the corners); lyt.c blends by alpha or adds and drew it as an opaque blob, so the code is in but off and the layout is not in the packs. It needs a multiply blend |
| MarioLauncherLayout (SimpleLayout "Sight") | menu.c sgt_* | fixed #659: Sight.arc was in no pack; it is menus.bin's now (4 KB: no scene repack) |
| StarPointerLayout / Blur / Controller / Director | menu.c sp_* | ported, not re-read (the Wii pointer as the right stick's cursor) |
| StarPointerGuidance | menu.c spg_* | ok by names. SE_SY_CS_NOTICE_USE_DPD: Wii-only |
| PlayerActionGuidance, SurfingGuidance | - | Wii-only (pictures of the Wii remote's motions): to decide with the user (plan-complete-game.md) |

## Menus

| Petari class | Port | Verdict |
|---|---|---|
| PauseMenu | menu.c pm_* | fixed #659: SE_SY_PAUSE_OFF at the lower button's decide; exeGameDataSave back to Selecting when the save failed. Left out: CS_CLICK_CLOSE (the Wii remote's speaker), Minus as a second close button |
| GamePauseSequence | menu.c menu_update | ok by names |
| ButtonPaneController | menu.c btn_* | ok (its nine nerves, calcPointingAnimStartFrame, cDecidedToDisappearAnimRate 1.5; the decide trigger is the pad's X) |
| YesNoController | menu.c yn_* | ok. Left out: the LeftText / RightText effects, CS_CLICK_CLOSE; setSE replaces the three sounds together |
| YesNoLayout | menu.c ynl_* | ok by names |
| SysInfoWindow | menu.c si_* | fixed #679: its key windows' IconAButton (open in appear, term in Disappear, drawn with the window). WindowType_Mini not used |
| IconAButton | menu.c iab_* | ok (Open / Wait / Term, openWithoutMessage, the follow pane) |
| BackButton | menu.c (ss.back, fsl.back, gm.back, pb.back) | ok (SE_SY_BUTTON_CURSOR_ON on the pointer, SE_SY_GALAXY_DECIDE_CANCEL once) |
| GalaxySelectBackButton | menu.c dmu_back_* | ok by names |
| ScenarioSelectLayout | menu.c ss_* | ok on its eleven nerves, their constants (cDecideFrame 40, the End at 60, the white fade at 180 / 30, 210, the comet warning 15 / 180, the cancel's 90) and sounds (SE_SY_DECIDE_1, SE_DM_LV_SENARIO_SEL_FLY, SE_SY_COMET_WARNING_DISP / WARNING at step % 80 == 25, BGM_SENARIO_SEL_2). Not re-read: ScenarioSelectStar (sst_*), the pane placement, the selected star's effects |
| GalaxySelectInfo, GalaxyConfirmLayout, GalaxyNamePlate, GalaxyInfoLayoutSetter | menu.c dmu_* | ok by names. GalaxyNamePlate's lower variant (GalaxyNamePlateU, PicBeakU: the map's dome icons) is not used: the port has no dome name plates |
| GalaxyMapController | menu.c gm_update | ok on its nerves' constants (sDisplayKeepFrame 30, sMapFadeinoutFrame 12, the Tico comet's 60 / 60 white fade) and sounds (SE_SY_GALAMAP_PAGE_CHANGE with CH_PITCH_UP / DOWN, the three SE_SY_LV_GALAMAP_BLINK_*, SE_DM_TICOCOMET_MORPH_WIPE_OUT). The domes have no map (map.bin is the Observatory's): issue #663 |
| GalaxyMap, its icons, GalaxyMapTitle, GalaxyMapSelectButton | menu.c gm_* | ok by names |
| PowerStarList | menu.c gm_list_* | ok on its nerves (Appear, Wait, PageNext, PagePrev with SE_SY_GALAMAP_SCROLL; its Disappear is never asked: the controller kills it under the wipe). Its Capture button, TxtPhotoPage, the message board picture: Wii-only |
| LuigiLetter, PeachLetter, BrosButton | menu.c llt_* / plt_* / bros | ok by names |
| PictureBookLayout, PictureBookCloseButton | menu.c pb_* | ok by names |
| ProloguePictureBook, PrologueLetter | menu.c pbk_* / prl_* | ok by names |
| TitleSequenceProduct, LogoFader | menu.c fs_title | ok by names. Left out: the TitleLogoLight effects |
| FileSelector, FileSelectItem, FileSelectButton / Info / Number | menu.c fs_* | fixed #767: FileSelectItem's SE_SY_LV_FILE_SEL_MORPHBLUR in the first 40 frames of its change nerves. Rest ok by names. Left out: exeFileBroken (System_FileSelect012: the port takes a file that does not read as empty); CopyReject is reached by no nerve. Wii-only: the manual (Manual2P, SE_SY_FILE_SEL_TIPS_OPEN), the Mii tip / caution / info windows, the P2 button |
| MiiSelect, MiiSelectIcon, MiiConfirmIcon | menu.c mii_* (the five fellows) | Mii faces: Wii-only; the fellows' icons ported, not re-read |
| StaffRoll | menu.c srl_* | ok by names |
| MoviePlayingSequence, MovieStarter, MovieSubtitles | movie.c, game.c movie_starter | ok by names (MoviePlayingSequence) |
| CometRetryButton | menu.c crb_* | fixed #660. Left out: forceOpenWipeCircle / forceCloseWipeCircle |
| Manual2P, BatteryInfo, HomeButtonLayout, EncouragePal60Window, ErrorMessageWindow, SaveIcon, IsbnManager, OdhConverter / odh | - | Wii-only (2P, the remote's battery, the HOME menu, PAL60, the Wii's NAND errors and banner) |

## Results, misses, wipes, the sequence

| Petari class | Port | Verdict |
|---|---|---|
| StageResultInformer (+ DisplayInformationForResult, StageResultSequenceChecker) | menu.c sr_*, gamedata.c gd_just_* | fixed #641: ShowGalaxyComplete (System_Result009, BGM_GALAXY_COMPLETE) and ShowGetPictureBook (System_Result005 / 008) added, decideNextNerve's order, its IconAButton and the information window's, initBestScoreWindow's Coin pane; InformationWindow in menus.bin (it was the ending's only: the windows never showed in the Observatory). fixed #659: the scene start's game data holder is no longer stored again after a stage clear (the just checks saw the star as old). Left out: ResultStarCounter / ResultPieceCounter effects, CS_CLICK_CLOSE |
| MissLayout | menu.c mlay (TooBad) | ok. Left out: the TooBad effect at step 18 |
| WipeGameOver | menu.c mlay (GameOver) | ok |
| WipeKoopa | menu.c mlay (WipeKoopa) | ok ("out" at its end / frames, sAnimFrame 90) |
| WipeRing, WipeFade, SceneWipeHolder, SystemWipeHolder | menu.c wr_* / wipe_* | ok by names |
| GameStageClearSequence | game.c (the star get demo) | ok by names |
| GameSequenceProgress | sequence.c seq_galaxy_move, main.c scene_switch | fixed #640: the garden to the Gateway's SE_SY_TICO_WAKE_PLAYER and its 120 frames before the scene (mMinFrame 300, the SE at 180: the load is not concurrent here, so the voice plays once the next scene's sounds are in); PlayerMissLeft. Rest ported, not re-read |
| StorySequenceExecutor, GalaxyCometScheduler, FindingLuigiEventScheduler, LuigiLeftSupplier | sequence.c, gamedata.c | ok by names (StorySequenceExecutor); the schedulers not re-read |
| SaveDataHandleSequence (+ NANDErrorSequence) | menu.c sv_* | ok by names. NAND_03 / 05 / 06 / 08 (the Wii's system memory errors): Wii-only |
| ScenePlayingResult | game.c | fixed #658: the star pieces' 1-up (STAR_PIECE_STEP_NUM 50, getLast1upStarPieceNum) |

## Screen effects (not layouts)

BloomEffect, BloomEffectSimple, CenterScreenBlur, DepthOfFieldBlur, FullScreenBlur, ScreenBlurEffect, HeatHazeEffect,
LensFlare, WaterCameraFilter, ImageEffect*, CaptureScreenDirector, ScreenAlphaCapture, ScreenPreserver,
CopyFilterNegater: the image effects, out of this audit's scope (render.c; several have no PS2 counterpart).

## Next pass

docs/sound-gaps.md lists no SE of a Screen/ class as missing but the Wii-only ones (the pointer's B, the guidance
layouts, the battery, Manual2P, MiiSelect's scroll, PowerStarList's capture) once this pass is in: what is left to
check in the rows below is animation names and frames, not nerves gone.

The rows marked "ported, not re-read", in this order:
ScenarioSelectStar, GalaxyMap and its icons, the file select, the save window, the staff roll.
