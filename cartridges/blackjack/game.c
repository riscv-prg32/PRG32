#include "prg32.h"
#include "blackjack_rules.h"
#include "assets.h"
/* The cartridge builder accepts one C translation unit; include the small rules core. */
#include "blackjack_rules.c"

#define C_FELT      0x0208
#define C_FELT2     0x036A
#define C_GOLD      0xFD20
#define C_GOLD2     0xFEA0
#define C_CREAM     0xFFDE
#define C_SHADOW    0x10A2
#define C_CARD      0xFFFF
#define C_RED       0xE8E4
#define C_BLACK     0x0841
#define C_BLUE      0x241F
#define C_GREEN     0x07E0
#define C_GRAY      0x7BEF
#define C_DARK      0x0008

#define MIN_BET 10
#define MAX_BET 500

typedef enum { MODE_SOLO=0, MODE_HOTSEAT=1, MODE_NETWORK=2 } bj_mode_t;
typedef enum {
    ST_TITLE=0, ST_BET, ST_INSURANCE, ST_PLAYER, ST_DEALER,
    ST_SETTLE, ST_ROUND_END, ST_RULES
} bj_state_t;

typedef enum { RES_NONE=0, RES_WIN, RES_LOSE, RES_PUSH, RES_BLACKJACK, RES_SURRENDER } bj_result_t;

static bj_rules_t rules = {6,0,1,1,3,2};
static bj_mode_t mode;
static bj_state_t state;
static bj_player_t players[BJ_MAX_PLAYERS];
static bj_hand_t dealer;
static uint8_t shoe[BJ_SHOE_MAX];
static int shoe_count, shoe_pos;
static int player_count, current_player, current_hand, insurance_player;
static int bet_cursor[BJ_MAX_PLAYERS];
static bj_result_t results[BJ_MAX_PLAYERS][BJ_MAX_HANDS];
static uint32_t rng_state, last_input, frame_no, dealer_delay;
static uint32_t best_bankroll;
static uint8_t menu_row;
static uint8_t net_joined;
static uint8_t message_timer;
static char message[25];

static void str_copy(char *dst, const char *src, int cap) {
    int i=0; if (cap<=0) return;
    while (src[i] && i<cap-1) { dst[i]=src[i]; i++; }
    dst[i]=0;
}
static void u32_to_str(uint32_t v, char *out) {
    char tmp[11]; int n=0,i;
    if (!v) { out[0]='0'; out[1]=0; return; }
    while (v && n<10) { tmp[n++]=(char)('0'+(v%10)); v/=10; }
    for (i=0;i<n;i++) out[i]=tmp[n-1-i]; out[n]=0;
}
static void money_text(uint32_t v, char *out) {
    char n[12]; int i=0,j=0; u32_to_str(v,n); out[j++]='$'; while(n[i] && j<15) out[j++]=n[i++]; out[j]=0;
}
static void set_message(const char *s) { str_copy(message,s,sizeof(message)); message_timer=80; }

static void sfx_chip(void) { prg32_audio_note_on_pan(4,4,72,185,-18); }
static void sfx_card(void) { prg32_audio_note_on_pan(5,5,58,130,22); }
static void sfx_win(void)  { prg32_audio_note_on_pan(4,6,76,230,0); }
static void sfx_lose(void) { prg32_audio_note_on_pan(4,7,42,170,0); }
static void sfx_blackjack(void) { prg32_audio_note_on_pan(4,6,84,255,0); prg32_audio_note_on_pan(5,2,88,210,24); }
static void sfx_release(void) { prg32_audio_note_off(4); prg32_audio_note_off(5); }

