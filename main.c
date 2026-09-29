/* GB-Protector - a Defender-style shooter for the Game Boy (GBDK-2020).
 *
 * Layers:
 *   Background - scrolls horizontally; the 1024 px world is streamed into the
 *                32-tile hardware map one column at a time (see stream_bg).
 *   Window     - HUD along the bottom: scanner (2 rows) + text (1 row).
 *   Sprites    - 8x16 mode; scanner blips are sprites drawn over the window.
 *
 * Game logic that does not touch hardware lives in sim.h and is unit tested
 * on the host (`make test`).
 *
 * Controls: D-pad = thrust left/right (momentum) and move up/down,
 *           A = fire (hold for autofire), B = smart bomb, START = restart.
 */
#include <gb/gb.h>
#include <gb/sgb.h>
#include <stdint.h>
#include "sim.h"
#include "gfx.h"
#include "title.h"
#include "sgb_border.h"
#include "border_data.h"

/* ------------------------------------------------------------- constants */
#define PLAY_H      120u            /* playfield height; HUD window starts here */
#define MAX_ENEMY   8
#define MAX_HUMAN   6
#define MAX_BUL     8
#define MAX_EXP     3
#define NO_TARGET   0xFF

#define SHIP_W      16u
#define SHIP_H      8u
#define ENEMY_W     8u
#define HUMAN_W     8u
#define PBUL_W      4u
#define PBUL_H      2u
#define EBUL_W      2u
#define EBUL_H      2u
#define MAX_PBUL    4               /* player bullets in flight */

#define OWN_PLAYER  0
#define OWN_ENEMY   1

#define THRUST      20              /* 8.8 px/frame^2 */
#define MAX_VX      600             /* 8.8 px/frame  (~2.3 px) */
#define SHIP_SX_R   32u             /* screen x of ship when facing right */
#define SHIP_SX_L   112u            /* ...and when facing left */

#define ST_PLAY     0
#define ST_DYING    1
#define ST_OVER     2
#define ST_TITLE    3   /* boot: waits for START, same handling as ST_OVER */

/* The title screen's high score, drawn as sprites rather than tiles.  Two
 * reasons, both about the title image owning the tile bank: sprite colour 0 is
 * transparent, so the digits sit on the art with no white box around them, and
 * a number that changes needs no regenerated image.
 *
 * Sprite tiles and BG tiles are the same VRAM, so these must sit clear of every
 * tile the title image uses -- otherwise loading a glyph silently redraws part
 * of the artwork.  Past TITLE_TILE_COUNT, and past the game's own sprites
 * (64-89) too, the range is free. */
#define T_TITLE_GLYPH 144           /* even: 8x16 sprites pair 2n with 2n+1 */
#if T_TITLE_GLYPH < TITLE_TILE_COUNT
#error "T_TITLE_GLYPH overlaps the title image's BG tiles (shared VRAM)"
#endif
#define TITLE_HI_X    48            /* screen x of the "H" in "HI 00000" */
#define TITLE_HI_Y    104           /* the white strip above the terrain band */

#define RESCUE_BONUS 500            /* set a caught human back down */
#define OVER_HOLD   300             /* frames the game over screen holds, 5 s */

/* ----------------------------------------------------------------- state */
static uint8_t  state;
static uint8_t  frame;
static uint8_t  prev_keys;
static uint16_t rng_s;

static uint16_t player_x;           /* left edge, world px */
static uint8_t  player_xf;          /* sub-pixel fraction */
static int16_t  player_vx;          /* 8.8 fixed */
static uint8_t  player_y;
static int8_t   facing;             /* +1 right, -1 left */
static uint8_t  ship_sx;            /* screen x of ship's left edge */
static uint16_t cam;                /* world x at the left screen edge */

static uint8_t  fire_cd, invuln, state_timer, bomb_flash;
static uint8_t  lives, bombs, wave;
static uint16_t score;
static uint8_t  hud_dirty;
static uint16_t over_timer;         /* counts the game over screen down */

/* ----------------------------------------------------------- high score
 * The record sits at the base of cartridge SRAM; sim.h owns its format, this
 * file owns reaching it.  The Makefile asks the linker for MBC5 + RAM +
 * battery (-Wl-yt0x1B) and 4 RAM banks (-Wl-ya4, 32 KB), which is what makes
 * the cart header, the 0xA000 address below and the .sav an emulator writes
 * agree with each other -- change one and you have a game that never saves. */
static uint8_t  __at(0xA000) hs_sram[HS_LEN];
static uint16_t hi_score;
static uint8_t  new_high;

static Lander   en[MAX_ENEMY];
static uint8_t  en_on[MAX_ENEMY];
static uint8_t  en_tgt[MAX_ENEMY];
static uint8_t  en_cd[MAX_ENEMY];
static Target   hum[MAX_HUMAN];
static Target   ptgt;               /* the player, as a mutant's target */

