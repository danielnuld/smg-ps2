/* SnowFloor / SnowplowSwitch after Petari (Game/MapObj/SnowFloor.cpp, SnowplowSwitch.cpp). #included by game.c (snow_*).
 *
 * SnowFloor: a 128 x 128 snow depth map over a half cylinder (radius 900, mAxis its Z, 1024 either way along it, up
 * its Y, +-1024 / 900 rad about the axis). control: the SnowplowSwitch check list (one a frame), Mario's walk digs
 * (doMove 2.5 cells, 600) and slows him (setPlayerWalkingResist), the spin (doErase 100, 20) / hip drop landing (50,
 * 1000) erase with SE_OJ_LV_SNOW_ERASE, the pointer digs (30, SE_OJ_LV_SNOW_DIG), then every cell falls back towards
 * 50 over its base; the texture (I8, the depth clamped) rebuilt. attackSensor (its "eye" sensor, 1448): Mario's fire
 * balls erase (their 80, 200); a SnowplowSwitch still hidden joins the check list. draw: 12 layers 6 apart, each 16
 * strips, colour (220, 220, 250) - 3 a layer, the texture's alpha tested over 20 x layer + 1 and blended, depth
 * written, back faces culled. Clipped as Petari (sphere 1448, far 100 m). SnowplowSwitch: hidden under the snow until
 * the depth over its sensor (70) sums under 100 x 70: then its SW_A.
 * ponytail: the pointer's world position is the cursor ray on the snow's cylinder (getStarPointerWorldPosUsingDepth
 * reads the depth buffer), no 2P pointer; the level SEs at full volume (digDpd's volume: the dig played only while it
 * moves snow); other enemy sensors digging at their feet (attackSensor's getBaseMtx case: no SnowCapsule enemy
 * reaches the snow); each strip split in 8 along the axis (the GS drops triangles past its guard band); the fill-back
 * loop skips rows settled within 0.01 of their limit (Petari's creep there is under a float step, the texel the
 * same) and the texture rewrites only changed rows (mDirtyFrames a row); the reduce map (createReduceMap) is never
 * read in Petari and not made. Cost (SNOW_TEST): fill-back 0.4 ms with the snow dug, the draw 1.0-1.3 ms */
#define SNOW_N 128      /* sTextureSize */
#define SNOW_STRIPS 16  /* sStripCount */
#define SNOW_LAYERS 12  /* sLayerCount */
#define SNOW_SEG 8      /* draw: each strip's pieces along the axis */
#define SNOW_CELL 8.0f  /* sCellSize: 1024 / 128 */
#define SNOW_CHECK 8
static struct {
    const Scene *at;
    int n;
    struct {
        int row;
        V3 pos, up, axis, dir[SNOW_STRIPS];
        float radius, hlen, hang;
        float *depth;            /* mSnowDepth */
        u8 *base;                /* mBaseDepth */
        float resist;            /* mWalkingResist */
        int resist_t;            /* mResistFrames */
        float last_dpd[2], last_player[2];
        int check[SNOW_CHECK], ncheck; /* mCheckList: SnowplowSwitch rows */
        u32 lv;
        u8 act[SNOW_N], dirty[SNOW_N]; /* per texture row: still filling back; texels to rewrite (2: both buffers) */
        float grid[(SNOW_STRIPS + 1) * (SNOW_SEG + 1)][3]; /* drawLayer's corners at layer 0, the columns' */
        float gdir[SNOW_STRIPS + 1][3];                     /* the columns' step a layer (sLayerHeight 6) */
    } f[I8_SLOTS];
    int sw_t;
    u8 found[64];                /* SnowplowSwitch::mIsDiscovered, per switch (snow_sw_idx) */
    int sw_rows[64], nsw;
    u32 rnd;
    u32 cycles; /* snow_draw's EE time (SNOW_TEST) */
} snow;