static void reset_bankrolls(void) {
    int p; for (p=0;p<BJ_MAX_PLAYERS;p++) { players[p].bankroll=1000; bj_player_clear_round(&players[p]); bet_cursor[p]=25; }
    best_bankroll=1000;
}
static void rebuild_shoe(void) {
    bj_build_shoe(shoe,&shoe_count,rules.decks); bj_shuffle_shoe(shoe,shoe_count,&rng_state); shoe_pos=0;
}
static uint8_t draw_card(void) {
    if (shoe_pos >= shoe_count-20) rebuild_shoe();
    sfx_card(); return bj_draw(shoe,shoe_count,&shoe_pos);
}
static void add_card(bj_hand_t *h, uint8_t c) { if (h->count<BJ_MAX_CARDS) h->cards[h->count++]=c; }
static int dealer_up_is_ace(void) { return dealer.count>1 && bj_card_rank(dealer.cards[1])==0; }
static int dealer_up_is_ten(void) { int r=dealer.count>1?bj_card_rank(dealer.cards[1]):-1; return r>=9; }

static void clear_results(void) {
    int p,h; for(p=0;p<BJ_MAX_PLAYERS;p++) for(h=0;h<BJ_MAX_HANDS;h++) results[p][h]=RES_NONE;
}
static void begin_betting(void) {
    int p;
    if (shoe_pos >= (shoe_count*3)/4) { rebuild_shoe(); set_message("SHUFFLING SHOE"); }
    bj_hand_clear(&dealer); clear_results();
    for(p=0;p<player_count;p++) {
        bj_player_clear_round(&players[p]);
        if (bet_cursor[p] < MIN_BET) bet_cursor[p]=MIN_BET;
        if (bet_cursor[p] > players[p].bankroll) bet_cursor[p]=players[p].bankroll;
    }
    current_player=0; current_hand=0; state=ST_BET;
}
static void deal_initial(void) {
    int p;
    for(p=0;p<player_count;p++) { bj_hand_t *h=&players[p].hands[0]; add_card(h,draw_card()); }
    add_card(&dealer,draw_card());
    for(p=0;p<player_count;p++) { bj_hand_t *h=&players[p].hands[0]; add_card(h,draw_card()); }
    add_card(&dealer,draw_card());
    insurance_player=0;
    if (dealer_up_is_ace()) state=ST_INSURANCE;
    else if ((dealer_up_is_ten() || dealer_up_is_ace()) && bj_is_blackjack(&dealer)) state=ST_SETTLE;
    else { current_player=0; current_hand=0; state=ST_PLAYER; }
}
static void lock_bet(void) {
    bj_player_t *p=&players[current_player]; int b=bet_cursor[current_player];
    if (b<MIN_BET || b>p->bankroll) return;
    p->bankroll-=b; p->hands[0].bet=(uint16_t)b; sfx_chip();
    current_player++;
    if (current_player>=player_count) { current_player=0; deal_initial(); }
}
static void advance_player_hand(void) {
    bj_player_t *p=&players[current_player];
    current_hand++;
    if (current_hand < p->hand_count) { p->active_hand=(uint8_t)current_hand; return; }
    current_player++; current_hand=0;
    if (current_player>=player_count) { state=ST_DEALER; dealer_delay=0; }
    else players[current_player].active_hand=0;
}
static void settle_round(void) {
    int p,h; int dealer_bj=bj_is_blackjack(&dealer);
    for(p=0;p<player_count;p++) {
        bj_player_t *pl=&players[p];
        if (pl->insurance && dealer_bj) pl->bankroll=(uint16_t)(pl->bankroll + pl->insurance*3u);
        for(h=0;h<pl->hand_count;h++) {
            bj_hand_t *ph=&pl->hands[h];
            if (ph->surrendered) { results[p][h]=RES_SURRENDER; continue; }
            if (dealer_bj) {
                if (bj_is_blackjack(ph)) { pl->bankroll+=ph->bet; results[p][h]=RES_PUSH; }
                else results[p][h]=RES_LOSE;
            } else if (bj_is_blackjack(ph)) {
                uint32_t profit=((uint32_t)ph->bet*rules.blackjack_payout_num)/rules.blackjack_payout_den;
                pl->bankroll=(uint16_t)(pl->bankroll + ph->bet + profit); results[p][h]=RES_BLACKJACK;
            } else {
                int cmp=bj_compare(ph,&dealer);
                if (cmp>0) { pl->bankroll=(uint16_t)(pl->bankroll+ph->bet*2u); results[p][h]=RES_WIN; }
                else if (cmp==0) { pl->bankroll+=ph->bet; results[p][h]=RES_PUSH; }
                else results[p][h]=RES_LOSE;
            }
        }
        if (pl->bankroll>best_bankroll) best_bankroll=pl->bankroll;
    }
    if (results[0][0]==RES_BLACKJACK) sfx_blackjack();
    else if (results[0][0]==RES_WIN) sfx_win(); else if (results[0][0]==RES_LOSE) sfx_lose();
    if (mode==MODE_SOLO && players[0].bankroll>=best_bankroll) prg32_score_submit_current_player("blackjack", players[0].bankroll);
    state=ST_ROUND_END;
}
static void handle_insurance(uint32_t pressed) {
    bj_player_t *p;
    if (insurance_player>=player_count) {
        if (bj_is_blackjack(&dealer)) state=ST_SETTLE; else { current_player=0; current_hand=0; state=ST_PLAYER; }
        return;
    }
    p=&players[insurance_player];
    if (pressed & PRG32_BTN_A) {
        uint16_t cost=(uint16_t)(p->hands[0].bet/2u);
        if (cost && p->bankroll>=cost) { p->bankroll-=cost; p->insurance=cost; sfx_chip(); }
        insurance_player++;
    } else if (pressed & (PRG32_BTN_B|PRG32_BTN_SELECT)) insurance_player++;
}
static void do_split(bj_player_t *p, bj_hand_t *h) {
    int new_index=p->hand_count; bj_hand_t *h2=&p->hands[new_index]; uint8_t moved=h->cards[1]; int ace=bj_card_rank(h->cards[0])==0;
    p->bankroll-=h->bet; bj_hand_clear(h2); h2->bet=h->bet; h->count=1; add_card(h2,moved); p->hand_count++;
    h->from_split=1; h2->from_split=1;
    if (ace) { h->split_aces=1; h2->split_aces=1; }
    add_card(h,draw_card()); add_card(h2,draw_card()); sfx_chip();
    if (ace) { h->stood=1; h2->stood=1; advance_player_hand(); }
}
static void handle_player(uint32_t pressed) {
    bj_player_t *p=&players[current_player]; bj_hand_t *h=&p->hands[current_hand]; int value=bj_hand_value(h,0);
    if (h->stood || h->surrendered || value>=21) { h->stood=1; advance_player_hand(); return; }
    if (pressed & PRG32_BTN_A) { add_card(h,draw_card()); if (bj_hand_value(h,0)>=21) { h->stood=1; advance_player_hand(); } return; }
    if (pressed & PRG32_BTN_B) { h->stood=1; advance_player_hand(); return; }
    if (pressed & PRG32_BTN_UP) {
        if (bj_can_double(p,h,&rules,p->hand_count>1)) { p->bankroll-=h->bet; h->bet*=2; h->doubled=1; add_card(h,draw_card()); h->stood=1; sfx_chip(); advance_player_hand(); }
        else set_message("DOUBLE NOT AVAILABLE");
        return;
    }
    if (pressed & PRG32_BTN_DOWN) {
        if (bj_can_split(p,h)) do_split(p,h); else set_message("SPLIT NOT AVAILABLE");
        return;
    }
    if (pressed & PRG32_BTN_LEFT) {
        if (rules.late_surrender && h->count==2 && p->hand_count==1) { h->surrendered=1; p->bankroll+=(uint16_t)(h->bet/2u); sfx_chip(); advance_player_hand(); }
        else set_message("SURRENDER UNAVAILABLE");
    }
}