static uint16_t bx[MAX_BUL];
static uint8_t  by[MAX_BUL];
static int8_t   bvx[MAX_BUL], bvy[MAX_BUL];
static uint8_t  blife[MAX_BUL], bown[MAX_BUL];

static uint16_t ex_x[MAX_EXP];
static uint8_t  ex_y[MAX_EXP], ex_t[MAX_EXP], ex_on[MAX_EXP];

static uint8_t  wave_total, wave_spawned, spawn_timer;
static uint8_t  map_w[32];          /* world column held by each BG map column */

static uint8_t  oam_n, oam_prev;

/* ---------------------------------------------------------------- sound
 * All sound goes through these two calls so music can drop in later.
 * Channel 4 (noise) is reserved for SFX. */
static void sfx_init(void)
{
    NR52_REG = 0x80;
    NR50_REG = 0x77;
    NR51_REG = 0xFF;
}

static void sfx_shoot(void)
{
    NR41_REG = 0x30;
    NR42_REG = 0x81;
    NR43_REG = 0x21;
    NR44_REG = 0xC0;
}

static void sfx_boom(void)
{
    NR41_REG = 0x10;
    NR42_REG = 0xF3;
    NR43_REG = 0x63;
    NR44_REG = 0xC0;
}

/* The rescue: the one action that previously had no feedback.  Square channel
 * 1 -- noise stays the shoot/boom channel, and no music exists yet to conflict
 * with ch1.  A short up-chirp for catching a human, a long rising sweep for
 * setting one down (+500).  Frequencies are ear-tuning values. */
static void sfx_catch(void)
{
    NR10_REG = 0x71;        /* sweep: fast, rising */
    NR11_REG = 0x80;        /* 50% duty            */
    NR12_REG = 0x83;        /* volume 8, decay     */
    NR13_REG = 0x7B;        /* ~988 Hz             */
    NR14_REG = 0x87;        /* trigger             */
}

static void sfx_rescue(void)
{
    NR10_REG = 0x7F;        /* sweep: long, steep rise  */
    NR11_REG = 0x80;
    NR12_REG = 0x86;        /* volume 8, slower decay   */
    NR13_REG = 0x00;        /* ~171 Hz start            */
    NR14_REG = 0x85;        /* trigger                  */
}

/* ------------------------------------------------------------------ misc */
static void add_score(uint16_t n)
{
    score = (score > 65535u - n) ? 65535u : score + n;
    hud_dirty = 1;
}

static uint8_t glyph(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(T_FONT_DIGIT + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(T_FONT_ALPHA + (c - 'A'));
    return T_BLANK;
}

static void fmt5(char *d, uint16_t v)
{
    uint8_t i;
    static const uint16_t p[4] = { 10000u, 1000u, 100u, 10u };
    for (i = 0; i < 4; i++) {
        d[i] = '0';
        while (v >= p[i]) { v -= p[i]; d[i]++; }
    }
    d[4] = (char)('0' + v);
}

/* "<6-char label><5 digits>", so SCORE and HI line up under each other. */
static void fmt_label(char *d, const char *label, uint16_t v)
{
    uint8_t i;
    for (i = 0; i < 6; i++) d[i] = label[i];
    fmt5(&d[6], v);
    d[11] = 0;
}

/* SRAM is only mapped in while it is being touched: left enabled, a stray
 * write to 0xA000-0xBFFF is indistinguishable from a save. */
static void hi_load(void)
{
    ENABLE_RAM;
    hi_score = hs_read(hs_sram);
    DISABLE_RAM;
}

static void hi_save(uint16_t v)
{
    ENABLE_RAM;
    hs_write(hs_sram, v);
    DISABLE_RAM;
}

static void win_text(uint8_t x, uint8_t y, const char *s)
{
    uint8_t buf[20];
    uint8_t n = 0;
    while (s[n] && n < 20) { buf[n] = glyph(s[n]); n++; }
    set_win_tiles(x, y, n, 1, buf);
}

static void hud_draw(void)
{
    char s[18];
    uint8_t w = wave;
    s[0] = 'S'; s[1] = 'C';
    fmt5(&s[2], score);
    s[7] = ' '; s[8] = 'L'; s[9] = (char)('0' + lives);
    s[10] = ' '; s[11] = 'B'; s[12] = (char)('0' + bombs);
    s[13] = ' '; s[14] = 'W';
    s[15] = (char)('0' + (w / 10));
    s[16] = (char)('0' + (w % 10));
    s[17] = 0;
    win_text(0, 2, s);
}

static void hud_frame(void)
{
    uint8_t top[20], bot[20], i;
    for (i = 0; i < 20; i++) { top[i] = T_HUD_TOP; bot[i] = T_HUD_BOT; }
    set_win_tiles(0, 0, 20, 1, top);
    set_win_tiles(0, 1, 20, 1, bot);
}

static uint8_t rnd(void) { return (uint8_t)(rng16(&rng_s) >> 4); }

/* ------------------------------------------------------- background world */
static void draw_col(uint8_t wc)
{
    uint8_t buf[18];
    uint8_t r, top = terrain_top(wc);
    for (r = 0; r < 18; r++) {
        if (r < top || r >= 15) buf[r] = T_BLANK;
        else if (r == top)      buf[r] = T_SURFACE;
        else                    buf[r] = T_FILL;
    }
    set_bkg_tiles(wc & 31, 0, 1, 18, buf);
    map_w[wc & 31] = wc;
}

static void ensure_col(uint8_t wc)
{
    wc &= WORLD_COL_MASK;
    if (map_w[wc & 31] != wc) draw_col(wc);
}

/* The camera moves less than one tile per frame, so only the two edge
 * columns can ever be stale. */
static void stream_bg(void)
{
    uint8_t c = (uint8_t)((cam >> 3) & WORLD_COL_MASK);
    ensure_col(c);
    ensure_col((uint8_t)(c + 20));
}

static void draw_bg_full(void)
{
    uint8_t c = (uint8_t)((cam >> 3) & WORLD_COL_MASK), i;
    for (i = 0; i < 32; i++) draw_col((uint8_t)((c + i) & WORLD_COL_MASK));
}

/* -------------------------------------------------------------- entities */
static void explode(uint16_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < MAX_EXP; i++) {
        if (!ex_on[i]) break;
    }
    if (i == MAX_EXP) i = 0;
    ex_x[i] = x; ex_y[i] = y; ex_t[i] = 0; ex_on[i] = 1;
}