static float snow_rand(void) /* MR::getRandom: JMath::TRandom_fast */
{
    snow.rnd = snow.rnd * 0x19660Du + 0x3C6EF35Fu;
    union { u32 u; float f; } v = {.u = 0x3F800000u | snow.rnd >> 9};
    return v.f - 1.0f;
}

static void snow_init(const Scene *s)
{
    memset(&snow, 0, sizeof snow);
    snow.at = s;
    snow.rnd = 0x12345678u;
    int t = scene_type(s, "SnowFloor");
    snow.sw_t = scene_type(s, "SnowplowSwitch");
    for (u32 i = 0; i < s->nobjs; i++) {
        const Row *r = &s->objs[i];
        if (snow.sw_t >= 0 && (int)r->type == snow.sw_t && snow.nsw < 64) snow.sw_rows[snow.nsw++] = (int)i;
        if (t < 0 || (int)r->type != t || snow.n >= I8_SLOTS) continue;
        typeof(&snow.f[0]) f = &snow.f[snow.n++];
        f->row = (int)i;
        f->depth = mem_alloc(MEM_OBJECTS, SNOW_N * SNOW_N * sizeof(float));
        f->base = mem_alloc(MEM_OBJECTS, SNOW_N * SNOW_N);
        for (int k = 0; k < SNOW_N * SNOW_N; k++) {
            f->depth[k] = 32.0f + 120.0f * snow_rand();
            f->base[k] = (u8)f->depth[k];
            f->depth[k] += 80.0f;
        }
        memset(f->dirty, 2, sizeof f->dirty);
        /* initLocalPoint */
        V3 x, z;
        row_axes(r, &x, &z);
        f->pos = row_pos(r);
        f->up = vcross(z, x);
        f->axis = z;
        f->hlen = SNOW_N * SNOW_CELL;
        f->radius = 900.0f;
        f->hang = f->hlen / 900.0f;
        /* drawSetup: each strip's centre direction, the up turned about the axis */
        float half = f->hang / SNOW_STRIPS, range = 2.0f * f->hang - 2.0f * half;
        V3 side = vcross(f->axis, f->up);
        for (int k = 0; k < SNOW_STRIPS; k++) {
            float a = range * ((float)k / (SNOW_STRIPS - 1)) - f->hang + half;
            f->dir[k] = vadd(vmul(f->up, cosf(a)), vmul(side, sinf(a)));
        }
        /* drawLayer's corners: column 0 the first strip's left edge, column c + 1 strip c's right edge (p1 / p2) */
        float hw = SNOW_N * SNOW_CELL / SNOW_STRIPS;
        for (int c = 0; c <= SNOW_STRIPS; c++) {
            int k = c ? c - 1 : 0;
            V3 center = vadd(f->pos, vmul(f->dir[k], f->radius)), tangent = vnorm(vcross(f->axis, f->dir[k])); /* -(dir x axis) */
            V3 p = vadd(center, vmul(tangent, c ? hw : -hw));
            f->gdir[c][0] = 6.0f * f->dir[k].x; f->gdir[c][1] = 6.0f * f->dir[k].y; f->gdir[c][2] = 6.0f * f->dir[k].z;
            for (int j = 0; j <= SNOW_SEG; j++) {
                V3 q = vadd(p, vmul(f->axis, f->hlen * (1.0f - 2.0f * j / SNOW_SEG)));
                float *o = f->grid[c * (SNOW_SEG + 1) + j];
                o[0] = q.x; o[1] = q.y; o[2] = q.z;
            }
        }
        printf("snow floor: row %d at %.0f %.0f %.0f up %.2f %.2f %.2f axis %.2f %.2f %.2f" "\n", (int)i, f->pos.x, f->pos.y,
               f->pos.z, f->up.x, f->up.y, f->up.z, f->axis.x, f->axis.y, f->axis.z);
    }
}

