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
| Kuribo | 255 | fixed #688 | Fire floor death (isBindedGroundDamageFire); a fire ball / a shell: requestBlowDown (was the generic kill); BlowLand's wall rebound; endNonActive's onBind whatever nerve follows. Left: isPressedRoofAndGround (the walker binder keeps no roof), sendMsgPush to Mario on a failed attack |
| TakoHei | 146 | fixed #688 | A fire ball / a shell: requestPunch (was the generic kill). Everything else equal. Left: tryPressed (no roof) |
| JumpSpider | 141 | fixed #688 | The kick while powerless (isMsgPlayerKick: HitBlow); a fire ball / a shell: HitBlow; the generic rainbow kill beside its own; EyeLight kept while drawn |
| PackunPetit | 132 | fixed #693 | A fire ball / the rainbow attack: punchDown (not in HitWaitForAttack / Hit); a shell: the scene stop and punchDown (not in Attack either); the scene stop only for the spin, the kick and the shell; a failed attack pushes Mario |
| Unizo (Unizo, UnizoLand, UnizoShoal) | 86 | partly #693 | An enemy attack not from a Unizo (blows, explosions): doBreak (was the generic kill, or nothing for explosions); rainbow Mario no longer kills it (no isMsgInvincibleAttack: a push); doAttack only when Mario took the attack; UnizoLand's SearchSmoke in Chase. Left: UnizoShoal's Ripple at the water surface, its sendMsgEnemyAttack on other enemies |
| KoteBug | 85 | partly #693 | Messages only: a fire ball: Blow (was the generic kill); a shell: Blow when overturned, else setOverturnFall. Its nerves were not re-read |
| Meramera (Meramera, IceMeramera) | 85 | partly #688 | Enemy blows no longer kill it (only explosions and the rainbow attack: requestDownForce); a shell passes; the Meramera-Meramera push was done twice (0.5 + 0.5); IgnitionForce's setVelocityJump kept its horizontal velocity; IceMeramera's Revival effect in Sink was behind the hidden-model return. Left: sendMsgElementAttack on non-player sensors (a fire one lights a waiting Candlestand), ExtinguishSide / ExtinguishSmoke are one-shot instead of following mExtinguishMtx, the fire ball / rainbow tests use the generic 60 sphere instead of its body 55 x scale |
| Kiraira (Kiraira, KirairaRail) | 80 | partly #693 | Ported after Kiraira.cpp (it was an early stand-in): Wait / FaceToMario / FaceToMarioAndStare, Explode from his touch or spin, a star piece, a shell (force-detonated: the explosion to what its eye holds, the other Kirairas BeExploded 10 frames later), the rumble / shake, its sounds, Dead 300 with the group's alarm, RecoverSign 120, Recover; down, its body pushes. Left (issue #691): its model packed with clips (Down / Revival BCKs, eye BTPs, BRKs), SearchLight, KirairaChain, its shadow |
| MechanicKoopaMini | 75 | partly #688 | A shell passes (was the generic kill); Pursue's distance tests run after a JumpStart too; the push between two was done twice. Left: the star piece test uses the generic sphere, not its body 130 x scale at JointRoot |
| Karikari | 73 | partly #688 | The hip drop release never ran (the landing zeroed the cling count before the director); a fire ball / a shell / the rainbow attack: tryBlowOut (10 / 38, Hit) unless it clings; BlowOutStarPiece plays BLOW and the blow hit sound; DPDAttacked's Touch effect on every release; a refused star piece flies on; an enemy blow is taken in DPDAttacked. Left: the director's isPlayerParalyzing (ElectricDeath) and isPlayerInRush; removeAllClingingKarikari from the slider landing, the abyss damage and the black hole; Press (nothing sends ToEnemyAttackTrample); its BTPs (Bite, Land); BlowOut dying the frame it starts when on the ground |
| Teresa (Teresa, TeresaChief) | 63 | partly #688 | filterBind inverted (GhostThrough walls are passed only while drifting); the appear nerves' onEnd (endAppear) at their end; a shell and enemy blows no longer kill it. Left: its body's push with other enemies (only Teresa on Teresa), a star piece that stops it is consumed, Walk's Normal BTP |
| Takobo | 62 | partly #688 | Its body pushes Mario (above it, out of Move / Attack, on a failed attack; isPlayerExistUp as a direction test; not on the hip drop landing); the kick while stunned; a fire ball / a shell / the rainbow attack: HitPunch with the scene stop; a star piece in Press / HitPunch flies on; the leg's frame count an s32. Left: the fire floor bounce (arg0), Ice (no SpinningBox ice) |
| KuriboMini | 46 | fixed #688 | Fire floor death; a fire ball / a shell / the rainbow attack: requestBlowDown with a coin |
| OnimasuPivot | 42 | partly #688 | See Onimasu |
| Karon | 41 | partly #688 | An explosion: kill and three star pieces; a shell: Broken; enemy blows no longer kill it; Pursue without clipping; SinkDown keeps its attackSensor; dark matter kills; a star piece / fire ball taken in Recover; the fall check without the ground test; SinkDown's second level sound. Left: isFallOrDangerNextMove's danger floors, forceBindOnGround in Broken / StepBroken, isMsgStartPowerStarGet |
| Birikyu (BigBirikyu) | 40 | partly #688 | arg0 0 no longer falls back to 10. Left: star pieces not reflected, BigBirikyu's Hit / Clash and own-named loop effect, the electric attack on enemies and its push on a failed one |
| StinkBugSmall | 40 | partly #688 | The rainbow attack, a fire ball and enemy blows no longer kill it generically (its own HipDropDown for the rainbow one); a nerve set by a message starts at its first step (a hip drop in DashSignEnd killed with no Death clip); Shake with Mario hip-dropping on it: Panic; Panic's second level sound. Left: the rainbow wall / roof touch, HipDropDown's invalidateClipping, the Death BRK |
| BasaBasa (BasaBasa, BasaBasaIce) | 36 | partly #688 | No clipping out of Wait / AirWait (it froze out of view); a fire ball / a shell / the rainbow attack / an enemy blow: PunchDown (was the generic kill); explosions pass; BasaBasaIce as Ice Mario; trample / spin taken in HitBack; AttachCelling without sensors; a failed attack pushes Mario; TrampleDown stops the clip. Left: the spin storm on BasaBasaIce, the hang model, AnimStampController |
| BegomanSpike | 34 | todo | |
| Poihana | 33 | partly #693 | Messages only: a fire ball: tryShock with the scene stop (was the generic kill). Its nerves were not re-read |
| Kanina (Kanina, KaninaRed) | 33 | partly #693 | Messages only: enemy blows pass. Left: receiveFireBall from fire balls and shells, receiveInvincibleAttack. Its nerves were not re-read |
| StringSpider | 33 | partly #693 | Messages only: fire balls and shells pass (were the generic kill). Its nerves were not re-read |
| Gesso | 31 | partly #693 | Messages only: a fire ball / the rainbow attack / an explosion: knockOut (were the generic kill, or nothing); enemy blows pass. Its nerves were not re-read |
| Snakehead (Snakehead, SnakeheadSmall) | 30 | partly #693 | Messages only: fire balls, the rainbow attack and enemy blows pass (were the generic kill; only the shell: TurtleDown). Its nerves were not re-read |
| BombHeiLauncher (BombHeiLauncher, BombLauncher) | 28 | todo | |
| Dossun | 27 | todo | |
| Jellyfish | 26 | todo | |
| RingBeamer | 22 | todo | |
| Mogu | 22 | partly #693 | Messages only: a fire ball passes; a shell / the rainbow attack: tryPunchHitted unless hidden (were the generic kill); enemy blows pass. Its nerves were not re-read |
| Mogucchi | 22 | partly #693 | Messages only: enemy blows pass (no receiveMsgEnemyAttack). Left: fire balls / shells / the rainbow attack are a HitAll for it (still the generic kill). Its nerves were not re-read |
| SearchBeamer | 22 | todo | |
| EyeBeamer | 21 | todo | |
| Metbo | 21 | partly #693 | Messages only: fire balls and shells pass (were the generic kill). Its nerves were not re-read |
| Pukupuku | 19 | todo | |
| TeresaWater | 19 | partly #693 | Messages only: it answers no attack (fire balls, shells, the rainbow attack and enemy blows killed it generically). Its nerves were not re-read |
| Hanachan | 19 | todo | |
| Kabokuri | 17 | partly #693 | Messages only: enemy blows pass. Left: its HitAll from fire balls / shells / the rainbow attack. Its nerves were not re-read |
| FireBubble | 16 | todo | |
| SamboHead | 14 | partly #693 | Messages only: enemy blows pass. Left: its HitAll from fire balls / shells / the rainbow attack. Its nerves were not re-read |
| BombTeresa | 13 | todo | |
| NokonokoLand | 13 | todo | |
| Petari | 13 | todo | |
| JumpBeamer | 12 | todo | |
| BegomanLauncher (BegomanLauncher, BegomanBabyLauncher) | 10 | todo | |
| OnimasuJump (Onimasu) | 10 | partly #688 | The landing's rumble and shake (中 within 2000 with the normal weak shake, 弱 within 3500 with the weak one, from its place before the landing); FactoryGalaxy's WaitForStamp before SW_A. Left: isSameDirection as the cross product test (0.01) |
| HammerHeadPackun | 10 | todo | |
| OtaRock | 8 | todo | |
| JellyfishElectric | 7 | todo | |
| BegomanBaby | 6 | todo | |
| BegomanSpring (BegomanSpring, BegomanSpringHide) | 6 | partly #693 | BegomanSpringHide's baby out in Shake (SE_EM_BEGOMAN_LAUNCH_BABY; needs the packs with BegomanBaby for it). Left: its return to hiding out of view, the event cameras. Its nerves were not re-read |
| BombHei | 4 | partly #693 | An explosion sets it off (isMsgExplosionAttack: Explode), fire balls / shells / the rainbow attack pass; held at Mario's HandR (updateTakingPosition's _49C) with his カブ抜き / カブウエイト clips, 両手投げ at the throw; Thrown rebounds off enemies (calcReflectionVector 0.8); no body sensor while taken. Left: the PullOut null animation, getThrowVec's swim / target cases, the rebound off NPCs. Its other nerves were not re-read |
| BallBeamer | 3 | todo | |
| BombBird | 3 | todo | |
| WaterBazooka (WaterBazooka, ElectricBazooka) | 3 | todo | |
| Balloonfish | 2 | todo | |
| CocoSambo | 2 | todo | |
| JumpGuarder | 2 | todo | |
| DharmaSambo | 2 | todo | |
| IceMerameraKing | 2 | partly #688 | Enemy blows, shells and fire balls no longer kill it (it answers none). Left: its children's MoveLimit collision, its shadow dropped at DeathDemo |
| KuriboChief | 1 | todo | |
| UnizoLauncher | 1 | todo | |
| KoopaJrShip | 1 | todo | |
| StinkBugParent | 1 | todo | Read: a star piece on its body is not reflected (SpinReaction), no shadow |
| Jiraira, MogucchiRefuseTerritory | 0 | skipped | Not placed |

Shared: SpinHitController's star pieces come with SE_OJ_STAR_PIECE_BURST (spin_hit_exec dropped them silently): #688.