static void bullet_fire(uint16_t x, uint8_t y, int8_t vx, int8_t vy,
                        uint8_t owner, uint8_t life)
{
    uint8_t i;
    for (i = 0; i < MAX_BUL; i++) {
        if (!blife[i]) {
            bx[i] = x; by[i] = y; bvx[i] = vx; bvy[i] = vy;
            bown[i] = owner; blife[i] = life;
            return;
        }
    }
}

static void kill_enemy(uint8_t i)
{
    uint8_t t;
    Lander *l = &en[i];
    if (l->state == LANDER_GRAB || l->state == LANDER_CARRY) {
        t = en_tgt[i];
        if (t != NO_TARGET && hum[t].state == HUM_HELD) human_drop(&hum[t]);
    }
    add_score(l->state == LANDER_MUTANT ? 200 : 150);
    explode(l->x, l->y);
    en_on[i] = 0;
    sfx_boom();
}

static uint8_t nearest_ground_human(uint16_t x)
{
    uint8_t i, best = NO_TARGET;
    uint16_t bd = 0xFFFF, d;
    int16_t dx;
    for (i = 0; i < MAX_HUMAN; i++) {
        if (hum[i].state != HUM_GROUND) continue;
        dx = torus_dx(x, hum[i].x);
        d = (uint16_t)(dx < 0 ? -dx : dx);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static uint8_t humans_alive(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_HUMAN; i++) if (hum[i].state != HUM_DEAD) n++;
    return n;
}

static void spawn_enemy(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMY; i++) {
        if (!en_on[i]) {
            en[i].x = rng16(&rng_s) & WORLD_MASK;
            en[i].y = 0;
            en[i].state = LANDER_SEEK;
            en[i].tick = 0;
            en_tgt[i] = NO_TARGET;
            en_cd[i] = (uint8_t)(60 + (rnd() & 63));
            en_on[i] = 1;
            wave_spawned++;
            return;
        }
    }
}

static void wave_setup(void)
{
    wave_total = (uint8_t)(wave < 9 ? 3 + wave : 12);
    wave_spawned = 0;
    spawn_timer = 60;
    hud_dirty = 1;
}

/* Enemy shot aimed roughly at the player. */
static void enemy_shoot(uint8_t i)
{
    int16_t dx = torus_dx(en[i].x, player_x + 8u);
    int16_t dy = (int16_t)(player_y + 4) - (int16_t)(en[i].y + 4);
    int8_t vx = 0, vy = 0;
    if (dx > 24)       vx = 2;
    else if (dx < -24) vx = -2;
    if (dy > 12)       vy = 1;
    else if (dy < -12) vy = -1;
    if (vx == 0 && vy == 0) vy = 2;
    if (vx == 0 && dy < 0) vy = -2;
    if (vx == 0 && dy >= 0) vy = 2;
    bullet_fire(en[i].x + 3u, (uint8_t)(en[i].y + 3), vx, vy, OWN_ENEMY, 90);
}