/* calcLocalPoint: (angle / mHalfAngle, along the axis / mHalfLength, distance from the axis) */
static int snow_local(const typeof(snow.f[0]) *f, V3 p, V3 *out)
{
    V3 off = vsub(p, f->pos), radial = vtangent(off, f->axis);
    float rad = vlen(radial);
    float ang = atan2f(vdot(vcross(f->axis, f->up), radial), vdot(f->up, radial)); /* diffAngleSignedHorizontal */
    float ax = vdot(off, f->axis) / f->hlen;
    if (ang < -f->hang || ang > f->hang || ax < -1.0f || ax > 1.0f) return 0;
    *out = v3(ang / f->hang, ax, rad);
    return 1;
}
static void snow_tex_point(V3 l, float pt[2]) /* (x + 1) / 2, 1 - (y + 1) / 2 */
{
    pt[0] = (l.x + 1.0f) * 0.5f;
    pt[1] = 1.0f - (l.y + 1.0f) * 0.5f;
}
static int snow_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void snow_box(float cx, float cy, float r, int *x0, int *y0, int *x1, int *y1) /* the cells' range, s16 casts */
{
    *x0 = snow_clamp((short)(cx - r) - 1, 0, SNOW_N); *y0 = snow_clamp((short)(cy - r) - 1, 0, SNOW_N);
    *x1 = snow_clamp((short)(cx + r) + 1, 0, SNOW_N); *y1 = snow_clamp((short)(cy + r) + 1, 0, SNOW_N);
}
static void snow_touch(typeof(snow.f[0]) *f, int y0, int y1) /* rows dug: the fill loop takes them again */
{
    for (int y = y0; y < y1; y++) f->act[y] = 1;
}

/* doMove: the snow in radius cells of `to` dug by amount and pushed into the ring out to 1.732 x radius ahead of the
 * move from `from`; returns the depth dug through */
static float snow_move(typeof(snow.f[0]) *f, const float from[2], const float to_[2], float radius, float amount)
{
    static u16 xs[4096], ys[4096];
    static float ws[4096];
    float removed = 0.0f, wsum = 0.0f, dsum = 0.0f;
    u32 count = 0;
    float ux = to_[0] - from[0], uy = to_[1] - from[1], ul = ux * ux + uy * uy;
    if (ul > 0.0f) { ul = 1.0f / sqrtf(ul); ux *= ul; uy *= ul; } /* normalize: a zero move stays zero */
    float outer = 1.732051f * radius, tx = to_[0] * SNOW_N, ty = to_[1] * SNOW_N;
    int x0, y0, x1, y1;
    snow_box(tx, ty, outer, &x0, &y0, &x1, &y1);
    float o2 = outer * outer, r2 = radius * radius;
    snow_touch(f, y0, y1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x - tx, dy = y - ty, d2 = dx * dx + dy * dy;
            if (d2 <= r2) {
                float *d = &f->depth[y * SNOW_N + x];
                dsum += *d;
                if (*d > amount) { removed += amount; *d -= amount; }
                else { removed += *d; *d = 0.0f; }
            } else if (d2 <= o2) {
                float w = dx * ux + dy * uy;
                if (w > 0.0f && count < 4096) { wsum += w; xs[count] = (u16)x; ys[count] = (u16)y; ws[count] = w; count++; }
            }
        }
    if (count == 0) return 0.0f;
    removed /= wsum;
    for (u32 i = 0; i < count; i++) f->depth[ys[i] * SNOW_N + xs[i]] += removed * ws[i];
    return dsum;
}
/* doErase(TVec2f, ...): a smooth (1 - d^2 / r^2)^2 dig of amount */
static void snow_erase_at(typeof(snow.f[0]) *f, const float pt[2], float radius, float amount)
{
    float px = pt[0] * SNOW_N, py = pt[1] * SNOW_N, r2 = radius * radius;
    int x0, y0, x1, y1;
    snow_box(px, py, radius, &x0, &y0, &x1, &y1);
    snow_touch(f, y0, y1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x - px, dy = y - py, d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            float k = 1.0f - d2 / r2, dig = k * k * amount, *d = &f->depth[y * SNOW_N + x];
            *d = *d > dig ? *d - dig : 0.0f;
        }
}
/* doErase(TVec3f, ...): a sphere of radius about p cut at the snow's surface */
static int snow_erase(typeof(snow.f[0]) *f, V3 p, float radius, float amount)
{
    V3 l;
    if (!snow_local(f, p, &l)) return 0;
    float r2 = radius * radius, h = l.z - f->radius;
    if (r2 < h * h) return 0;
    float pt[2];
    snow_tex_point(l, pt);
    snow_erase_at(f, pt, sqrtf(r2 - h * h) / SNOW_CELL, amount);
    return 1;
}
static float snow_cover(const typeof(snow.f[0]) *f, const float pt[2], float radius) /* calcCoverSnow */
{
    float total = 0.0f, px = pt[0] * SNOW_N, py = pt[1] * SNOW_N, r2 = radius * radius;
    int x0, y0, x1, y1;
    snow_box(px, py, 1.732051f * radius, &x0, &y0, &x1, &y1);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float dx = x - px, dy = y - py;
            if (dx * dx + dy * dy <= r2) total += f->depth[y * SNOW_N + x];
        }
    return total;
}

