/* Host tests for sim.h.  Build and run with `make test` (plain gcc). */
#include <stdio.h>
#include <stdlib.h>
#include "../sim.h"

static int checks = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        exit(1); \
    } \
} while (0)

static void test_torus_dx(void)
{
    uint16_t a, b;
    CHECK(torus_dx(1020, 4) == 8);          /* across the seam, rightwards */
    CHECK(torus_dx(4, 1020) == -8);         /* and leftwards               */
    CHECK(torus_dx(0, 511) == 511);
    CHECK(torus_dx(0, 512) == -512);
    CHECK(torus_dx(100, 100) == 0);
    for (a = 0; a < WORLD_W; a += 7) {
        for (b = 0; b < WORLD_W; b++) {
            int16_t d = torus_dx(a, b);
            CHECK(d >= -512 && d <= 511);
            CHECK(torus_ahead(a, d) == b);  /* d really gets you to b      */
            if (d != -512) CHECK(torus_dx(b, a) == -d);
        }
    }
}

static void test_torus_ahead(void)
{
    CHECK(torus_ahead(1020, 8) == 4);
    CHECK(torus_ahead(4, -8) == 1020);
    CHECK(torus_ahead(0, -1) == 1023);
    CHECK(torus_ahead(1023, 1) == 0);
    CHECK(torus_ahead(500, 0) == 500);
}

/* Reference implementation: mark pixels on the torus and intersect. */
static uint8_t brute_overlap(uint16_t ax, uint8_t aw, uint16_t bx, uint8_t bw)
{
    uint16_t i, j;
    for (i = 0; i < aw; i++)
        for (j = 0; j < bw; j++)
            if (((ax + i) & WORLD_MASK) == ((bx + j) & WORLD_MASK)) return 1;
    return 0;
}

static void test_torus_overlap(void)
{
    uint16_t ax, bx;
    CHECK(torus_overlap(1020, 16, 4, 8));       /* straddles the seam      */
    CHECK(!torus_overlap(1020, 16, 20, 8));
    CHECK(!torus_overlap(100, 16, 116, 16));    /* touching, not overlapping */
    CHECK(!torus_overlap(100, 16, 84, 16));
    CHECK(torus_overlap(100, 16, 85, 16));
    CHECK(torus_overlap(100, 16, 115, 16));
    for (ax = 0; ax < WORLD_W; ax += 3) {
        for (bx = 0; bx < WORLD_W; bx++) {
            CHECK(torus_overlap(ax, 16, bx, 8) == brute_overlap(ax, 16, bx, 8));
        }
    }
    CHECK(torus_overlap(0, 4, 1022, 4) == brute_overlap(0, 4, 1022, 4));
}

static void test_span_overlap(void)
{
    CHECK(span_overlap(10, 8, 17, 4));
    CHECK(!span_overlap(10, 8, 18, 4));
    CHECK(span_overlap(10, 8, 7, 4));
    CHECK(!span_overlap(10, 8, 6, 4));
    CHECK(span_overlap(250, 8, 252, 8));        /* no uint8 overflow trouble */
}

static void test_fix_step(void)
{
    uint8_t f = 0;
    int16_t total = 0;
    int i;

    CHECK(fix_step(&f, 128) == 0);              /* half a pixel...         */
    CHECK(fix_step(&f, 128) == 1);              /* ...then a whole one     */

    f = 0; total = 0;
    for (i = 0; i < 256; i++) total += fix_step(&f, 100);
    CHECK(total == 100);                        /* no drift over time      */

    f = 0; total = 0;
    for (i = 0; i < 256; i++) total += fix_step(&f, -100);
    CHECK(total == -100);                       /* symmetric when negative */

    f = 0;
    CHECK(fix_step(&f, -128) == -1);            /* floor toward -inf...    */
    CHECK(fix_step(&f, -128) == 0);             /* ...but sums correctly   */
}

static void test_rng(void)
{
    uint16_t s = 0xACE1, first, x;
    int i;
    first = s;
    for (i = 0; i < 1000; i++) {
        x = rng16(&s);
        CHECK(x != 0);
        CHECK(x != first);
    }
    CHECK(s != 0xACE1);
}

static void test_terrain(void)
{
    uint16_t c;
    uint8_t lo = 255, hi = 0;
    for (c = 0; c < WORLD_COLS; c++) {
        uint8_t t = terrain_top(c);
        int diff;
        CHECK(t >= 10 && t <= 14);
        CHECK(terrain_top(c + WORLD_COLS) == t);        /* periodic        */
        diff = (int)terrain_top(c + 1) - (int)t;
        CHECK(diff >= -2 && diff <= 2);                 /* no cliffs; the seam is c=127 */
        if (t < lo) lo = t;
        if (t > hi) hi = t;
    }
    CHECK(hi > lo);                                     /* not flat        */
    for (c = 0; c < WORLD_W; c++) {
        uint8_t g = human_ground_y(c);
        CHECK(g >= 64 && g <= 100);
    }
}

