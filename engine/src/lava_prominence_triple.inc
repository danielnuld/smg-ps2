/* lava_prominence_triple.inc: MapObj/LavaProminenceTriple.cpp after Petari (#included by game.c; lpt_*):
 * LavaProminenceTriple (HellPoolAZone's three) and LavaProminenceEnvironment (HellProminenceGalaxy's seven): the
 * wheel of three fire heads (the Environment: one, no sensor). appear (its SW_APPEAR on): the appear time arg1, the
 * Drop effects. control: the appear time counted down first; then its Y turn + arg0 (sInitRotateVel 100) / 100
 * degrees a frame, its matrix its rotation x that turn, SE_OJ_LV_PROMINENCE_T_MOVE, a head's Drop effect (on its
 * Head joint) off while the joint is out of the view and on again in it. Its attack sensor (80 x scale y) on the
 * ring of sToSensorRadius 1900 x scale y at Mario's angle about it, clamped into the head's arc he is nearest
 * (sNoSensorTable); touching it: sendMsgEnemyAttackFireStrong.
 * ponytail: the Head joints' places are the BDLs' (read offline: children of the root, translations only); no bloom
 * model; its state per placed one in MEM_OBJECTS */
typedef struct { int row, t, was; float ry; u8 drop[3]; } Lpt;
static Lpt *lpts;
static int nlpt, lpt_type[2];