static uint8_t on_screen(uint16_t wx)
{
    int16_t sx = torus_dx(cam, wx);
    return sx > -8 && sx < 160;
}

static void enemies_update(void)
{
    uint8_t i, t;
    Lander *l;
    ptgt.x = player_x + 4u;
    ptgt.y = player_y;

    for (i = 0; i < MAX_ENEMY; i++) {
        if (!en_on[i]) continue;
        l = &en[i];

        if (l->state == LANDER_MUTANT) {
            lander_step(l, &ptgt);
            if (state == ST_PLAY && on_screen(l->x)) {
                if (en_cd[i]) en_cd[i]--;
                else { enemy_shoot(i); en_cd[i] = (uint8_t)(70 + (rnd() & 31)); }
            }
        } else if (l->state == LANDER_SEEK) {
            t = en_tgt[i];
            if (t == NO_TARGET || hum[t].state != HUM_GROUND) {
                t = nearest_ground_human(l->x);
                en_tgt[i] = t;
            }
            if (t == NO_TARGET) {           /* nobody left to steal: hunt */
                l->state = LANDER_MUTANT;
                l->tick = 0;
                continue;
            }
            lander_step(l, &hum[t]);
            if (l->state == LANDER_GRAB) hum[t].state = HUM_HELD;
        } else {                            /* GRAB / CARRY */
            t = en_tgt[i];
            lander_step(l, &hum[t]);
            hum[t].x = l->x;
            hum[t].y = (uint8_t)(l->y + LANDER_H);
            if (l->state == LANDER_MUTANT) {
                hum[t].state = HUM_DEAD;    /* the human is lost */
                l->tick = 0;
            }
        }
    }
}

static void humans_update(void)
{
    uint8_t i, g, carrying = NO_TARGET;

    for (i = 0; i < MAX_HUMAN; i++)
        if (hum[i].state == HUM_CARRIED) { carrying = i; break; }

    for (i = 0; i < MAX_HUMAN; i++) {
        if (hum[i].state != HUM_FALL) continue;
        human_fall_step(&hum[i], human_ground_y(hum[i].x));
        /* Catch by flying into it -- checked AFTER the step, so a human that
         * touched down this frame is already safe and not catchable, and one
         * that falls past the ship's 8 px band is caught on the way through
         * rather than missed. One at a time: you have one ship. */
        if (carrying == NO_TARGET && state == ST_PLAY &&
            human_catchable(&hum[i]) &&
            span_overlap(player_y, SHIP_H, hum[i].y, HUMAN_H) &&
            torus_overlap(player_x, SHIP_W, hum[i].x, HUMAN_W)) {
            hum[i].state = HUM_CARRIED;
            carrying = i;
            sfx_catch();
        }
    }

    if (carrying == NO_TARGET) return;

    /* Sling it under the hull, centred on the ship -- +4 also lines its
     * ground column up with the ship's, so it is set down on the same tile
     * the ship's own ground clamp used. */
    hum[carrying].x = torus_ahead(player_x, 4);
    hum[carrying].y = (uint8_t)(player_y + SHIP_H);

    g = (uint8_t)(terrain_top(((player_x + 8u) >> 3) & WORLD_COL_MASK) << 3);
    if (human_landable((uint8_t)(player_y + SHIP_H), g)) {
        hum[carrying].y = human_ground_y(hum[carrying].x);
        hum[carrying].state = HUM_GROUND;
        add_score(RESCUE_BONUS);
        sfx_rescue();
    }
}

static void bullets_update(void)
{
    uint8_t i, j;
    int16_t ny;
    for (i = 0; i < MAX_BUL; i++) {
        if (!blife[i]) continue;
        blife[i]--;
        bx[i] = torus_ahead(bx[i], bvx[i]);
        ny = (int16_t)by[i] + bvy[i];
        if (ny < 0 || ny >= (int16_t)PLAY_H) { blife[i] = 0; continue; }
        by[i] = (uint8_t)ny;

        if (bown[i] != OWN_PLAYER) continue;
        for (j = 0; j < MAX_ENEMY; j++) {
            if (en_on[j] &&
                span_overlap(by[i], PBUL_H, en[j].y, LANDER_H) &&
                torus_overlap(bx[i], PBUL_W, en[j].x, ENEMY_W)) {
                kill_enemy(j);
                blife[i] = 0;
                break;
            }
        }
    }
}

static void explosions_update(void)
{
    uint8_t i;
    for (i = 0; i < MAX_EXP; i++) {
        if (!ex_on[i]) continue;
        if (++ex_t[i] >= 16) ex_on[i] = 0;
    }
}