static void publish_network(uint32_t input) {
    int total=0; uint16_t flags=0; uint16_t sprite=(uint16_t)state;
    if (!net_joined) return;
    prg32_multiplayer_tick();
    if (player_count>0) total=bj_hand_value(&players[0].hands[players[0].active_hand],0);
    flags=(uint16_t)((state&0x0f) | ((players[0].hands[0].count&0x0f)<<4));
    prg32_multiplayer_set_input(input);
    prg32_multiplayer_set_local_state((int16_t)players[0].bankroll,(int16_t)total,sprite,flags);
}

void blackjack_init(void) {
    int p;
    rng_state=prg32_ticks_ms() ^ 0xB1ACCA11u; mode=MODE_SOLO; state=ST_TITLE; player_count=1; current_player=0; current_hand=0;
    last_input=0; frame_no=0; dealer_delay=0; menu_row=0; net_joined=0; message[0]=0; message_timer=0;
    reset_bankrolls(); rebuild_shoe();
    prg32_band_set_game_info("BLACKJACK | A HIT  B STAND  UP DOUBLE  DOWN SPLIT");
    prg32_audio_play_track(0);
    for(p=0;p<BJ_MAX_PLAYERS;p++) bet_cursor[p]=25;
}

static void title_update(uint32_t pressed) {
    if (pressed & PRG32_BTN_UP) { mode=(bj_mode_t)((mode+2)%3); sfx_chip(); }
    if (pressed & PRG32_BTN_DOWN) { mode=(bj_mode_t)((mode+1)%3); sfx_chip(); }
    if (mode==MODE_HOTSEAT) {
        if ((pressed & PRG32_BTN_LEFT) && player_count>2) player_count--;
        if ((pressed & PRG32_BTN_RIGHT) && player_count<4) player_count++;
        if (player_count<2) player_count=2;
    } else player_count=1;
    if (pressed & PRG32_BTN_B) { state=ST_RULES; menu_row=0; }
    if (pressed & PRG32_BTN_A) {
        reset_bankrolls(); rebuild_shoe();
        if (mode==MODE_NETWORK) {
            prg32_multiplayer_init();
            if (prg32_multiplayer_available() && prg32_multiplayer_join("blackjack-v1",PRG32_MP_FLAG_ENABLE)==0) { net_joined=1; set_message("NETWORK TABLE JOINED"); }
            else set_message("NETWORK OFFLINE: SOLO PLAY");
        }
        begin_betting();
    }
}

