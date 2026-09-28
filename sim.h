/* sim.h - GB-Protector's hardware-independent rules.
 *
 * stdint.h only: no gb/gb.h.  tests/test_sim.c compiles this exact file with
 * plain gcc, so `make test` exercises the real code and can genuinely fail.
 *
 * The world is a torus WORLD_W pixels wide.  Every x-distance and x-overlap in
 * the game goes through torus_dx(), because the classic bug in a wrap-around
 * shooter is things colliding (or failing to) across the seam.
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

/* World geometry.  Power-of-two so wrapping is a mask, never a modulo.
 * The hardware BG map is 32 tiles wide, so the game streams one tile column
 * per 8 px of camera movement (see main.c). */
#define WORLD_W        1024u
#define WORLD_MASK     1023u
#define WORLD_COLS     128u          /* WORLD_W / 8 */
#define WORLD_COL_MASK 127u

#define HUMAN_H        12u           /* sprite art height of a human   */
#define FALL_SAFE      40u           /* a longer fall kills            */
#define FALL_STEP      1u            /* px a falling human moves per tick.
                                        Half the ship's climb rate, so you
                                        can always dive to meet it -- and
                                        the catch window is ~20 frames. */

/* ------------------------------------------------------------ torus math */

/* Signed shortest x-distance from a to b on the torus: -512..511. */
static int16_t torus_dx(uint16_t a, uint16_t b)
{
    int16_t d = (int16_t)((b - a) & WORLD_MASK);
    if (d >= 512) d -= 1024;
    return d;
}

/* x moved by d pixels (d may be negative), wrapped. */
static uint16_t torus_ahead(uint16_t x, int16_t d)
{
    return (uint16_t)((x + d) & WORLD_MASK);
}

/* Do [ax, ax+aw) and [bx, bx+bw) overlap in x, seam included? */
static uint8_t torus_overlap(uint16_t ax, uint8_t aw, uint16_t bx, uint8_t bw)
{
    int16_t d = torus_dx(ax, bx);          /* b's left edge relative to a's */
    return (d < (int16_t)aw) && (d > -(int16_t)bw);
}

/* Plain (non-wrapping) overlap of [ay, ay+ah) and [by, by+bh). */
static uint8_t span_overlap(uint8_t ay, uint8_t ah, uint8_t by, uint8_t bh)
{
    return ((int16_t)by < (int16_t)ay + ah) && ((int16_t)ay < (int16_t)by + bh);
}

/* ------------------------------------------------------------- 8.8 fixed */

/* Advance a sub-pixel position by velocity v (8.8 fixed, px/frame).
 * *frac holds the fractional byte; returns the whole-pixel delta. */
static int16_t fix_step(uint8_t *frac, int16_t v)
{
    int16_t acc = (int16_t)*frac + v;
    *frac = (uint8_t)(acc & 0xFF);
    return acc >> 8;
}

/* ---------------------------------------------------------------- random */

/* 16-bit xorshift (7,9,8): full period 65535 from any non-zero seed. */
static uint16_t rng16(uint16_t *s)
{
    uint16_t x = *s;
    x ^= (uint16_t)(x << 7);
    x ^= (uint16_t)(x >> 9);
    x ^= (uint16_t)(x << 8);
    *s = x;
    return x;
}

/* --------------------------------------------------------------- terrain */

/* Row (in 8 px tiles) where the ground surface starts at world tile column
 * `col`.  Deterministic and periodic in WORLD_COLS so the seam is invisible:
 * a 16-column ripple plus a 64-column swell, both dividing 128. */
static uint8_t terrain_top(uint16_t col)
{
    uint8_t a, b;
    col &= WORLD_COL_MASK;
    a = (uint8_t)(col & 15u);
    if (a >= 8) a = (uint8_t)(15u - a);           /* 0..7  */
    b = (uint8_t)(col & 63u);
    if (b >= 32) b = (uint8_t)(63u - b);          /* 0..31 */
    return (uint8_t)(10u + (a >> 2) + (b >> 3));  /* 10..14 */
}

/* y (px) of the top of a human standing at world x. */
static uint8_t human_ground_y(uint16_t x)
{
    return (uint8_t)((terrain_top((x + 4u) >> 3) << 3) - HUMAN_H);
}

/* ------------------------------------------------- lander state machine */

#define LANDER_SEEK   0     /* flying toward a human on the ground      */
#define LANDER_GRAB   1     /* has hold of the human, about to lift off */
#define LANDER_CARRY  2     /* rising with the human                    */
#define LANDER_MUTANT 3     /* hunting the player (fast, shoots)        */

#define LANDER_H      8u    /* hitbox / art height of an enemy          */
#define LANDER_TOP_Y  0u    /* reaching this y while carrying mutates   */

#define HUM_GROUND 0
#define HUM_HELD   1
#define HUM_FALL   2
#define HUM_DEAD   3
#define HUM_CARRIED 4   /* in the ship's hands, being taken down */

typedef struct {
    uint16_t x;             /* left edge, world px */
    uint8_t  y;             /* top edge, screen px */
    uint8_t  state;
    uint8_t  tick;
} Lander;