static void world_update(void)
{
    uint8_t i, alive = 0;

    enemies_update();
    humans_update();
    bullets_update();
    explosions_update();

    for (i = 0; i < MAX_ENEMY; i++) if (en_on[i]) alive++;

    if (wave_spawned < wave_total) {
        if (spawn_timer) spawn_timer--;
        else { spawn_enemy(); spawn_timer = 45; }
    } else if (alive == 0 && state == ST_PLAY) {
        add_score((uint16_t)(50u * humans_alive()));   /* wave bonus */
        wave++;
        wave_setup();
    }
}

/* ---------------------------------------------------------------- player */
static void kill_player(void)
{
    uint8_t i;
    explode(player_x, player_y);
    explode(player_x + 8u, player_y);
    sfx_boom();
    /* A human in your hands is dropped, not saved: it falls from the height
     * you were at and the ordinary fall rule decides its fate. */
    for (i = 0; i < MAX_HUMAN; i++)
        if (hum[i].state == HUM_CARRIED) human_drop(&hum[i]);
    if (lives) lives--;
    state = ST_DYING;
    state_timer = 70;
    hud_dirty = 1;
}

static void smart_bomb(void)
{
    uint8_t i;
    bombs--;
    bomb_flash = 8;
    hud_dirty = 1;
    for (i = 0; i < MAX_ENEMY; i++)
        if (en_on[i] && on_screen(en[i].x)) kill_enemy(i);
    for (i = 0; i < MAX_BUL; i++)
        if (blife[i] && bown[i] == OWN_ENEMY) blife[i] = 0;
    sfx_boom();
}

static void player_respawn(void)
{
    uint8_t i;
    player_vx = 0;
    player_y = 48;
    invuln = 120;
    bombs = 3;
    state = ST_PLAY;
    for (i = 0; i < MAX_BUL; i++)
        if (bown[i] == OWN_ENEMY) blife[i] = 0;
    hud_dirty = 1;
}

static void player_update(uint8_t keys, uint8_t pressed)
{
    uint8_t i, target, g;

    /* thrust / drag */
    if (keys & J_LEFT)       { player_vx -= THRUST; facing = -1; }
    else if (keys & J_RIGHT) { player_vx += THRUST; facing = 1; }
    else {
        player_vx -= (player_vx >> 5);
        if (player_vx > -32 && player_vx < 32) player_vx = 0;
    }
    if (player_vx >  MAX_VX) player_vx =  MAX_VX;
    if (player_vx < -MAX_VX) player_vx = -MAX_VX;

    /* vertical: direct control, and the ground is solid */
    if (keys & J_UP)   player_y = (uint8_t)(player_y >= 2 ? player_y - 2 : 0);
    if (keys & J_DOWN) player_y += 2;
    g = (uint8_t)(terrain_top(((player_x + 8u) >> 3) & WORLD_COL_MASK) << 3);
    if (player_y > g - SHIP_H) player_y = (uint8_t)(g - SHIP_H);

    player_x = torus_ahead(player_x, fix_step(&player_xf, player_vx));

    /* ease the ship toward the trailing side so you see further ahead */
    target = (facing > 0) ? SHIP_SX_R : SHIP_SX_L;
    if (ship_sx + 2 <= target)      ship_sx += 2;
    else if (ship_sx >= target + 2) ship_sx -= 2;
    else                            ship_sx = target;
    cam = torus_ahead(player_x, -(int16_t)ship_sx);

    /* weapons */
    if (fire_cd) fire_cd--;
    if ((keys & J_A) && !fire_cd) {
        uint8_t n = 0;
        for (i = 0; i < MAX_BUL; i++)
            if (blife[i] && bown[i] == OWN_PLAYER) n++;
        if (n < MAX_PBUL) {
            if (facing > 0)
                bullet_fire(torus_ahead(player_x, SHIP_W), (uint8_t)(player_y + 3),
                            4, 0, OWN_PLAYER, 28);
            else
                bullet_fire(torus_ahead(player_x, -(int16_t)PBUL_W), (uint8_t)(player_y + 3),
                            -4, 0, OWN_PLAYER, 28);
            sfx_shoot();
            fire_cd = 8;
        }
    }
    if ((pressed & J_B) && bombs) smart_bomb();

    /* damage */
    if (invuln) { invuln--; return; }
    for (i = 0; i < MAX_ENEMY; i++) {
        if (en_on[i] &&
            span_overlap(player_y, SHIP_H, en[i].y, LANDER_H) &&
            torus_overlap(player_x, SHIP_W, en[i].x, ENEMY_W)) {
            kill_enemy(i);
            kill_player();
            return;
        }
    }
    for (i = 0; i < MAX_BUL; i++) {
        if (blife[i] && bown[i] == OWN_ENEMY &&
            span_overlap(player_y, SHIP_H, by[i], EBUL_H) &&
            torus_overlap(player_x, SHIP_W, bx[i], EBUL_W)) {
            blife[i] = 0;
            kill_player();
            return;
        }
    }
}