static void rules_update(uint32_t pressed) {
    if (pressed & PRG32_BTN_UP) menu_row=(uint8_t)((menu_row+3)%4);
    if (pressed & PRG32_BTN_DOWN) menu_row=(uint8_t)((menu_row+1)%4);
    if (pressed & (PRG32_BTN_LEFT|PRG32_BTN_RIGHT|PRG32_BTN_A)) {
        if (menu_row==0) { rules.decks=rules.decks==6?1:6; rebuild_shoe(); }
        else if (menu_row==1) rules.dealer_hits_soft17=!rules.dealer_hits_soft17;
        else if (menu_row==2) rules.double_after_split=!rules.double_after_split;
        else if (menu_row==3) { if (rules.blackjack_payout_num==3) { rules.blackjack_payout_num=6; rules.blackjack_payout_den=5; } else { rules.blackjack_payout_num=3; rules.blackjack_payout_den=2; } }
        sfx_chip();
    }
    if (pressed & PRG32_BTN_B) state=ST_TITLE;
}

void blackjack_update(void) {
    uint32_t input=prg32_input_read(); uint32_t pressed=input & ~last_input;
    frame_no++;
    if ((frame_no&7u)==0) sfx_release();
    if (message_timer) message_timer--;
    if (state==ST_TITLE) title_update(pressed);
    else if (state==ST_RULES) rules_update(pressed);
    else if (state==ST_BET) {
        bj_player_t *p=&players[current_player]; int *b=&bet_cursor[current_player];
        if (pressed&PRG32_BTN_LEFT) *b-=5; if (pressed&PRG32_BTN_RIGHT) *b+=5;
        if (pressed&PRG32_BTN_DOWN) *b-=25; if (pressed&PRG32_BTN_UP) *b+=25;
        if (*b<MIN_BET) *b=MIN_BET; if (*b>MAX_BET) *b=MAX_BET; if (*b>p->bankroll) *b=p->bankroll;
        if (pressed&PRG32_BTN_A) lock_bet();
        if (pressed&PRG32_BTN_SELECT) { if (net_joined) prg32_multiplayer_leave(); state=ST_TITLE; }
    }
    else if (state==ST_INSURANCE) handle_insurance(pressed);
    else if (state==ST_PLAYER) handle_player(pressed);
    else if (state==ST_DEALER) {
        dealer_delay++;
        if (dealer_delay>18) { dealer_delay=0; if (bj_dealer_should_hit(&dealer,&rules)) add_card(&dealer,draw_card()); else state=ST_SETTLE; }
    }
    else if (state==ST_SETTLE) settle_round();
    else if (state==ST_ROUND_END) {
        if (pressed&PRG32_BTN_A) {
            int alive=0,p; for(p=0;p<player_count;p++) if(players[p].bankroll>=MIN_BET) alive++;
            if(alive) begin_betting(); else { reset_bankrolls(); begin_betting(); set_message("NEW BANKROLL: $1000"); }
        }
        if (pressed&PRG32_BTN_B) { if(net_joined){prg32_multiplayer_leave();net_joined=0;} state=ST_TITLE; }
        if (pressed&PRG32_BTN_SELECT && mode==MODE_SOLO) prg32_scoreboard_show("blackjack","BLACKJACK HIGH ROLLERS");
    }
    if (mode==MODE_NETWORK && state!=ST_TITLE && state!=ST_RULES) publish_network(input);
    last_input=input;
}

