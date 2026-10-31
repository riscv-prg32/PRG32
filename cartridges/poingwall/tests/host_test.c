/* Drives the cartridge with a scripted room: the fake multiplayer API below
 * plays the other consoles and records what this one publishes. */
#include "prg32.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void poingwall_init(void);
void poingwall_update(void);
void poingwall_draw(void);

#define FLAG_READY 1u
#define FLAG_AUTHORITY 2u
#define W 320
#define BALL_RADIUS 30

static uint32_t now_ms;
static uint32_t local_id;
static uint32_t local_input;
static prg32_player_state_t peers[PRG32_MP_MAX_PEERS];
static int peer_count;
static prg32_player_state_t published;
static int ball_rects;

uint32_t prg32_ticks_ms(void) { return now_ms; }
uint32_t prg32_input_read(void) { return local_input; }
void prg32_gfx_clear_indexed(uint8_t index) { (void)index; ball_rects = 0; }
void prg32_gfx_pixel_indexed(int x, int y, uint8_t index) {
    (void)index;
    assert(x >= 0 && x < W && y >= 0 && y < 200);
}
void prg32_gfx_rect_indexed(int x, int y, int w, int h, uint8_t index) {
    assert(w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= W && y + h <= 200);
    if (index >= 32) ++ball_rects;
}
void prg32_gfx_text8(int x, int y, const char *text, uint16_t fg, uint16_t bg) {
    (void)x; (void)y; (void)text; (void)fg; (void)bg;
}
void prg32_palette_set(uint8_t index, uint16_t rgb565) { (void)index; (void)rgb565; }
uint16_t prg32_palette_get(uint8_t index) { (void)index; return 0; }
void prg32_audio_note(uint8_t channel, uint8_t wave, uint8_t note, uint8_t velocity, uint16_t duration_ms) {
    (void)channel; (void)wave; (void)note; (void)velocity; (void)duration_ms;
}
void prg32_multiplayer_init(void) {}
bool prg32_multiplayer_available(void) { return true; }
int prg32_multiplayer_join(const char *signature, uint32_t flags) {
    (void)signature; (void)flags;
    return 0;
}
void prg32_multiplayer_tick(void) {}
uint32_t prg32_multiplayer_local_player_id(void) { return local_id; }
int prg32_multiplayer_set_local_state(int16_t x, int16_t y, uint16_t sprite, uint16_t flags) {
    published.x = x; published.y = y; published.sprite = sprite; published.flags = flags;
    published.frame++;
    return 0;
}
int prg32_multiplayer_set_input(uint32_t input) { published.input = input; return 0; }
int prg32_multiplayer_get_peer_count(void) { return peer_count; }
int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out) {
    if (index < 0 || index >= peer_count) return -1;
    *out = peers[index];
    return 0;
}

static void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 33) {
        now_ms += 33;
        poingwall_update();
        poingwall_draw();
    }
}

static void add_peer(uint32_t id, uint16_t flags) {
    memset(&peers[peer_count], 0, sizeof(peers[0]));
    peers[peer_count].player_id = id;
    peers[peer_count].flags = flags;
    ++peer_count;
}

static void test_waits_for_welcome_before_leading(void) {
    local_id = 0;
    peer_count = 0;
    poingwall_init();
    run(2000);
    assert((published.flags & (FLAG_READY | FLAG_AUTHORITY)) == 0);
    puts("no claim before welcome: PASS");
}

static void test_alone_leads_after_settling(void) {
    local_id = 500;
    run(300);
    assert((published.flags & FLAG_AUTHORITY) == 0);
    run(1000);
    assert(published.flags & FLAG_READY);
    assert(published.flags & FLAG_AUTHORITY);
    puts("alone, leads after settling: PASS");
}

