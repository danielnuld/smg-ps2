/* banekiti.inc: MapObj/Banekiti.cpp after Petari (#included by game.c; update_banekiti): the spring bumper of
 * TeresaMario2DGalaxy (four placed, on their rails: MapPartsRailMover is the port's railer). Wait: its Wait clip,
 * SE_OJ_LV_BANEKITI_MOVE. Boo Mario touching one of its three sensors (body, right, left: 80, 100 apart along its
 * X) is reflected (MarioActor::attackOrPushSensor as Teresa: doTeresaReflection) and sends it ACTMES_TERESA_PLAYER_
 * TOUCH: Repel unless it is in it (a weak rumble and shake, its Repel clip, SE_OJ_BANEKITI_REPEL), Wait again at the
 * clip's end. Its nerve and step are its row's row_turn / row_step.
 * ponytail: no DPDSwoon (WalkerStateBindStarPointer: the 2P pointer), no star piece hit reaction (AnimScaleController
 * is the walkers' here: game.c asc_start) */
static void update_banekiti(void)
{
    int t = scene_type(sc, "Banekiti");
    if (t < 0) return;
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        int row = in->row;
        if (row < 0 || (int)sc->objs[row].type != t || !row_shown(row)) continue;
        if (!row_turn[row]) { /* exeWait */
            if (in->anim && (in->anim->clip_id < 0 || row_step[row] == 0)) obj_play(in, "wait", 1, 0);
            row_step[row] = 1;
            in->lv[0] = se_level(SE_OJ_LV_BANEKITI_MOVE, &in->m[12], in->lv[0]);
        } else if (row_step[row]++ == 0) { /* exeRepel */
            pad_rumble(RUMBLE_WEAK); camera_shake(SHAKE_WEAK);
            obj_play(in, "repel", 0, 0);
            se_start(SE_OJ_BANEKITI_REPEL, &in->m[12]);
#ifdef BANEKITI_TEST
            printf("banekiti test f %u: row %d repels%c", game_frame, row, 10);
#endif
        } else if (!in->anim || in->anim->done) { row_turn[row] = 0; row_step[row] = 0; } /* isBckStopped */
        if (player_mode != PM_TERESA) continue;
        for (int s = -1; s <= 1; s++) { /* its sensors against Boo Mario's body */
            V3 c = v3(in->m[12] + in->m[0] * 100.0f * s, in->m[13] + in->m[1] * 100.0f * s, in->m[14] + in->m[2] * 100.0f * s);
            V3 off = vsub(mw.c, c);
            float sep = vlen(off), r = 80.0f + MC.mTeresaBodyRadius;
            if (sep >= r || sep < 1e-4f) continue;
            teresa_reflect(mario, vmul(vnorm(off), r - sep), 0);
            if (!row_turn[row]) { row_turn[row] = 1; row_step[row] = 0; } /* receiveOtherMsg */
            break;
        }
    }
#ifdef BANEKITI_TEST /* plain build XF=-DBANEKITI_TEST (TeresaMario2DGalaxy/1): Boo Mario put by the first Banekiti every 300 frames from 1000 */
    if (game_frame >= 1000 && game_frame % 300 >= 100 && game_frame % 300 < 104) /* (held four frames: he may be falling) */
        for (int i = 0; i < ninsts; i++)
            if (insts[i].row >= 0 && (int)sc->objs[insts[i].row].type == t) {
                test_nolost = 1;
                if (player_mode != PM_TERESA) set_player_mode(PM_TERESA, 1);
                mario->pos = vsub(v3(insts[i].m[12] + 60.0f, insts[i].m[13], insts[i].m[14]), vmul(mario->up, HIT_BODY_H)); /* his centre 60 off its body sensor */
                mario->hvel = v3(0, 0, 0); mario->vup = 0;
                printf("banekiti test f %u: Boo Mario by row %d (anim %d, mode %d, centre %.0f off)%c", game_frame, insts[i].row, insts[i].anim != NULL, player_mode,
                       vlen(vsub(mw.c, v3(insts[i].m[12], insts[i].m[13], insts[i].m[14]))), 10);
                break;
            }
#endif
}