/* ---------------------------------------------------------------- render */
static void spr(uint8_t k, uint8_t prop, int16_t sx, int16_t sy)
{
    if (oam_n >= 40) return;
    if (sx <= -8 || sx >= 160) return;
    set_sprite_tile(oam_n, T_SPR(k));
    set_sprite_prop(oam_n, prop);
    move_sprite(oam_n, (uint8_t)(sx + 8), (uint8_t)(sy + 16));
    oam_n++;
}

static void blip(uint8_t k, uint16_t wx, uint8_t wy)
{
    int16_t d = torus_dx(player_x, wx);
    spr(k, 0, 79 + (d >> 3), 121 + (wy >> 3));
}

/* One pass per world list, as plain induction loops.  These exist so render()
 * can rotate the CATEGORY order cheaply: a runtime-indexed array access
 * compiles roughly 3x slower on SDCC than an induction one, and rotating
 * within the lists that way cost the game 40% of its frame budget
 * (60 fps -> 37).  Category-level rotation measures free; use it. */
static void draw_enemies(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMY; i++)
        if (en_on[i])
            spr(en[i].state == LANDER_MUTANT ? SPR_MUTANT : SPR_LANDER, 0,
                torus_dx(cam, en[i].x), en[i].y);
}

static void draw_bullets(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BUL; i++)
        if (blife[i])
            spr(bown[i] == OWN_PLAYER ? SPR_BULLET : SPR_EBULLET, 0,
                torus_dx(cam, bx[i]), by[i]);
}

static void draw_humans(void)
{
    uint8_t i;
    for (i = 0; i < MAX_HUMAN; i++)
        if (hum[i].state != HUM_DEAD)
            spr(SPR_HUMAN, 0, torus_dx(cam, hum[i].x), hum[i].y);
}

static void draw_exps(void)
{
    uint8_t i;
    for (i = 0; i < MAX_EXP; i++)
        if (ex_on[i])
            spr(SPR_EXP0 + (ex_t[i] >> 2), 0, torus_dx(cam, ex_x[i]), ex_y[i]);
}

static void render(void)
{
    uint8_t i, n;

    oam_n = 0;

    /* Ship first, never rotated: with the DMG honouring OAM index order at
     * 10 sprites per scanline, the first drawn is the last to be dropped. */
    if (state == ST_PLAY && !(invuln && (frame & 4))) {
        if (facing > 0) {
            spr(SPR_SHIP_L, 0, ship_sx, player_y);
            spr(SPR_SHIP_R, 0, ship_sx + 8, player_y);
        } else {
            spr(SPR_SHIP_R, S_FLIPX, ship_sx, player_y);
            spr(SPR_SHIP_L, S_FLIPX, ship_sx + 8, player_y);
        }
    }

    /* Everything else rotates so the 10-per-scanline cap eats a different
     * victim each frame instead of erasing the same ones for good: each of
     * the four lists takes turns leading.  Kept at category level only --
     * within-list rotation measures at 2-40% of the frame budget on SDCC
     * (see the draw_*() note), and the headroom here is a few hundred
     * cycles. */
    n = frame & 3;
    if (n == 0) {
        draw_enemies(); draw_bullets();
        draw_humans();  draw_exps();
    } else if (n == 1) {
        draw_humans();  draw_exps();
        draw_enemies(); draw_bullets();
    } else if (n == 2) {
        draw_bullets(); draw_humans();
        draw_exps();    draw_enemies();
    } else {
        draw_exps();    draw_enemies();
        draw_bullets(); draw_humans();
    }

    /* scanner, centred on the player */
    if (state == ST_PLAY) blip(SPR_BLIP_B, player_x + 7u, player_y);
    for (i = 0; i < MAX_ENEMY; i++)
        if (en_on[i]) blip(SPR_BLIP_B, en[i].x, en[i].y);
    for (i = 0; i < MAX_HUMAN; i++)
        if (hum[i].state != HUM_DEAD) blip(SPR_BLIP_S, hum[i].x, hum[i].y);

    for (i = oam_n; i < oam_prev; i++) hide_sprite(i);
    oam_prev = oam_n;
}