static void test_ball_stays_on_one_screen_alone(void) {
    int16_t lowest = 32767, highest = -32768;
    int16_t lowest_y = 32767, highest_y = -32768;
    for (int i = 0; i < 400; ++i) {
        run(33);
        if (published.x < lowest) lowest = published.x;
        if (published.x > highest) highest = published.x;
        if (published.y < lowest_y) lowest_y = published.y;
        if (published.y > highest_y) highest_y = published.y;
    }
    assert(lowest == BALL_RADIUS);
    assert(highest == W - BALL_RADIUS);
    assert(lowest_y > 0 && highest_y < 200 * 16);
    puts("ball covers the wall and bounces at its ends: PASS");
}

static void test_wall_grows_with_a_peer(void) {
    int16_t highest = -32768;
    add_peer(900, FLAG_READY);
    for (int i = 0; i < 800; ++i) {
        run(33);
        peers[0].frame++;
        if (published.x > highest) highest = published.x;
    }
    assert(highest == 2 * W - BALL_RADIUS);
    assert(published.flags & FLAG_AUTHORITY);
    assert((published.flags >> 8) == 2);
    puts("wall of two: ball crosses into the second screen: PASS");
}

static void test_unready_lower_id_does_not_take_over(void) {
    add_peer(100, 0);
    run(100);
    assert(published.flags & FLAG_AUTHORITY);
    puts("a console still settling does not take the ball: PASS");
}

static void test_lower_ready_id_takes_the_ball_and_is_followed(void) {
    peers[1].flags = FLAG_READY | FLAG_AUTHORITY;
    peers[1].x = 400;
    peers[1].y = 800;
    peers[1].sprite = (uint16_t)((uint16_t)(uint8_t)(int8_t)-20 << 8 | 40);
    peers[1].frame = 77;
    run(33);
    assert((published.flags & FLAG_AUTHORITY) == 0);
    assert(published.x >= 400 && published.x <= 403);
    puts("lower ready ID leads; this console follows its ball: PASS");
}

static void test_next_lowest_takes_over_where_the_ball_was(void) {
    int16_t x = published.x;
    peer_count = 1; /* The leader, peers[1], has gone. */
    run(33);
    assert(published.flags & FLAG_AUTHORITY);
    assert(published.x >= x - 8 && published.x <= x + 8);
    puts("next-lowest ID takes over where the ball was: PASS");
}

static void test_shorter_wall_keeps_the_ball_on_it(void) {
    peer_count = 0;
    run(33);
    assert(published.x <= W - BALL_RADIUS);
    puts("a shorter wall keeps the ball on it: PASS");
}

static void test_a_button_reverses_the_ball(void) {
    int16_t before, moved_before, moved_after;
    run(1000);
    before = published.x;
    run(66);
    moved_before = (int16_t)(published.x - before);
    local_input = PRG32_BTN_A;
    run(33);
    local_input = 0;
    before = published.x;
    run(66);
    moved_after = (int16_t)(published.x - before);
    /* Unless a wall end reversed it at the same moment, the sign flips. */
    assert((moved_before > 0) != (moved_after > 0));
    puts("the A button sends the ball back: PASS");
}

static void test_drawing_clips_at_both_screen_edges(void) {
    /* The fake gfx functions assert the bounds. Walk the ball across the seam
     * as the second of two screens. */
    local_id = 900;
    peer_count = 0;
    add_peer(100, FLAG_READY | FLAG_AUTHORITY);
    for (int x = W - 60; x < W + 60; x += 7) {
        peers[0].x = (int16_t)x;
        peers[0].y = 100 * 16;
        peers[0].sprite = 0;
        peers[0].frame++;
        run(33);
    }
    assert(ball_rects > 0);
    puts("ball straddling a screen edge stays clipped: PASS");
}

int main(void) {
    test_waits_for_welcome_before_leading();
    test_alone_leads_after_settling();
    test_ball_stays_on_one_screen_alone();
    test_wall_grows_with_a_peer();
    test_unready_lower_id_does_not_take_over();
    test_lower_ready_id_takes_the_ball_and_is_followed();
    test_next_lowest_takes_over_where_the_ball_was();
    test_shorter_wall_keeps_the_ball_on_it();
    test_a_button_reverses_the_ball();
    test_drawing_clips_at_both_screen_edges();
    return 0;
}
