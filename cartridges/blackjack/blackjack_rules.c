#include "blackjack_rules.h"

int bj_card_rank(uint8_t card) { return (card % 52u) % 13u; }
int bj_card_suit(uint8_t card) { return ((card % 52u) / 13u) & 3u; }

int bj_hand_value(const bj_hand_t *hand, int *soft) {
    int total = 0;
    int aces = 0;
    int i;
    for (i = 0; i < hand->count; ++i) {
        int r = bj_card_rank(hand->cards[i]);
        if (r == 0) { total += 11; aces++; }
        else if (r >= 9) total += 10;
        else total += r + 1;
    }
    while (total > 21 && aces > 0) { total -= 10; aces--; }
    if (soft) *soft = aces > 0;
    return total;
}

int bj_is_blackjack(const bj_hand_t *hand) {
    return hand->count == 2 && !hand->from_split && bj_hand_value(hand, 0) == 21;
}
int bj_is_bust(const bj_hand_t *hand) { return bj_hand_value(hand, 0) > 21; }

int bj_can_split(const bj_player_t *p, const bj_hand_t *hand) {
    if (p->hand_count >= BJ_MAX_HANDS || hand->count != 2 || p->bankroll < hand->bet) return 0;
    return bj_card_rank(hand->cards[0]) == bj_card_rank(hand->cards[1]);
}

int bj_can_double(const bj_player_t *p, const bj_hand_t *hand, const bj_rules_t *rules, int is_split) {
    if (hand->count != 2 || p->bankroll < hand->bet || hand->split_aces) return 0;
    if (is_split && !rules->double_after_split) return 0;
    return 1;
}

uint32_t bj_xorshift32(uint32_t *state) {
    uint32_t x = *state;
    if (!x) x = 0x6d2b79f5u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

void bj_build_shoe(uint8_t *shoe, int *shoe_count, int decks) {
    int d, c, n = 0;
    if (decks < 1) decks = 1;
    if (decks > 6) decks = 6;
    for (d = 0; d < decks; ++d) for (c = 0; c < 52; ++c) shoe[n++] = (uint8_t)c;
    *shoe_count = n;
}

void bj_shuffle_shoe(uint8_t *shoe, int shoe_count, uint32_t *rng) {
    int i;
    for (i = shoe_count - 1; i > 0; --i) {
        int j = (int)(bj_xorshift32(rng) % (uint32_t)(i + 1));
        uint8_t t = shoe[i]; shoe[i] = shoe[j]; shoe[j] = t;
    }
}

uint8_t bj_draw(uint8_t *shoe, int shoe_count, int *shoe_pos) {
    if (*shoe_pos >= shoe_count) *shoe_pos = 0;
    return shoe[(*shoe_pos)++];
}

void bj_hand_clear(bj_hand_t *hand) {
    int i;
    for (i = 0; i < BJ_MAX_CARDS; ++i) hand->cards[i] = 0;
    hand->count = 0; hand->stood = 0; hand->doubled = 0; hand->surrendered = 0; hand->split_aces = 0; hand->from_split = 0; hand->bet = 0;
}

void bj_player_clear_round(bj_player_t *p) {
    int i;
    for (i = 0; i < BJ_MAX_HANDS; ++i) bj_hand_clear(&p->hands[i]);
    p->hand_count = 1; p->active_hand = 0; p->insurance = 0;
}

int bj_dealer_should_hit(const bj_hand_t *dealer, const bj_rules_t *rules) {
    int soft = 0;
    int value = bj_hand_value(dealer, &soft);
    if (value < 17) return 1;
    if (value == 17 && soft && rules->dealer_hits_soft17) return 1;
    return 0;
}

int bj_compare(const bj_hand_t *player, const bj_hand_t *dealer) {
    int pv = bj_hand_value(player, 0), dv = bj_hand_value(dealer, 0);
    if (pv > 21) return -1;
    if (dv > 21) return 1;
    if (pv > dv) return 1;
    if (pv < dv) return -1;
    return 0;
}
