#ifndef BLACKJACK_RULES_H
#define BLACKJACK_RULES_H

#include <stdint.h>

#define BJ_MAX_CARDS 12
#define BJ_MAX_HANDS 4
#define BJ_MAX_PLAYERS 4
#define BJ_SHOE_MAX 312

typedef struct {
    uint8_t cards[BJ_MAX_CARDS];
    uint8_t count;
    uint8_t stood;
    uint8_t doubled;
    uint8_t surrendered;
    uint8_t split_aces;
    uint8_t from_split;
    uint16_t bet;
} bj_hand_t;

typedef struct {
    bj_hand_t hands[BJ_MAX_HANDS];
    uint8_t hand_count;
    uint8_t active_hand;
    uint16_t bankroll;
    uint16_t insurance;
} bj_player_t;

typedef struct {
    uint8_t decks;
    uint8_t dealer_hits_soft17;
    uint8_t double_after_split;
    uint8_t late_surrender;
    uint8_t blackjack_payout_num;
    uint8_t blackjack_payout_den;
} bj_rules_t;

int bj_card_rank(uint8_t card);
int bj_card_suit(uint8_t card);
int bj_hand_value(const bj_hand_t *hand, int *soft);
int bj_is_blackjack(const bj_hand_t *hand);
int bj_is_bust(const bj_hand_t *hand);
int bj_can_split(const bj_player_t *p, const bj_hand_t *hand);
int bj_can_double(const bj_player_t *p, const bj_hand_t *hand, const bj_rules_t *rules, int is_split);
uint32_t bj_xorshift32(uint32_t *state);
void bj_build_shoe(uint8_t *shoe, int *shoe_count, int decks);
void bj_shuffle_shoe(uint8_t *shoe, int shoe_count, uint32_t *rng);
uint8_t bj_draw(uint8_t *shoe, int shoe_count, int *shoe_pos);
void bj_hand_clear(bj_hand_t *hand);
void bj_player_clear_round(bj_player_t *p);
int bj_dealer_should_hit(const bj_hand_t *dealer, const bj_rules_t *rules);
int bj_compare(const bj_hand_t *player, const bj_hand_t *dealer);

#endif