/* A thing a lander steers toward: a human, or the player (for mutants). */
typedef struct {
    uint16_t x;
    uint8_t  y;
    uint8_t  state;         /* HUM_* (unused for the player)            */
    uint8_t  aux;           /* height a falling human started from      */
} Target;

/* Advance one lander by one 60 Hz tick.  `t` is the human it is seeking or
 * carrying, or the player once it has mutated.  The caller owns the human:
 * it marks it HUM_HELD on SEEK->GRAB and kills it on CARRY->MUTANT. */
static void lander_step(Lander *l, const Target *t)
{
    int16_t dx = torus_dx(l->x, t->x);

    switch (l->state) {
    case LANDER_SEEK:
        l->tick++;
        if (dx > 24 || dx < -24) {          /* far: close the distance */
            l->x = torus_ahead(l->x, dx > 0 ? 1 : -1);
            break;
        }
        if (l->tick & 1) break;             /* near: half speed */
        if (dx > 1)  l->x = torus_ahead(l->x, 1);
        if (dx < -1) l->x = torus_ahead(l->x, -1);
        if (dx <= 2 && dx >= -2 && (uint16_t)l->y + LANDER_H >= t->y) {
            l->state = LANDER_GRAB;
            l->tick = 0;
        } else if ((uint16_t)l->y + LANDER_H < t->y) {
            l->y++;
        }
        break;

    case LANDER_GRAB:
        if (++l->tick >= 8) {
            l->state = LANDER_CARRY;
            l->tick = 0;
        }
        break;

    case LANDER_CARRY:
        if (++l->tick >= 3) {
            l->tick = 0;
            if (l->y > LANDER_TOP_Y) l->y--;
            if (l->y <= LANDER_TOP_Y) l->state = LANDER_MUTANT;
        }
        break;

    default:                                /* LANDER_MUTANT: chase */
        l->tick++;
        if (dx > 0) l->x = torus_ahead(l->x, 1);
        if (dx < 0) l->x = torus_ahead(l->x, -1);
        if (!(l->tick & 1)) {
            if (l->y < t->y) l->y++;
            else if (l->y > t->y) l->y--;
        }
        break;
    }
}

/* Advance one falling human by one tick (FALL_STEP px).  Lands safe, or dies
 * if the fall was longer than FALL_SAFE. */
static void human_fall_step(Target *h, uint8_t ground_y)
{
    if (h->state != HUM_FALL) return;
    if ((uint16_t)h->y + FALL_STEP < ground_y) {
        h->y += FALL_STEP;
        return;
    }
    h->y = ground_y;
    h->state = ((int16_t)ground_y - (int16_t)h->aux > (int16_t)FALL_SAFE)
                   ? HUM_DEAD : HUM_GROUND;
}

/* ------------------------------------------------------------- the rescue
 * A human knocked loose falls; the ship rescues it by flying INTO it, then
 * sets it down by riding the deck.  Both halves are one condition, and both
 * are the kind of condition that is invisible until you play -- so they are
 * named here, where the host tests can reach them. */

/* Can the ship catch this one?  Only a human in free fall.  One on the
 * ground is already safe, one a lander holds has to be shot free first, and
 * one already in your hands is in your hands. */
static uint8_t human_catchable(const Target *h)
{
    return (uint8_t)(h->state == HUM_FALL);
}

/* Knock a human loose.  It falls from where it is and FALL_SAFE decides
 * whether that was survivable.  Shooting a carrier and losing the ship that
 * was carrying one drop it exactly the same way. */
static void human_drop(Target *h)
{
    h->state = HUM_FALL;
    h->aux = h->y;
}

/* Can the ship set a carried human down?  main.c clamps the ship to
 * `ground - SHIP_H`, so its underside reaching the deck IS "flying low
 * enough" -- there is no second altitude to tune and keep in sync. */
static uint8_t human_landable(uint8_t ship_bottom, uint8_t ground_y)
{
    return (uint8_t)(ship_bottom >= ground_y);
}

/* ----------------------------------------------------------- high score
 * main.c owns the cartridge SRAM; this file owns what the bytes in it mean, so
 * "is this a save or a brand new cart's garbage?" is a question the host tests
 * can answer.  Two magic bytes AND a checksum, because a fresh cart reads as
 * arbitrary values and the power can drop partway through a write -- the data
 * has to vouch for itself rather than trust a flag elsewhere. */
#define HS_MAGIC0 0x47u         /* 'G' */
#define HS_MAGIC1 0x50u         /* 'P' */
#define HS_LEN    5u            /* magic, magic, score hi, score lo, check */

static uint8_t hs_check(const uint8_t *b)
{
    return (uint8_t)(HS_MAGIC0 ^ HS_MAGIC1 ^ b[2] ^ b[3]);
}

/* The saved best, or 0 if the record does not check out. */
static uint16_t hs_read(const uint8_t *b)
{
    if (b[0] != HS_MAGIC0 || b[1] != HS_MAGIC1 || b[4] != hs_check(b)) return 0;
    return (uint16_t)(((uint16_t)b[2] << 8) | b[3]);
}

static void hs_write(uint8_t *b, uint16_t v)
{
    b[0] = HS_MAGIC0;
    b[1] = HS_MAGIC1;
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
    b[4] = hs_check(b);
}

#endif /* SIM_H */
