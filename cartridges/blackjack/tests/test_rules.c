#include <assert.h>
#include <stdio.h>
#include "../blackjack_rules.h"

static uint8_t card(int suit,int rank){ return (uint8_t)(suit*13+rank); }
int main(void){
    bj_hand_t h,d; bj_player_t p; bj_rules_t r={6,0,1,1,3,2}; int soft=0;
    bj_hand_clear(&h); h.cards[0]=card(0,0); h.cards[1]=card(1,12); h.count=2;
    assert(bj_hand_value(&h,&soft)==21 && soft==1 && bj_is_blackjack(&h));
    h.cards[2]=card(2,0); h.count=3; assert(bj_hand_value(&h,&soft)==12 && soft==0);
    bj_hand_clear(&h); h.cards[0]=card(0,7); h.cards[1]=card(1,7); h.count=2; h.bet=25;
    p.bankroll=100; p.hand_count=1; assert(bj_can_split(&p,&h)); assert(bj_can_double(&p,&h,&r,0));
    bj_hand_clear(&d); d.cards[0]=card(0,0); d.cards[1]=card(1,5); d.count=2; assert(!bj_dealer_should_hit(&d,&r));
    d.cards[1]=card(1,4); d.count=2; assert(bj_dealer_should_hit(&d,&r));
    bj_hand_clear(&d); d.cards[0]=card(0,0); d.cards[1]=card(1,5); d.cards[2]=card(2,12); d.count=3; assert(!bj_dealer_should_hit(&d,&r));
    bj_hand_clear(&d); d.cards[0]=card(0,0); d.cards[1]=card(1,5); d.count=2; r.dealer_hits_soft17=1; assert(bj_dealer_should_hit(&d,&r));
    h.from_split=1; h.cards[0]=card(0,0); h.cards[1]=card(1,12); h.count=2; assert(!bj_is_blackjack(&h));
    p.hand_count=3; p.bankroll=100; h.bet=25; h.cards[0]=card(0,7); h.cards[1]=card(1,7); assert(bj_can_split(&p,&h));
    p.hand_count=4; assert(!bj_can_split(&p,&h));
    puts("blackjack rules tests: ok");
    return 0;
}
