# Enemy audit against Petari, class by class

Every placed class whose source is under `vendor/Petari/src/Game/Enemy/` (the bosses are another list), most placed
first (`out/census.json` "place"). For each: the Petari .cpp read whole (nerves and what they start, sensors and every
message answered, switches, items, clipping, sounds, effects), then the port, then what differed fixed.
Verdict: **ok** (read, nothing to fix), **fixed** (PR), **partly** (fixed in the PR, the rest listed), **todo** (not
read yet).

A cause shared by many classes: `fireball_hit_walkers`, the thrown shell's loop (JetTurtle) and `walker_blow`'s callers
ended with a generic kill (Kuribo's smoke and a coin) for every walker kind without its own case. Petari has no such
default: each class answers its own messages, and isMsgPlayerHitAll covers the spin, the shell, the fire ball, the
freeze and the rainbow attack. A class read here gets its own case in each of them (or none).

| Class | Placed | Verdict | What was done / what is left |
|---|---|---|---|
| Kuribo | 255 | fixed #677 | Fire floor death (isBindedGroundDamageFire); a fire ball / a shell: requestBlowDown (was the generic kill); BlowLand's wall rebound; endNonActive's onBind whatever nerve follows. Left: isPressedRoofAndGround (the walker binder keeps no roof), sendMsgPush to Mario on a failed attack |
| TakoHei | 146 | fixed #677 | A fire ball / a shell: requestPunch (was the generic kill). Everything else equal. Left: tryPressed (no roof) |
| JumpSpider | 141 | fixed #677 | The kick while powerless (isMsgPlayerKick: HitBlow); a fire ball / a shell: HitBlow; the generic rainbow kill beside its own; EyeLight kept while drawn |
| PackunPetit | 132 | todo | |
| Unizo (Unizo, UnizoLand, UnizoShoal) | 86 | todo | |
| KoteBug | 85 | todo | |
| Meramera (Meramera, IceMeramera) | 85 | partly #677 | Enemy blows no longer kill it (only explosions and the rainbow attack: requestDownForce); a shell passes; the Meramera-Meramera push was done twice (0.5 + 0.5); IgnitionForce's setVelocityJump kept its horizontal velocity; IceMeramera's Revival effect in Sink was behind the hidden-model return. Left: sendMsgElementAttack on non-player sensors (a fire one lights a waiting Candlestand), ExtinguishSide / ExtinguishSmoke are one-shot instead of following mExtinguishMtx, the fire ball / rainbow tests use the generic 60 sphere instead of its body 55 x scale |
| Kiraira (Kiraira, KirairaRail) | 80 | todo | |
| MechanicKoopaMini | 75 | partly #677 | A shell passes (was the generic kill); Pursue's distance tests run after a JumpStart too; the push between two was done twice. Left: the star piece test uses the generic sphere, not its body 130 x scale at JointRoot |
| Karikari | 73 | partly #677 | The hip drop release never ran (the landing zeroed the cling count before the director); a fire ball / a shell / the rainbow attack: tryBlowOut (10 / 38, Hit) unless it clings; BlowOutStarPiece plays BLOW and the blow hit sound; DPDAttacked's Touch effect on every release; a refused star piece flies on; an enemy blow is taken in DPDAttacked. Left: the director's isPlayerParalyzing (ElectricDeath) and isPlayerInRush; removeAllClingingKarikari from the slider landing, the abyss damage and the black hole; Press (nothing sends ToEnemyAttackTrample); its BTPs (Bite, Land); BlowOut dying the frame it starts when on the ground |
| Teresa (Teresa, TeresaChief) | 63 | partly #677 | filterBind inverted (GhostThrough walls are passed only while drifting); the appear nerves' onEnd (endAppear) at their end; a shell and enemy blows no longer kill it. Left: its body's push with other enemies (only Teresa on Teresa), a star piece that stops it is consumed, Walk's Normal BTP |
| Takobo | 62 | partly #677 | Its body pushes Mario (above it, out of Move / Attack, on a failed attack; isPlayerExistUp as a direction test; not on the hip drop landing); the kick while stunned; a fire ball / a shell / the rainbow attack: HitPunch with the scene stop; a star piece in Press / HitPunch flies on; the leg's frame count an s32. Left: the fire floor bounce (arg0), Ice (no SpinningBox ice) |
| KuriboMini | 46 | fixed #677 | Fire floor death; a fire ball / a shell / the rainbow attack: requestBlowDown with a coin |
| OnimasuPivot | 42 | partly #677 | See Onimasu |
| Karon | 41 | partly #677 | An explosion: kill and three star pieces; a shell: Broken; enemy blows no longer kill it; Pursue without clipping; SinkDown keeps its attackSensor; dark matter kills; a star piece / fire ball taken in Recover; the fall check without the ground test; SinkDown's second level sound. Left: isFallOrDangerNextMove's danger floors, forceBindOnGround in Broken / StepBroken, isMsgStartPowerStarGet |
| Birikyu (BigBirikyu) | 40 | partly #677 | arg0 0 no longer falls back to 10. Left: star pieces not reflected, BigBirikyu's Hit / Clash and own-named loop effect, the electric attack on enemies and its push on a failed one |
| StinkBugSmall | 40 | partly #677 | The rainbow attack, a fire ball and enemy blows no longer kill it generically (its own HipDropDown for the rainbow one); a nerve set by a message starts at its first step (a hip drop in DashSignEnd killed with no Death clip); Shake with Mario hip-dropping on it: Panic; Panic's second level sound. Left: the rainbow wall / roof touch, HipDropDown's invalidateClipping, the Death BRK |
| BasaBasa (BasaBasa, BasaBasaIce) | 36 | partly #677 | No clipping out of Wait / AirWait (it froze out of view); a fire ball / a shell / the rainbow attack / an enemy blow: PunchDown (was the generic kill); explosions pass; BasaBasaIce as Ice Mario; trample / spin taken in HitBack; AttachCelling without sensors; a failed attack pushes Mario; TrampleDown stops the clip. Left: the spin storm on BasaBasaIce, the hang model, AnimStampController |
| BegomanSpike | 34 | todo | |
| Poihana | 33 | todo | |
| Kanina (Kanina, KaninaRed) | 33 | todo | |
| StringSpider | 33 | todo | |
| Gesso | 31 | todo | |
| Snakehead (Snakehead, SnakeheadSmall) | 30 | todo | |
| BombHeiLauncher (BombHeiLauncher, BombLauncher) | 28 | todo | |
| Dossun | 27 | todo | |
| Jellyfish | 26 | todo | |
| RingBeamer | 22 | todo | |
| Mogu | 22 | todo | |
| Mogucchi | 22 | todo | |
| SearchBeamer | 22 | todo | |
| EyeBeamer | 21 | todo | |
| Metbo | 21 | todo | |
| Pukupuku | 19 | todo | |
| TeresaWater | 19 | todo | |
| Hanachan | 19 | todo | |
| Kabokuri | 17 | todo | |
| FireBubble | 16 | todo | |
| SamboHead | 14 | todo | |
| BombTeresa | 13 | todo | |
| NokonokoLand | 13 | todo | |
| Petari | 13 | todo | |
| JumpBeamer | 12 | todo | |
| BegomanLauncher (BegomanLauncher, BegomanBabyLauncher) | 10 | todo | |
| OnimasuJump (Onimasu) | 10 | partly #677 | The landing's rumble and shake (中 within 2000 with the normal weak shake, 弱 within 3500 with the weak one, from its place before the landing); FactoryGalaxy's WaitForStamp before SW_A. Left: isSameDirection as the cross product test (0.01) |
| HammerHeadPackun | 10 | todo | |
| OtaRock | 8 | todo | |
| JellyfishElectric | 7 | todo | |
| BegomanBaby | 6 | todo | |
| BegomanSpring (BegomanSpring, BegomanSpringHide) | 6 | todo | |
| BombHei | 4 | todo | |
| BallBeamer | 3 | todo | |
| BombBird | 3 | todo | |
| WaterBazooka (WaterBazooka, ElectricBazooka) | 3 | todo | |
| Balloonfish | 2 | todo | |
| CocoSambo | 2 | todo | |
| JumpGuarder | 2 | todo | |
| DharmaSambo | 2 | todo | |
| IceMerameraKing | 2 | partly #677 | Enemy blows, shells and fire balls no longer kill it (it answers none). Left: its children's MoveLimit collision, its shadow dropped at DeathDemo |
| KuriboChief | 1 | todo | |
| UnizoLauncher | 1 | todo | |
| KoopaJrShip | 1 | todo | |
| StinkBugParent | 1 | todo | Read: a star piece on its body is not reflected (SpinReaction), no shadow |
| Jiraira, MogucchiRefuseTerritory | 0 | skipped | Not placed |

Shared: SpinHitController's star pieces come with SE_OJ_STAR_PIECE_BURST (spin_hit_exec dropped them silently): #677.