static void snow_dig_walk(typeof(snow.f[0]) *f, V3 l) /* digPlayerWalk */
{
    if (l.z < f->radius - 50.0f || l.z > 50.0f + f->radius) return;
    float pt[2];
    snow_tex_point(l, pt);
    float depth = snow_move(f, f->last_player, pt, 20.0f / SNOW_CELL, 600.0f);
    f->last_player[0] = pt[0]; f->last_player[1] = pt[1];
    float excess = depth - 200.0f;
    if (excess < 0.0f) {
        if (f->resist_t) { m_walk_resist = f->resist; f->resist *= 0.8f; }
        else m_walk_resist = 0.0f;
    } else {
        float resist = fminf(fmaxf(excess / 500.0f, 0.0f), 1.0f) * 0.5f; /* cPlayerWalkResist */
        m_walk_resist = resist;
        if (!f->resist_t || resist > f->resist) { f->resist = resist; f->resist_t = 5; }
    }
    if (f->resist_t) f->resist_t--;
}
static void snow_dig_dpd(typeof(snow.f[0]) *f, V3 l) /* digDpd */
{
    if (l.z < f->radius - 250.0f || l.z > 250.0f + f->radius) return;
    float pt[2];
    snow_tex_point(l, pt);
    int vol = (int)(snow_move(f, f->last_dpd, pt, 50.0f / SNOW_CELL, 30.0f) / 200.0f);
    if (vol > 0) f->lv = se_level(SE_OJ_LV_SNOW_DIG, &f->pos.x, f->lv); /* startLevelSound(volume) */
    f->last_dpd[0] = pt[0]; f->last_dpd[1] = pt[1];
}
static int snow_sw_idx(int row)
{
    for (int k = 0; k < snow.nsw; k++) if (snow.sw_rows[k] == row) return k;
    return -1;
}
static void snow_check_update(typeof(snow.f[0]) *f) /* updateCheckList: the first one, then to the back unless found */
{
    if (!f->ncheck) return;
    int row = f->check[0], found = 0, k = snow_sw_idx(row);
    V3 l;
    if (snow_local(f, row_pos(&sc->objs[row]), &l)) {
        float pt[2];
        snow_tex_point(l, pt);
        if (snow_cover(f, pt, 70.0f / SNOW_CELL) < 100.0f * 70.0f && k >= 0 && !snow.found[k]) {
            snow.found[k] = 1; /* SnowplowSwitch::receiveOtherMsg ACTMES_NOTIFY_DISCOVER_SNOW: onSwitchA */
            sw_set(&sc->objs[row], SW_A, 1);
            printf("snowplow switch: row %d found" "\n", row);
            found = 1;
        }
    }
    f->ncheck--;
    for (int i = 0; i < f->ncheck; i++) f->check[i] = f->check[i + 1];
    if (!found) f->check[f->ncheck++] = row;
}