static void draw_chip(int x,int y,uint16_t c) {
    prg32_gfx_rect(x,y,10,10,C_SHADOW); prg32_gfx_rect(x+1,y+1,8,8,c); prg32_gfx_rect(x+3,y+3,4,4,C_CREAM);
}
static const uint8_t *suit_bits(int suit) {
    if(suit==0) return bj_suit_spade; if(suit==1) return bj_suit_heart; if(suit==2) return bj_suit_club; return bj_suit_diamond;
}
static uint16_t suit_color(int suit) { return (suit==1||suit==3)?C_RED:C_BLACK; }
static const char *rank_name(int rank) {
    static const char *names[13]={"A","2","3","4","5","6","7","8","9","10","J","Q","K"}; return names[rank];
}
static void draw_card_face(int x,int y,uint8_t card,int compact) {
    int r=bj_card_rank(card),s=bj_card_suit(card); uint16_t col=suit_color(s); int w=compact?28:34,h=compact?40:48;
    prg32_gfx_rect(x+2,y+2,w,h,C_SHADOW); prg32_gfx_rect(x,y,w,h,C_CARD);
    prg32_gfx_rect(x,y,w,2,C_GOLD2); prg32_gfx_rect(x,y,2,h,C_GOLD2);
    prg32_gfx_text8(x+3,y+3,rank_name(r),col,C_CARD);
    prg32_sprite_draw_8x8(x+(w-8)/2,y+(h-8)/2,suit_bits(s),col,C_CARD);
    if(!compact) prg32_sprite_draw_8x8(x+w-11,y+h-11,suit_bits(s),col,C_CARD);
}
static void draw_card_back(int x,int y) { prg32_gfx_rect(x+2,y+2,24,32,C_SHADOW); prg32_sprite_draw_indexed(x,y,&bj_cardback_sprite,0); }