/* ------------------------------------------------------------- game flow */
static void new_game(void)
{
    uint8_t i;
    uint16_t x;

    DISPLAY_OFF;

    /* The title swapped the BG tile bank for the title image, so the game's
     * own tiles go back before the terrain map is drawn (one 1.4 KB load). */
    set_bkg_data(0, GFX_TILE_COUNT, gfx_tiles);
    SHOW_WIN;

    score = 0; lives = 3; bombs = 3; wave = 1;
    player_x = 500; player_xf = 0; player_vx = 0; player_y = 48;
    facing = 1; ship_sx = SHIP_SX_R;
    cam = torus_ahead(player_x, -(int16_t)ship_sx);
    fire_cd = 0; invuln = 90; bomb_flash = 0;

    for (i = 0; i < MAX_ENEMY; i++) en_on[i] = 0;
    for (i = 0; i < MAX_BUL; i++) blife[i] = 0;
    for (i = 0; i < MAX_EXP; i++) ex_on[i] = 0;
    for (i = 0; i < MAX_HUMAN; i++) {
        x = (uint16_t)(40u + i * 170u);
        hum[i].x = x;
        hum[i].y = human_ground_y(x);
        hum[i].state = HUM_GROUND;
        hum[i].aux = 0;
    }
    wave_setup();

    for (i = 0; i < 40; i++) hide_sprite(i);
    oam_prev = 0;
    fill_win_rect(0, 0, 20, 12, T_BLANK);
    WY_REG = PLAY_H;
    hud_frame();
    hud_draw();
    hud_dirty = 0;
    draw_bg_full();
    SCX_REG = (uint8_t)(cam & 255u);
    BGP_REG = 0xE4;

    rng_s ^= (uint16_t)(DIV_REG | 1u);
    if (!rng_s) rng_s = 0xACE1;

    state = ST_PLAY;
    DISPLAY_ON;
}

/* "HI 00000" across the title art, as sprites rather than tiles.  Two reasons:
 * the title image owns the whole BG tile bank (0..136) and is white-on-black,
 * so a font-tile glyph there would come out a white box with dark strokes; and
 * it is a number that changes, which no regenerated image can follow.  A sprite
 * draws only its stroked pixels -- colour 0 is transparent -- so it drops onto
 * the art clean.  It sits on the white strip under the ship and above the
 * terrain band, and strokes in colour 2, which is the dark grey the title's own
 * lettering uses (OBP0 = 0xE4, the palette the game's sprites already draw on). */
static void title_hi_draw(void)
{
    static const uint8_t label[2] = { T_FONT_ALPHA + 7, T_FONT_ALPHA + 8 }; /* H I */
    char s[5];
    uint8_t i, n;

    fmt5(s, hi_score);

    /* 8x16 sprites pair an even tile with the odd one above it, so each glyph
     * needs a blank bottom half.  T_BLANK is 16 zero bytes: nothing drawn. */
    for (i = 0; i < 2; i++) {
        set_sprite_data((uint8_t)(T_TITLE_GLYPH + 20 + 2 * i), 1,
                        &gfx_tiles[label[i] * 16]);
        set_sprite_data((uint8_t)(T_TITLE_GLYPH + 21 + 2 * i), 1,
                        &gfx_tiles[T_BLANK * 16]);
    }
    for (i = 0; i < 10; i++) {
        set_sprite_data((uint8_t)(T_TITLE_GLYPH + 2 * i), 1,
                        &gfx_tiles[(T_FONT_DIGIT + i) * 16]);
        set_sprite_data((uint8_t)(T_TITLE_GLYPH + 2 * i + 1), 1,
                        &gfx_tiles[T_BLANK * 16]);
    }

    /* x/y below are screen coordinates; move_sprite() wants them offset by the
     * 8x16 origin, which is where the +8/+16 comes from.  H at TITLE_HI_X, I
     * one cell along, a blank cell, then the five digits.  Props stay 0, i.e.
     * OBP0, the same palette the game's own sprites draw on. */
    for (i = 0; i < 2; i++) {
        n = i;
        set_sprite_tile(n, (uint8_t)(T_TITLE_GLYPH + 20 + 2 * i));
        set_sprite_prop(n, 0);
        move_sprite(n, (uint8_t)(TITLE_HI_X + 8 + 8 * i), (uint8_t)(TITLE_HI_Y + 16));
    }
    for (i = 0; i < 5; i++) {
        n = (uint8_t)(2 + i);
        set_sprite_tile(n, (uint8_t)(T_TITLE_GLYPH + 2 * (uint8_t)(s[i] - '0')));
        set_sprite_prop(n, 0);
        move_sprite(n, (uint8_t)(TITLE_HI_X + 32 + 8 * i), (uint8_t)(TITLE_HI_Y + 16));
    }
}

/* Boot screen: a full-screen title image (tools/mktitle.py -> title.h) shown
 * by swapping the BG tile bank for its own; new_game() swaps back.  Same
 * shape as game_over_screen(), and it shares ST_OVER's handling in update():
 * draw once, wait for START, call new_game().  The window layer is hidden --
 * "PRESS START" is part of the image. */