static void snow_control(typeof(snow.f[0]) *f)
{
    snow_check_update(f);
    V3 l;
    if (snow_local(f, mario->pos, &l)) snow_dig_walk(f, l);
    if (mario->spin_timer > 0) { /* isPlayerSwingAction */
        if (snow_erase(f, mario->pos, 100.0f, 20.0f)) f->lv = se_level(SE_OJ_LV_SNOW_ERASE, &f->pos.x, f->lv);
    } else if (hip_hit) { /* isPlayerHipDropLand */
        if (snow_erase(f, mario->pos, 50.0f, 1000.0f)) f->lv = se_level(SE_OJ_LV_SNOW_ERASE, &f->pos.x, f->lv);
    }
    if (cur_idle < 300) { /* isStarPointerInScreen(0): the cursor ray on the cylinder of mRadius, the nearest hit */
        V3 o, d;
        cursor_ray(&o, &d);
        V3 oc = vtangent(vsub(o, f->pos), f->axis), dc = vtangent(d, f->axis);
        float a = vdot(dc, dc), b = vdot(oc, dc), c = vdot(oc, oc) - f->radius * f->radius, disc = b * b - a * c;
        if (a > 1e-6f && disc >= 0.0f) {
            float t = (-b - sqrtf(disc)) / a;
            if (t < 0.0f) t = (-b + sqrtf(disc)) / a;
            if (t >= 0.0f && snow_local(f, vadd(o, vmul(d, t)), &l)) snow_dig_dpd(f, l);
        }
    }
    /* the snow falls back: only rows dug since they settled (a row settles once each cell is within 0.01 under its
     * limit, 50 over its base, or at or over it: Petari's then creeps up by less than a float step, its texel the same) */
    for (int y = 0; y < SNOW_N; y++) {
        if (!f->act[y]) continue;
        float *dp = f->depth + y * SNOW_N;
        const u8 *bp = f->base + y * SNOW_N;
        int moving = 0;
        for (int x = 0; x < SNOW_N; x++) {
            float v = dp[x], b = bp[x];
            if (v < 10.0f) { v += 0.1f * snow_rand(); moving = 1; }       /* cSnowFallRateFirst */
            else if (v < 30.0f) { v += 0.5f * snow_rand(); moving = 1; }  /* cSnowFallRateBottom, cSnowFallRateQuick */
            else {
                float limit = 50.0f + b, deficit = limit - v;
                if (deficit > 0.0f) { v += deficit / (limit - 30.0f) * (0.2f * snow_rand()); moving |= deficit > 0.01f; } /* cSnowFallRate */
            }
            dp[x] = v < 0.0f ? 0.0f : v > 100.0f + b ? 100.0f + b : v;
        }
        f->act[y] = (u8)moving;
        f->dirty[y] = 2; /* mDirtyFrames */
    }
}
/* attackSensor: its eye sensor (1448) on the enemy sensors it overlaps */
static void snow_attack(typeof(snow.f[0]) *f)
{
    const float eye = 1.4142f * SNOW_N * SNOW_CELL;
    for (int b = 0; b < MAX_FIREBALLS; b++) /* マリオ炎球 (FireMarioBall: cSensorRadius 80) */
        if (fireballs[b].t && vlen(vsub(fireballs[b].pos, f->pos)) < eye + 80.0f) snow_erase(f, fireballs[b].pos, 80.0f, 200.0f);
    for (int k = 0; k < snow.nsw; k++) { /* SnowplowSwitch (Body 70): ACTMES_ASK_HIDDEN_BY_SNOW while not found */
        int row = snow.sw_rows[k];
        if (snow.found[k] || !row_shown(row) || vlen(vsub(row_pos(&sc->objs[row]), f->pos)) >= eye + 70.0f) continue;
        int in = 0;
        for (int i = 0; i < f->ncheck; i++) in |= f->check[i] == row;
        if (!in && f->ncheck < SNOW_CHECK) f->check[f->ncheck++] = row;
    }
}
static int snow_clipped(const typeof(snow.f[0]) *f) /* setClippingTypeSphere(1.414 x 1024), setClippingFar100m */
{
    return render_clip_judge(&f->pos.x, 1.414f * 1024.0f, 10000.0f);
}
static void snow_update(void)
{
    if (snow.at != sc) return;
    u32 c0 = cycles();
    for (int i = 0; i < snow.n; i++) {
        if (snow_clipped(&snow.f[i])) continue;
        snow_control(&snow.f[i]);
        snow_attack(&snow.f[i]);
    }
    c0 = cycles() - c0;
#ifdef SNOW_TEST /* XF=-DSNOW_TEST=k (plain, SnowCapsuleGalaxy/1): 240 frames after Mario is free he and the camera go
                  * 100 over SnowplowSwitch k (1 the first: under no snow), the pointer at the screen's centre digs
                  * there from frame 900, before it Mario walks to and fro along the axis; every 30 frames the cover
                  * over it, the resist, Mario's local place and the times */
    static int st;
    if (game_test_free() && ++st == 240 && SNOW_TEST - 1 < snow.nsw) {
        V3 p = row_pos(&sc->objs[snow.sw_rows[SNOW_TEST - 1]]), up = vnorm(vtangent(vsub(p, snow.f[0].pos), snow.f[0].axis));
        mario->pos = vadd(p, vmul(up, 100.0f));
        for (int i = 0; i < snow.n; i++) { /* Mario on the floor's middle (the switches lie over holes) */
            V3 l;
            if (snow_local(&snow.f[i], p, &l)) mario->pos = vadd(snow.f[i].pos, vmul(snow.f[i].up, 1400.0f));
        }
        camera_set_override(1, vadd(mario->pos, vmul(up, 1200.0f)), mario->pos, snow.f[0].axis, 45.0f);
        printf("snow test: Mario over switch row %d" "\n", snow.sw_rows[SNOW_TEST - 1]);
        for (int k = 0; k < snow.nsw; k++)
            for (int i = 0; i < snow.n; i++) {
                V3 l = v3(9, 9, 0), q = row_pos(&sc->objs[snow.sw_rows[k]]);
                snow_local(&snow.f[i], q, &l);
                printf("snow test: switch %d row %d at %.0f %.0f %.0f on floor %d: %.2f %.2f %.0f" "\n", k, snow.sw_rows[k], q.x, q.y, q.z, i, l.x, l.y, l.z);
            }
    }
    if (st > 900) { /* the pointer circling the centre, its radius 0..48 pixels */
        float a = st * 0.15f, r = (float)(st % 160) * 0.3f;
        cur_idle = 0; cur_x = 320.0f + r * cosf(a); cur_y = 224.0f + r * sinf(a);
    }
    if (st > 300 && st <= 900) { /* first Mario walks along the axis and back through the snow */
        static V3 home;
        if (st == 301) home = mario->pos;
        cur_idle = 1000;
        test_walk_to(vadd(home, vmul(snow.f[0].axis, (st / 200) & 1 ? 600.0f : -600.0f)), 30.0f);
    }
    if (st > 240 && st % 30 == 0 && SNOW_TEST - 1 < snow.nsw) {
        int row = snow.sw_rows[SNOW_TEST - 1];
        for (int i = 0; i < snow.n; i++) {
            V3 l;
            float pt[2], cov = -1.0f;
            if (snow_local(&snow.f[i], row_pos(&sc->objs[row]), &l)) { snow_tex_point(l, pt); cov = snow_cover(&snow.f[i], pt, 70.0f / SNOW_CELL); }
            V3 ml = v3(9, 9, 0);
            snow_local(&snow.f[i], mario->pos, &ml);
            int low = 0, act = 0;
            for (int k = 0; k < SNOW_N * SNOW_N; k++) low += snow.f[i].depth[k] < 20.0f;
            for (int k = 0; k < SNOW_N; k++) act += snow.f[i].act[k];
            printf("snow test f %d: floor %d cells under 20: %d, rows active %d" "\n", st, i, low, act);
            printf("snow test f %d: floor %d clipped %d cover %.0f check %d found %d sw %d resist %.3f mario %.2f %.2f %.0f upd %.2f ms draw %.2f ms" "\n",
                   st, i, snow_clipped(&snow.f[i]), cov, snow.f[i].ncheck, snow.found[SNOW_TEST - 1], sw_on(&sc->objs[row], SW_A),
                   m_walk_resist, ml.x, ml.y, ml.z, c0 / 294912.0f, snow.cycles / 294912.0f);
        }
    }
#endif
}