static void draw_hand(const bj_hand_t *h,int x,int y,int hide_first,int active) {
    int i,step=h->count>6?18:24;
    if(active) prg32_gfx_rect(x-3,y-3,step*(h->count? h->count:1)+8,46,C_GOLD);
    for(i=0;i<h->count;i++) { if(hide_first&&i==0) draw_card_back(x+i*step,y); else draw_card_face(x+i*step,y,h->cards[i],1); }
}
static void draw_header(void) {
    prg32_gfx_rect(0,0,320,18,C_DARK); prg32_gfx_text8(8,5,"PRG32 BLACKJACK",C_GOLD,C_DARK);
    prg32_gfx_text8(232,5,mode==MODE_SOLO?"SOLO":mode==MODE_HOTSEAT?"HOT SEAT":"NETWORK",C_CREAM,C_DARK);
}
static void draw_table_arc(void) {
    prg32_gfx_rect(0,18,320,182,C_FELT);
    prg32_gfx_rect(0,18,320,3,C_GOLD);
    prg32_gfx_rect(12,66,296,2,C_FELT2); prg32_gfx_rect(28,101,264,2,C_FELT2);
    prg32_gfx_text8(113,87,"BLACKJACK PAYS 3 TO 2",C_GOLD,C_FELT);
}
static void draw_money_line(int p,int y,int highlight) {
    char a[16],b[16]; money_text(players[p].bankroll,a); u32_to_str((uint32_t)(p+1),b);
    prg32_gfx_text8(6,y,"P",highlight?C_GOLD:C_CREAM,C_FELT);
    prg32_gfx_text8(14,y,b,highlight?C_GOLD:C_CREAM,C_FELT); prg32_gfx_text8(28,y,a,C_CREAM,C_FELT);
}
static void draw_network_peers(void) {
    int n=prg32_multiplayer_get_peer_count(),i; prg32_player_state_t peer; char t[12];
    if(!net_joined) return;
    prg32_gfx_text8(214,22,"TABLE",C_GOLD,C_FELT);
    for(i=0;i<n && i<4;i++) if(prg32_multiplayer_get_peer(i,&peer)==0) {
        u32_to_str(peer.player_id%1000u,t); prg32_gfx_text8(214,32+i*9,t,C_CREAM,C_FELT);
        money_text((uint16_t)peer.x,t); prg32_gfx_text8(246,32+i*9,t,C_GREEN,C_FELT);
    }
}