/* SEEK a human, grab, rise, mutate - all in the right order. */
static void test_lander_abduction(void)
{
    Lander l = { 90, 0, LANDER_SEEK, 0 };
    Target h = { 100, 80, HUM_GROUND, 0 };
    int i;

    for (i = 0; i < 2000 && l.state == LANDER_SEEK; i++) lander_step(&l, &h);
    CHECK(l.state == LANDER_GRAB);
    CHECK(torus_dx(l.x, h.x) >= -2 && torus_dx(l.x, h.x) <= 2);
    CHECK((int)l.y + LANDER_H >= h.y);

    for (i = 0; i < 7; i++) lander_step(&l, &h);
    CHECK(l.state == LANDER_GRAB);                      /* 8 ticks to grab */
    lander_step(&l, &h);
    CHECK(l.state == LANDER_CARRY);

    for (i = 0; i < 5000 && l.state == LANDER_CARRY; i++) lander_step(&l, &h);
    CHECK(l.state == LANDER_MUTANT);
    CHECK(l.y == LANDER_TOP_Y);
}

/* A lander at 1020 seeking a human at 8 must go 12 px right, not 1012 left. */
static void test_lander_seam(void)
{
    Lander l = { 1020, 0, LANDER_SEEK, 0 };
    Target h = { 8, 80, HUM_GROUND, 0 };
    int i;
    for (i = 0; i < 400 && l.state == LANDER_SEEK; i++) {
        lander_step(&l, &h);
        CHECK(l.x >= 1000 || l.x <= 30);                /* never the long way */
    }
    CHECK(l.state == LANDER_GRAB);
    CHECK(torus_dx(l.x, 8) >= -2 && torus_dx(l.x, 8) <= 2);
}

static void test_mutant_chase(void)
{
    Lander m = { 1000, 10, LANDER_MUTANT, 0 };
    Target p = { 20, 50, 0, 0 };
    int i;
    for (i = 0; i < 200; i++) lander_step(&m, &p);
    CHECK(torus_dx(m.x, p.x) == 0);                     /* crossed the seam */
    CHECK(m.y == 50);
}

static void test_human_fall(void)
{
    Target h;
    int i;

    h.x = 100; h.y = 20; h.state = HUM_FALL; h.aux = 20;
    for (i = 0; i < 200 && h.state == HUM_FALL; i++) human_fall_step(&h, 96);
    CHECK(h.state == HUM_DEAD);                         /* 76 px: too far  */

    h.y = 70; h.state = HUM_FALL; h.aux = 70;
    for (i = 0; i < 200 && h.state == HUM_FALL; i++) human_fall_step(&h, 96);
    CHECK(h.state == HUM_GROUND);                       /* 26 px: survives */
    CHECK(h.y == 96);

    h.state = HUM_GROUND; h.y = 50;
    human_fall_step(&h, 96);
    CHECK(h.y == 50);                                   /* only falls if HUM_FALL */
}

/* The rescue: catch a falling human by flying into it, carry it down, set it
 * on the deck. Only the conditions live in sim.h; main.c does the flying. */
static void test_human_rescue(void)
{
    Target h;
    int i;

    /* Only free fall is catchable: the ground is already safe, a lander's
     * human has to be shot free first, and one in your hands is in your
     * hands. */
    h.x = 100; h.y = 60; h.aux = 0;
    h.state = HUM_FALL;    CHECK(human_catchable(&h));
    h.state = HUM_GROUND;  CHECK(!human_catchable(&h));
    h.state = HUM_HELD;    CHECK(!human_catchable(&h));
    h.state = HUM_CARRIED; CHECK(!human_catchable(&h));
    h.state = HUM_DEAD;    CHECK(!human_catchable(&h));

    /* Dropping remembers the height it fell from, or FALL_SAFE cannot judge
     * it -- and a shot carrier and a lost ship must drop it the same way. */
    h.y = 30; h.aux = 0; h.state = HUM_HELD;
    human_drop(&h);
    CHECK(h.state == HUM_FALL);
    CHECK(h.aux == 30);

    /* Setting one down needs the ship at the deck, not merely near it. */
    CHECK(human_landable(96, 96));       /* riding the surface   */
    CHECK(!human_landable(95, 96));      /* a pixel above it     */
    CHECK(human_landable(100, 96));      /* below it, as clamped */

    /* The same 76 px drop, twice. Uncaught it kills; caught it does not --
     * that difference is the whole rescue. */
    h.x = 200; h.y = 20; h.aux = 20; h.state = HUM_FALL;
    for (i = 0; i < 200 && h.state == HUM_FALL; i++) human_fall_step(&h, 96);
    CHECK(h.state == HUM_DEAD);

    h.x = 200; h.y = 20; h.aux = 20; h.state = HUM_FALL;
    for (i = 0; i < 10; i++) human_fall_step(&h, 96);
    CHECK(h.y == 30);
    CHECK(human_catchable(&h));          /* the ship gets there in time */
    h.state = HUM_CARRIED;
    for (i = 0; i < 200; i++) human_fall_step(&h, 96);
    CHECK(h.state == HUM_CARRIED);       /* carried is not falling */
    CHECK(h.y == 30);
}

int main(void)
{
    test_torus_dx();
    test_torus_ahead();
    test_torus_overlap();
    test_span_overlap();
    test_fix_step();
    test_rng();
    test_terrain();
    test_lander_abduction();
    test_lander_seam();
    test_mutant_chase();
    test_human_fall();
    test_human_rescue();
    printf("OK: %d checks passed\n", checks);
    return 0;
}
