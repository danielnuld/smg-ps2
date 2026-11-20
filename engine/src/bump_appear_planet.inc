/* bump_appear_planet.inc: MapObj/BumpAppearPlanet.cpp after Petari (#included by game.c; update_bump_planet): the
 * planet of RabbitMoonZone whose bumps come out on its SW_A. Its collisions BumpAppearPlanetB / C on the joints of
 * those names (slice.py KCL_PARTS, JOINT_KCL_INDEXED: "BumpAppearPlanet#2.0" / "#3.0"), C always on, B off in Wait
 * (invalidateCollisionParts). SW_A on (listenStageSwitchOnOffA): BumpOut (BumpAppearPlanetPush,
 * SE_OJ_BUMP_APPEAR_PLN_OUT, B on); off: BumpIn (BumpAppearPlanetPull, SE_OJ_BUMP_APPEAR_PLN_IN), Wait at the clip's
 * end. Its nerve is its row's row_turn (0 Wait, 1 BumpOut, 2 BumpIn), row_step its first step.
 * ponytail: the switch is read each frame, not listened to (a switch already on at its init pushes the bumps out) */
static void update_bump_planet(void)
{
    int t = scene_type(sc, "BumpAppearPlanet");
    if (t < 0) return;
    const unsigned char *kb = scene_kcl(sc, "BumpAppearPlanet#2.0");
    for (int i = 0; i < ninsts; i++) {
        Inst *in = &insts[i];
        int row = in->row;
        if (row < 0 || (int)sc->objs[row].type != t) continue;
        const Row *r = &sc->objs[row];
        if (in->kparent >= 0) { /* its joint collisions: B by its nerve */
            if (in->kmove == 2) in->kcl = row_turn[row] ? kb : NULL;
            continue;
        }
        int on = r->sw[SW_A] >= 0 && sw_on(r, SW_A);
        if (row_turn[row] != 1 && on) { row_turn[row] = 1; row_step[row] = 0; }      /* startBumpOut */
        else if (row_turn[row] == 1 && !on) { row_turn[row] = 2; row_step[row] = 0; } /* startBumpIn */
        if (row_turn[row] && row_step[row] == 0) {
            int out = row_turn[row] == 1;
            row_step[row] = 1;
            obj_play(in, out ? "push" : "pull", 0, 0); /* (slice.py clip_key: the names over 31 characters cut to these) */
            se_start(out ? SE_OJ_BUMP_APPEAR_PLN_OUT : SE_OJ_BUMP_APPEAR_PLN_IN, &in->m[12]);
#ifdef BUMP_TEST
            printf("bump test f %u: row %d %s, anim %d, part kcl %d%c", game_frame, row, out ? "BumpOut" : "BumpIn", in->anim != NULL, kb != NULL, 10);
#endif
        } else if (row_turn[row] == 2 && (!in->anim || in->anim->done)) { row_turn[row] = 0; row_step[row] = 0; } /* isBckStopped */
#ifdef BUMP_TEST /* plain build XF=-DBUMP_TEST (ReverseKingdomGalaxy/n): its SW_A on at frame 1000, off at 1300, each 600 frames */
        if (game_frame >= 1000 && game_frame % 600 == 400 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
        if (game_frame >= 1000 && game_frame % 600 == 100 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 0);
        if (game_frame == 900) { /* Mario over it: its zone streams in */
            test_nolost = 1;
            mario->pos = vadd(row_pos(r), v3(0.0f, in->md->radius + 300.0f, 0.0f));
            mario->hvel = v3(0, 0, 0); mario->vup = 0; mario->grounded = 0;
            camera_start_pos_end();
        }
        if (game_frame >= 1000 && game_frame % 100 == 0)
            printf("bump test f %u: nerve %d sw %d part kcl %d clip %d done %d Mario %.0f off grounded %d%c", game_frame, row_turn[row], on, kb != NULL,
                   in->anim ? in->anim->clip_id : -2, in->anim ? in->anim->done : -1, vlen(vsub(mario->pos, row_pos(r))), mario->grounded, 10);
#endif
    }
}