static void draw_title(void) {
    int y;
    prg32_gfx_clear(C_DARK);
    for(y=0;y<200;y+=16) prg32_gfx_rect(0,y,320,1,(y&32)?C_FELT2:C_FELT);
    prg32_gfx_rect(45,20,230,160,C_FELT); prg32_gfx_rect(49,24,222,152,C_DARK); prg32_gfx_rect(53,28,214,144,C_FELT);
    draw_card_face(76,46,0,0); draw_card_face(210,46,12,0);
    prg32_gfx_text8(108,48,"BLACKJACK",C_GOLD,C_FELT);
    prg32_gfx_text8(93,65,"CASINO EDITION",C_CREAM,C_FELT);
    prg32_gfx_text8(78,103,mode==MODE_SOLO?"> SOLO <":mode==MODE_HOTSEAT?"> HOT-SEAT 2-4P <":"> NETWORK TABLE <",C_GOLD,C_FELT);
    if(mode==MODE_HOTSEAT) { char n[4]; u32_to_str(player_count,n); prg32_gfx_text8(112,118,"PLAYERS:",C_CREAM,C_FELT); prg32_gfx_text8(184,118,n,C_GOLD,C_FELT); }
    prg32_gfx_text8(81,139,"A PLAY   B RULES",C_CREAM,C_FELT); prg32_gfx_text8(81,153,"UP/DOWN MODE",C_GRAY,C_FELT);
    draw_chip(60,136,C_RED); draw_chip(250,136,C_BLUE);
}
static void draw_rules(void) {
    char n[8]; prg32_gfx_clear(C_DARK); prg32_gfx_rect(24,18,272,164,C_FELT); prg32_gfx_text8(104,26,"TABLE RULES",C_GOLD,C_FELT);
    prg32_gfx_text8(46,52,menu_row==0?"> DECKS":"  DECKS",menu_row==0?C_GOLD:C_CREAM,C_FELT); u32_to_str(rules.decks,n); prg32_gfx_text8(208,52,n,C_CREAM,C_FELT);
    prg32_gfx_text8(46,72,menu_row==1?"> DEALER SOFT 17":"  DEALER SOFT 17",menu_row==1?C_GOLD:C_CREAM,C_FELT); prg32_gfx_text8(224,72,rules.dealer_hits_soft17?"HIT":"STAND",C_CREAM,C_FELT);
    prg32_gfx_text8(46,92,menu_row==2?"> DOUBLE AFTER SPLIT":"  DOUBLE AFTER SPLIT",menu_row==2?C_GOLD:C_CREAM,C_FELT); prg32_gfx_text8(248,92,rules.double_after_split?"YES":"NO",C_CREAM,C_FELT);
    prg32_gfx_text8(46,112,menu_row==3?"> BLACKJACK PAYOUT":"  BLACKJACK PAYOUT",menu_row==3?C_GOLD:C_CREAM,C_FELT); prg32_gfx_text8(232,112,rules.blackjack_payout_num==3?"3:2":"6:5",C_CREAM,C_FELT);
    prg32_gfx_text8(46,140,"LATE SURRENDER: YES",C_GRAY,C_FELT); prg32_gfx_text8(46,154,"B BACK",C_CREAM,C_FELT);
}
static void draw_bet(void) {
    char m[16],pnum[4]; draw_table_arc(); draw_header(); draw_money_line(current_player,27,1);
    u32_to_str((uint32_t)(current_player+1),pnum); money_text((uint32_t)bet_cursor[current_player],m);
    prg32_gfx_text8(93,70,"PLACE YOUR BET",C_GOLD,C_FELT); prg32_gfx_text8(118,91,"PLAYER",C_CREAM,C_FELT); prg32_gfx_text8(178,91,pnum,C_GOLD,C_FELT);
    draw_chip(122,111,C_RED); draw_chip(137,111,C_BLUE); draw_chip(152,111,C_GOLD); prg32_gfx_text8(177,113,m,C_CREAM,C_FELT);
    prg32_gfx_text8(55,146,"LEFT/RIGHT 5   UP/DOWN 25",C_GRAY,C_FELT); prg32_gfx_text8(100,162,"A DEAL",C_CREAM,C_FELT);
}
static void draw_play(void) {
    char val[8],cash[16],bet[16]; bj_hand_t *h;
    draw_table_arc(); draw_header();
    draw_hand(&dealer,76,25,state==ST_PLAYER||state==ST_INSURANCE,0);
    if(state==ST_PLAYER||state==ST_INSURANCE) str_copy(val,"?",sizeof(val)); else u32_to_str(bj_hand_value(&dealer,0),val);
    prg32_gfx_text8(12,32,"DEALER",C_CREAM,C_FELT); prg32_gfx_text8(12,44,val,C_GOLD,C_FELT);
    if(state==ST_INSURANCE) {
        char pn[4]; u32_to_str((uint32_t)(insurance_player+1),pn); prg32_gfx_rect(56,105,208,42,C_DARK); prg32_gfx_text8(77,111,"DEALER SHOWS ACE",C_GOLD,C_DARK); prg32_gfx_text8(75,126,"P",C_CREAM,C_DARK); prg32_gfx_text8(83,126,pn,C_CREAM,C_DARK); prg32_gfx_text8(99,126,"A INSURE  B NO",C_CREAM,C_DARK); return;
    }
    { int dp=current_player; if(dp>=player_count) dp=player_count-1;
    h=&players[dp].hands[current_hand<players[dp].hand_count?current_hand:0];
    draw_hand(h,76,116,0,state==ST_PLAYER);
    u32_to_str(bj_hand_value(h,0),val); money_text(players[dp].bankroll,cash); money_text(h->bet,bet);
    prg32_gfx_text8(8,115,"P",C_CREAM,C_FELT); {char pn[4];u32_to_str((uint32_t)(dp+1),pn);prg32_gfx_text8(16,115,pn,C_GOLD,C_FELT);} prg32_gfx_text8(8,128,val,C_GOLD,C_FELT);
    prg32_gfx_text8(8,143,cash,C_CREAM,C_FELT); prg32_gfx_text8(8,156,"BET",C_GRAY,C_FELT); prg32_gfx_text8(32,156,bet,C_CREAM,C_FELT);
    if(state==ST_PLAYER) {
        prg32_gfx_text8(67,174,"A HIT B STAND UP DOUBLE DOWN SPLIT",C_CREAM,C_FELT);
        prg32_gfx_text8(112,187,"LEFT SURRENDER",C_GRAY,C_FELT);
    } else if(state==ST_DEALER) prg32_gfx_text8(116,174,"DEALER PLAY",C_GOLD,C_FELT);
    draw_network_peers(); }
}
static const char *result_name(bj_result_t r) {
    if(r==RES_WIN) return "WIN"; if(r==RES_LOSE) return "LOSE"; if(r==RES_PUSH) return "PUSH"; if(r==RES_BLACKJACK) return "BJ"; if(r==RES_SURRENDER) return "SURRENDER"; return "";
}
static uint16_t result_color(bj_result_t r) { return (r==RES_WIN||r==RES_BLACKJACK)?C_GOLD:(r==RES_LOSE?C_RED:C_CREAM); }
static void draw_round_end(void) {
    int p,h,y=91; char cash[16],pn[4]; draw_table_arc(); draw_header(); draw_hand(&dealer,100,26,0,0);
    prg32_gfx_text8(8,28,"DEALER",C_CREAM,C_FELT); {char v[8];u32_to_str(bj_hand_value(&dealer,0),v);prg32_gfx_text8(8,41,v,C_GOLD,C_FELT);}
    for(p=0;p<player_count;p++) {
        u32_to_str((uint32_t)(p+1),pn); money_text(players[p].bankroll,cash); prg32_gfx_text8(20,y,"P",C_CREAM,C_FELT); prg32_gfx_text8(28,y,pn,C_GOLD,C_FELT); prg32_gfx_text8(48,y,cash,C_CREAM,C_FELT);
        for(h=0;h<players[p].hand_count;h++) prg32_gfx_text8(145+h*43,y,result_name(results[p][h]),result_color(results[p][h]),C_FELT);
        y+=16;
    }
    prg32_gfx_text8(68,169,"A NEXT ROUND   B MENU",C_CREAM,C_FELT); if(mode==MODE_SOLO) prg32_gfx_text8(84,184,"SELECT HIGH SCORES",C_GRAY,C_FELT); draw_network_peers();
}
static void draw_message(void) { if(message_timer && message[0]) { prg32_gfx_rect(58,78,204,24,C_DARK); prg32_gfx_rect(60,80,200,20,C_GOLD); prg32_gfx_rect(62,82,196,16,C_DARK); prg32_gfx_text8(68,86,message,C_CREAM,C_DARK); } }

void blackjack_draw(void) {
    if(state==ST_TITLE) draw_title();
    else if(state==ST_RULES) draw_rules();
    else if(state==ST_BET) draw_bet();
    else if(state==ST_ROUND_END) draw_round_end();
    else draw_play();
    draw_message();
}