static void title_screen(void)
{
    uint8_t i;

    for (i = 0; i < 40; i++) hide_sprite(i);
    oam_prev = 0;

    DISPLAY_OFF;
    set_bkg_data(0, TITLE_TILE_COUNT, title_tiles);
    set_bkg_tiles(0, 0, 20, 18, title_map);
    HIDE_WIN;
    SCX_REG = 0;
    SCY_REG = 0;
    BGP_REG = 0xE4;
    title_hi_draw();
    DISPLAY_ON;
    state = ST_TITLE;
}

static void game_over_screen(void)
{
    char s[12];
    uint8_t i;

    for (i = 0; i < 40; i++) hide_sprite(i);
    oam_prev = 0;

    /* Bank it here, before the screen is even built, and only when it beats
     * the record: this write is the one thing in the game that outlives the
     * console being switched off. */
    new_high = (uint8_t)(score > hi_score);
    if (new_high) {
        hi_score = score;
        hi_save(hi_score);
    }

    DISPLAY_OFF;
    fill_win_rect(0, 0, 20, 12, T_BLANK);
    WY_REG = 40;
    /* No "!" on the end: glyph() knows digits and capitals, and anything else
     * comes out as a blank, which is a silent way to lose a character. */
    win_text(new_high ? 3 : 5, 3, new_high ? "NEW HIGH SCORE" : "GAME OVER");
    fmt_label(s, "SCORE ", score);
    win_text(4, 5, s);
    fmt_label(s, "HI    ", hi_score);
    win_text(4, 7, s);
    win_text(4, 9, "PRESS START");
    BGP_REG = 0xE4;
    DISPLAY_ON;
    over_timer = OVER_HOLD;
    state = ST_OVER;
}

static void update(void)
{
    uint8_t keys = joypad();
    uint8_t pressed = (uint8_t)(keys & ~prev_keys);
    prev_keys = keys;
    frame++;

    if (state == ST_PLAY) {
        player_update(keys, pressed);
        world_update();
        if (state == ST_PLAY && humans_alive() == 0) { game_over_screen(); return; }
    } else if (state == ST_DYING) {
        world_update();
        if (state_timer) state_timer--;
        if (!state_timer) {
            if (lives == 0 || humans_alive() == 0) { game_over_screen(); return; }
            player_respawn();
        }
    } else {                                /* ST_OVER / ST_TITLE: wait for START */
        if (pressed & J_START) { new_game(); return; }
        /* Game over gets bored and goes back to the title, which is also where
         * the score it just banked is on show.  START cuts the wait short and
         * starts play directly, as it always did; the title has no countdown. */
        if (state == ST_OVER && !--over_timer) title_screen();
        return;
    }
    render();
}

void main(void)
{
    sfx_init();
    DISPLAY_OFF;
    hi_load();
    /* GBDK keeps BG tiles 0-127 at 0x9000 and sprite tiles at 0x8000, so the
     * same generated array is loaded twice: whole set as BG, sprites as OBJ. */
    set_bkg_data(0, GFX_TILE_COUNT, gfx_tiles);
    set_sprite_data(SPR_BASE, GFX_TILE_COUNT - SPR_BASE, &gfx_tiles[SPR_BASE * 16]);
    BGP_REG = 0xE4;
    OBP0_REG = 0xE4;
    OBP1_REG = 0xE4;
    SPRITES_8x16;
    SHOW_BKG;
    SHOW_WIN;
    SHOW_SPRITES;
    WX_REG = 7;
    WY_REG = PLAY_H;
    rng_s = 0xACE1;
    DISPLAY_ON;

    /* Super Game Boy border, once, at boot.  The SGB reads the CHR_TRN/PCT_TRN
     * payloads off the rendered screen, so this has to come after DISPLAY_ON --
     * and it trashes GB VRAM getting there, which costs nothing here because
     * title_screen() reloads the tile bank it wants on the next line.  Four
     * frames first: a PAL SNES needs that delay at startup or no border shows.
     * sgb_check() is false on a DMG, so an ordinary Game Boy boots exactly as
     * it did before -- all of this is behind that gate. */
    {
        uint8_t i;
        for (i = 0; i != 4; i++) vsync();
    }
    if (sgb_check()) {
        set_sgb_border((unsigned char *)border_data_tiles, sizeof(border_data_tiles),
                       (unsigned char *)border_data_map, sizeof(border_data_map),
                       (unsigned char *)border_data_palettes,
                       sizeof(border_data_palettes));
    }

    title_screen();

    while (1) {
        update();
        wait_vbl_done();                    /* sprites (shadow OAM) copied now */
        if (state == ST_PLAY || state == ST_DYING) {
            SCX_REG = (uint8_t)(cam & 255u); /* same frame as the sprites */
            stream_bg();
            if (hud_dirty) { hud_draw(); hud_dirty = 0; }
            if (bomb_flash) { bomb_flash--; BGP_REG = 0x1B; }
            else BGP_REG = 0xE4;
        }
    }
}
