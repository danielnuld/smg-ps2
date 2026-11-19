/* lava_jambo_sun.inc: MapObj/LavaJamboSunPlanet.cpp after Petari (#included by game.c after race.inc; ljs_*): the
 * sun of HellShellZone on its rail (one placed). At its rail's first point (initAfterPlacement). Wait: its SW_A on,
 * Move. Pause: its SW_B on, Move. Move: its speed the current rail point's arg0 when set (getMapPartsArgMoveSpeed
 * NoInit), x 0.1 each frame as Petari keeps it (_94), along the rail (moveCoordAndFollowTrans),
 * SE_AT_LV_JUMBO_SUN_MOVE; the current point's arg1 1: Pause, else at the rail's goal: Wait.
 * ponytail: no bloom model (createBloomModel) and no LodCtrl (the port's own LOD) */
enum { LJS_WAIT, LJS_PAUSE, LJS_MOVE };
static struct { int row, nrv; float speed; RmRail rail; u32 lv; } ljs;

static void ljs_init(void) /* from game_init */
{
    int t = scene_type(sc, "LavaJamboSunPlanet");
    ljs.row = -1;
    for (u32 i = 0; t >= 0 && i < sc->nobjs; i++)
        if ((int)sc->objs[i].type == t && rr_init(&ljs.rail, &sc->objs[i])) { ljs.row = (int)i; ljs.nrv = LJS_WAIT; ljs.speed = 0.0f; ljs.lv = 0; break; }
}
static void ljs_update(void)
{
    if (ljs.row < 0 || !row_shown(ljs.row)) return;
    const Row *r = &sc->objs[ljs.row];
    Inst *in = row_inst(ljs.row);
    if (ljs.nrv == LJS_WAIT) { if (r->sw[SW_A] >= 0 && sw_on(r, SW_A)) ljs.nrv = LJS_MOVE; }
    else if (ljs.nrv == LJS_PAUSE) { if (r->sw[SW_B] >= 0 && sw_on(r, SW_B)) ljs.nrv = LJS_MOVE; }
    else {
        int a0 = rr_point_arg(&ljs.rail, rr_point_no(&ljs.rail), 0);
        if (a0 != -1) ljs.speed = (float)a0;
        ljs.speed *= 0.1f;
        ljs.rail.coord = fminf(ljs.rail.coord + ljs.speed, ljs.rail.len);
        if (in) ljs.lv = se_level(SE_AT_LV_JUMBO_SUN_MOVE, &in->m[12], ljs.lv);
        if (rr_point_arg(&ljs.rail, rr_point_no(&ljs.rail), 1) == 1) ljs.nrv = LJS_PAUSE;
        else if (rr_reached_goal(&ljs.rail)) ljs.nrv = LJS_WAIT;
    }
    if (in) { V3 p = rr_pos(&ljs.rail); in->m[12] = p.x; in->m[13] = p.y; in->m[14] = p.z; }
#ifdef LJS_TEST /* plain build XF=-DLJS_TEST (its galaxy): its SW_A on at frame 1000, its SW_B every 300 frames after */
    if (game_frame == 1000 && r->sw[SW_A] >= 0) sw_set(r, SW_A, 1);
    if (game_frame > 1000 && game_frame % 300 == 0 && r->sw[SW_B] >= 0) sw_set(r, SW_B, 1);
    if (game_frame >= 1000 && game_frame % 60 == 0)
        printf("ljs test f %u: nerve %d coord %.0f / %.0f speed %.1f point %d inst %d%c", game_frame, ljs.nrv, ljs.rail.coord, ljs.rail.len, ljs.speed,
               rr_point_no(&ljs.rail), in != NULL, 10);
#endif
}