static void lpt_init(void) /* from game_init */
{
    lpt_type[0] = scene_type(sc, "LavaProminenceTriple"); lpt_type[1] = scene_type(sc, "LavaProminenceEnvironment");
    nlpt = 0; lpts = NULL;
    for (int pass = 0; pass < 2; pass++) {
        int n = 0;
        for (u32 i = 0; i < sc->nobjs; i++) {
            int ty = (int)sc->objs[i].type;
            if (ty < 0 || (ty != lpt_type[0] && ty != lpt_type[1])) continue;
            if (pass) { memset(&lpts[n], 0, sizeof(Lpt)); lpts[n].row = (int)i; }
            n++;
        }
        if (!pass && n) lpts = mem_alloc(MEM_OBJECTS, n * sizeof(Lpt));
        nlpt = n;
    }
}
static void lpt_update(void)
{
    static const float head[2][3][3] = {{{1371.87f, 0.0f, -1294.55f}, {-1303.14f, 0.0f, -1365.97f}, {-33.66f, 0.0f, 1887.97f}}, /* LavaProminenceTriple.bdl Head1-3 */
                                        {{1885.45f, 0.0f, 54.67f}}};                                                          /* LavaProminenceEnvironment.bdl Head1 */
    static const float no[7] = {0.0f, 59.765598f, 134.2f, 176.045f, 222.6f, 278.26199f, 360.0f}; /* sNoSensorTable */
    static const char *const drop[3] = {"Drop1", "Drop2", "Drop3"};
    for (int k = 0; k < nlpt; k++) {
        Lpt *l = &lpts[k];
        const Row *r = &sc->objs[l->row];
#ifdef LPT_TEST
        if (game_frame % 300 == 0 && game_frame >= 900) printf("lpt test f %u: %d row %d type %d shown %d inst %d turn %.0f appear %d drops %d %d %d%c", game_frame, k, l->row, (int)r->type == lpt_type[1], row_shown(l->row), row_inst(l->row) != NULL, l->ry, l->t, l->drop[0], l->drop[1], l->drop[2], 10);
#endif
        int env = (int)r->type == lpt_type[1], shown = row_shown(l->row), nh = env ? 1 : 3;
        const char *cls = env ? "LavaProminenceEnvironment" : "LavaProminenceTriple";
        Inst *in = shown ? row_inst(l->row) : NULL;
        if (shown && !l->was) { l->t = r->arg[1] != -1 ? r->arg[1] : 0; memset(l->drop, 1, 3); } /* appear */
        l->was = (u8)shown;
        if (!in) continue;
        float m[16], rot[3] = {0, l->ry, 0}, zero[3] = {0, 0, 0}, one[3] = {1, 1, 1}, ym[16];
        if (l->t > 0) l->t--;
        else {
            l->ry = fmodf(l->ry + (r->arg[0] != -1 ? (float)r->arg[0] : 100.0f) / 100.0f + 360.0f, 360.0f); /* mRotateVel */
            in->lv[0] = se_level(SE_OJ_LV_PROMINENCE_T_MOVE, &in->m[12], in->lv[0]);
        }
        rot[1] = l->ry;
        mat_translate_rot_scale(m, r->pos, r->rot, r->scale); /* calcAndSetBaseMtx: mBaseRot x mCurrRot */
        mat_translate_rot_scale(ym, zero, rot, one);
        mat_mul(in->m, m, ym);
        float w[4][16];
        memset(w, 0, sizeof w);
        for (int j = 0; j < 4; j++) {
            w[j][0] = w[j][5] = w[j][10] = w[j][15] = 1.0f;
            if (j) { w[j][12] = head[env][(j - 1) % nh][0]; w[j][13] = 0.0f; w[j][14] = head[env][(j - 1) % nh][2]; }
        }
        if (l->t <= 0)
            for (int h = 0; h < nh; h++) { /* updateEffectClipping: isJudgedToClipFrustum(the joint, 1) */
                const float *q = head[env][h];
                float p[3] = {in->m[0] * q[0] + in->m[8] * q[2] + in->m[12], in->m[1] * q[0] + in->m[9] * q[2] + in->m[13], in->m[2] * q[0] + in->m[10] * q[2] + in->m[14]};
                l->drop[h] = (u8)render_visible(p, 1.0f);
            }
        for (int h = 0; h < nh; h++) if (l->drop[h]) fx_named(cls, drop[h], in->m, (const float (*)[16])w, (const char *)l + h);
        if (env) continue;
        /* updateHitSensor, attackSensor */
        V3 x = vnorm(v3(m[0], m[1], m[2])), z = vnorm(v3(m[8], m[9], m[10])), pos = row_pos(r), d = vsub(mw.c, pos);
        if (vlen(d) < 0.001f) continue;
        float a = fmodf(atan2f(vdot(x, d), vdot(z, d)) * 57.29578f - l->ry + 720.0f, 360.0f);
        for (int i = 0; i < 3; i++) {
            if (a < (no[i * 2] + no[i * 2 + 1]) / 2.0f) {
                int j = i == 0 ? 2 : i - 1;
                if (i == 0) a += 360.0f;
                a = a < no[1 + j * 2] ? no[1 + j * 2] : a > no[2 + j * 2] ? no[2 + j * 2] : a;
                break;
            }
            if (i == 2) a = a < no[4] ? no[4] : a > no[5] ? no[5] : a;
        }
        a = fmodf(a + l->ry, 360.0f) / 57.29578f;
        V3 sp = vadd(pos, vmul(vadd(vmul(z, cosf(a)), vmul(x, sinf(a))), r->scale[1] * 1900.0f));
        if (vlen(vsub(mw.c, sp)) < 80.0f * r->scale[1] + HIT_BODY_R) mario_attack(ATK_FIRERUN_STRONG, sp);
#ifdef LPT_TEST
        if (game_frame % 60 == 0) printf("lpt test f %u: row %d turn %.0f appear %d drops %d %d %d sensor %.0f off Mario%c", game_frame, l->row, l->ry, l->t, l->drop[0], l->drop[1], l->drop[2], vlen(vsub(mw.c, sp)), 10);
#endif
    }
#ifdef LPT_TEST /* plain build XF=-DLPT_TEST (HellProminenceGalaxy/n): Mario put on the first Triple's sensor ring at frame 1000 */
    for (int k = 0; k < nlpt && game_frame == 1000; k++)
        if ((int)sc->objs[lpts[k].row].type == lpt_type[0]) {
            const Row *r = &sc->objs[lpts[k].row];
            float m[16];
            mat_translate_rot_scale(m, r->pos, r->rot, r->scale);
            sw_set(r, SW_APPEAR, 1); test_nolost = 1;
            mario->pos = vadd(row_pos(r), vmul(vnorm(v3(m[8], m[9], m[10])), 1900.0f * r->scale[1]));
            mario->hvel = v3(0, 0, 0); mario->vup = 0;
            camera_start_pos_end();
            break;
        }
#endif
}