static void snow_draw(V3 eye)
{
    if (snow.at != sc) return;
    u32 c0 = cycles();
    static float uv[(SNOW_STRIPS + 1) * (SNOW_SEG + 1)][2];
    if (uv[SNOW_SEG][1] == 0.0f)
        for (int c = 0; c <= SNOW_STRIPS; c++)
            for (int k = 0; k <= SNOW_SEG; k++) {
                uv[c * (SNOW_SEG + 1) + k][0] = (float)c / SNOW_STRIPS; /* mTexCoords */
                uv[c * (SNOW_SEG + 1) + k][1] = (float)k / SNOW_SEG;
            }
    for (int i = 0; i < snow.n; i++) {
        typeof(&snow.f[0]) f = &snow.f[i];
        if (snow_clipped(f)) continue;
        u8 *img = render_i8_pixels(i); /* createTexture: the depth clamped to 0..255, the rows changed */
        for (int y = 0; y < SNOW_N; y++) {
            if (!f->dirty[y]) continue;
            f->dirty[y]--;
            for (int x = y * SNOW_N; x < (y + 1) * SNOW_N; x++) {
                int v = (int)f->depth[x];
                img[x] = (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
            }
        }
        render_i8_upload(i);
        float h = vdot(vsub(eye, f->pos), f->up), hs = vdot(vsub(eye, f->pos), vcross(f->axis, f->up));
        for (int layer = 0; layer < SNOW_LAYERS; layer++) {
            int th = layer * (240 / SNOW_LAYERS) + 1;
            render_i8_aref(th >> 1); /* GXSetAlphaCompare(GX_GREATER, threshold): I / 2 > threshold / 2 (odd) */
            u32 r = 220 - 3 * layer, g = 220 - 3 * layer, b = 250 - 3 * layer; /* sColor + layer x sColorStep */
            float lh = f->radius + 6.0f * layer; /* sLayerHeight */
            u8 on[SNOW_STRIPS];
            int any = 0;
            for (int c = 0; c < SNOW_STRIPS; c++) { /* GX_CULL_BACK: the strip's plane faces the eye */
                V3 d = f->dir[c];
                on[c] = vdot(d, f->up) * h + vdot(d, vcross(f->axis, f->up)) * hs > lh;
                any |= on[c];
            }
            if (!any) continue;
            render_grid((const float (*)[3])f->grid, (const float (*)[3])f->gdir, (float)layer, (const float (*)[2])uv,
                        SNOW_STRIPS + 1, SNOW_SEG + 1, on, I8_TEX, r | g << 8 | b << 16 | 0x80u << 24);
        }
    }
    snow.cycles = cycles() - c0;
}
