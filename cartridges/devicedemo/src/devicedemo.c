#include "prg32.h"
#include <stdint.h>
#include <stddef.h>

#define C_NAVY 0x0842u
#define C_DARK 0x1084u
#define C_PANEL 0x18c6u
#define C_TEAL 0x0430u
#define C_ORANGE 0xfd20u
#define C_GRAY 0x8410u
#define C_LIME 0x87e0u
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))

typedef void (*page_draw_fn)(uint32_t now, uint32_t input);
static uint8_t g_page;
static uint32_t g_prev_input;
static uint32_t g_pressed;
static uint32_t g_frame;
static prg32_platform_actor_t g_actor;

static const uint16_t palette16[16] = {
  0x0000,0xffff,0xf800,0x07e0,0x001f,0xffe0,0x07ff,0xf81f,
  0x8410,0xfd20,0x87e0,0x18c6,0x39e7,0x4208,0x2104,0xad55
};

/* 16x16, 4 bpp: two pixels per byte. */
static const uint8_t indexed_pixels[128] = {
  0x00,0x00,0x11,0x11,0x11,0x11,0x00,0x00, 0x00,0x12,0x22,0x22,0x22,0x21,0x10,0x00,
  0x01,0x22,0x99,0x99,0x99,0x99,0x22,0x10, 0x12,0x29,0x11,0x11,0x11,0x19,0x22,0x11,
  0x12,0x91,0xaa,0xaa,0xaa,0xa1,0x92,0x11, 0x12,0x91,0xa3,0x33,0x33,0xa1,0x92,0x11,
  0x12,0x91,0xa3,0x66,0x63,0xa1,0x92,0x11, 0x12,0x91,0xa3,0x6f,0x63,0xa1,0x92,0x11,
  0x12,0x91,0xa3,0x66,0x63,0xa1,0x92,0x11, 0x12,0x91,0xa3,0x33,0x33,0xa1,0x92,0x11,
  0x12,0x91,0xaa,0xaa,0xaa,0xa1,0x92,0x11, 0x12,0x29,0x11,0x11,0x11,0x19,0x22,0x11,
  0x01,0x22,0x99,0x99,0x99,0x99,0x22,0x10, 0x00,0x12,0x22,0x22,0x22,0x21,0x10,0x00,
  0x00,0x00,0x11,0x11,0x11,0x11,0x00,0x00, 0x00,0x00,0x00,0x11,0x11,0x00,0x00,0x00
};
static const prg32_indexed_sprite_t indexed_sprite = {
  indexed_pixels, palette16, 16,16,1,16, PRG32_SPRITE_BPP_4, 0
};

/* Four 8x8 1-bpp planes stored sequentially; the first two are used for 2-bpp demo. */
static const uint8_t bitplane_pixels[32] = {
  0x18,0x3c,0x7e,0xdb,0xff,0x24,0x5a,0xa5,
  0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81,
  0xff,0x81,0xbd,0xa5,0xa5,0xbd,0x81,0xff,
  0x00,0x7e,0x42,0x5a,0x5a,0x42,0x7e,0x00
};
static const uint16_t bitplane_palette[4] = {0x0000,0x07ff,0xffe0,0xffff};
static const prg32_indexed_sprite_t bitplane_sprite = {
  bitplane_pixels, bitplane_palette, 8,8,1,4, PRG32_SPRITE_BPP_2, 0
};

static const uint8_t tile_checker[8] = {0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
static const uint8_t tile_star[8] = {0x18,0x18,0x7e,0x3c,0x7e,0x18,0x24,0x42};
static const uint8_t tile_ground[8] = {0xff,0x81,0xbd,0xa5,0xbd,0x81,0xff,0xff};

static void text(int x,int y,const char *s,uint16_t fg){ prg32_gfx_text8(x,y,s,fg,C_NAVY); }
static void header(const char *title,const char *sub){
  prg32_gfx_clear(C_NAVY); prg32_gfx_rect(0,0,320,24,C_TEAL);
  prg32_gfx_text8(8,5,title,PRG32_COLOR_WHITE,C_TEAL);
  prg32_gfx_text8(8,30,sub,PRG32_COLOR_CYAN,C_NAVY);
  prg32_gfx_text8(8,184,"LEFT/RIGHT page  A action  B/SELECT next",C_GRAY,C_NAVY);
}
static void value_bar(int x,int y,int w,int v,int max,uint16_t color){
  prg32_gfx_rect(x,y,w,8,C_DARK); if(max>0){int fill=(v*w)/max; if(fill<0)fill=0;if(fill>w)fill=w;prg32_gfx_rect(x,y,fill,8,color);} }

static void page_overview(uint32_t now,uint32_t input){
  (void)now;(void)input; header("PRG32 DEVICE DEMO +","development-c6 feature tour");
  text(8,50,"Native RV32IMAC cartridge on ESP32-C6",PRG32_COLOR_WHITE);
  text(8,64,"320x200 game viewport / RGB565 framebuffer",PRG32_COLOR_WHITE);
  text(8,78,"Indexed + bitplane sprites / tiles / playfields",C_LIME);
  text(8,92,"SID-like procedural synth + tracker events",C_LIME);
  text(8,106,"2-player input / WiFi / Store / scores / metrics",C_LIME);
  text(8,130,"This cartridge is both showcase and smoke test.",PRG32_COLOR_YELLOW);
  prg32_sprite_draw_indexed(248,55,&indexed_sprite,0);
  prg32_debug_overlay_draw(1,224,132,prg32_input_read(),g_frame);
}
static void page_input(uint32_t now,uint32_t input){
  (void)now; header("INPUT + CONTROLLERS","live masks for local players and external controller");
  uint32_t p2=prg32_input_read_player(1), ctl=prg32_controller_read();
  char buf[40];
  prg32_gfx_text8(8,50,"P1",PRG32_COLOR_WHITE,C_NAVY); value_bar(40,50,190,(int)(input&0x7f),127,C_LIME);
  prg32_gfx_text8(8,68,"P2",PRG32_COLOR_WHITE,C_NAVY); value_bar(40,68,190,(int)(p2&0x7f),127,PRG32_COLOR_CYAN);
  prg32_gfx_text8(8,86,"CTL",PRG32_COLOR_WHITE,C_NAVY); value_bar(40,86,190,(int)(ctl&0xff),255,PRG32_COLOR_YELLOW);
  (void)buf;
  text(8,112,"Directions move the marker.",C_GRAY);
  int x=160,y=145; if(input&PRG32_BTN_LEFT)x-=30;if(input&PRG32_BTN_RIGHT)x+=30;if(input&PRG32_BTN_UP)y-=20;if(input&PRG32_BTN_DOWN)y+=20;
  prg32_gfx_rect(x-7,y-7,15,15,PRG32_COLOR_MAGENTA);
}
static void page_primitives(uint32_t now,uint32_t input){
  (void)input; header("FRAMEBUFFER PRIMITIVES","pixels, rects, text, animation and clipping");
  for(int i=0;i<12;i++) prg32_gfx_rect(8+i*25,50,20,18,palette16[(i+2)&15]);
  for(int i=0;i<160;i+=4) prg32_gfx_pixel(80+i/2,95+(int)((now/40+i)%24),palette16[(i/4)&15]);
  text(8,132,"Direct RGB565 drawing remains the universal path.",PRG32_COLOR_WHITE);
  prg32_sprite_draw_indexed(276,120,&indexed_sprite,0);
}
static void page_indexed(uint32_t now,uint32_t input){
  (void)input; header("INDEXED SPRITES","new 1/2/4/8-bpp palette graphics");
  text(8,48,"4 bpp is the compact default for colorful retro art.",PRG32_COLOR_WHITE);
  for(int i=0;i<8;i++) prg32_sprite_draw_indexed(16+i*36,80+((i&1)?18:0),&indexed_sprite,0);
  int bounce=(int)((now/12)%220); if(bounce>110) bounce=220-bounce;
  prg32_sprite_draw_indexed(100+bounce,132,&indexed_sprite,0);
  text(8,162,"Palette assets expand directly into RGB565 at draw time.",C_LIME);
}
static void page_bitplanes(uint32_t now,uint32_t input){
  (void)now;(void)input; header("BITPLANE SPRITES","compact planar art through the same palette descriptor");
  text(8,48,"Ideal for old-school masked/plane-oriented content.",PRG32_COLOR_WHITE);
  for(int y=0;y<5;y++) for(int x=0;x<12;x++) prg32_sprite_draw_bitplanes(12+x*24,74+y*18,&bitplane_sprite,0);
  text(8,166,"This page exercises prg32_sprite_draw_bitplanes().",C_LIME);
}
static void page_tiles(uint32_t now,uint32_t input){
  (void)now;(void)input; header("TILES + PLAYFIELDS","8x8 tiles, dual layers, camera and parallax");
  prg32_tile_define(1,tile_checker,0x39e7,C_DARK); prg32_tile_define(2,tile_star,PRG32_COLOR_YELLOW,C_TEAL);
  prg32_playfield_clear(0,1); prg32_playfield_clear(1,0);
  for(int x=0;x<40;x+=3) prg32_playfield_put(1,(uint8_t)x,(uint8_t)(4+(x%7)),2);
  prg32_playfield_camera((int)((g_frame/2)%192),0); prg32_playfield_parallax(1,128,256); prg32_playfield_draw_dual();
  prg32_gfx_rect(0,0,320,40,C_NAVY); prg32_gfx_text8(8,6,"PLAYFIELD ACTIVE",PRG32_COLOR_WHITE,C_NAVY);
}
static void page_platform(uint32_t now,uint32_t input){
  (void)now; header("PLATFORM HELPER","tile collision, gravity, jumping and camera-follow API");
  prg32_tile_define(3,tile_ground,PRG32_COLOR_GREEN,C_DARK); prg32_platform_tile_flags(3,PRG32_TILE_FLAG_SOLID);
  prg32_playfield_clear(0,0); for(int x=0;x<40;x++) prg32_playfield_put(0,(uint8_t)x,20,3);
  for(int x=5;x<14;x++) prg32_playfield_put(0,(uint8_t)x,15,3);
  prg32_platform_actor_step(&g_actor,input,2,7,1,5); prg32_platform_camera_follow(&g_actor,80,40);
  prg32_playfield_draw(0,0); prg32_gfx_rect(g_actor.x-prg32_playfield_camera_x(),g_actor.y-prg32_playfield_camera_y(),g_actor.w,g_actor.h,C_ORANGE);
  prg32_gfx_rect(0,0,320,38,C_NAVY); prg32_gfx_text8(8,6,"MOVE + A JUMP",PRG32_COLOR_WHITE,C_NAVY);
}
static void page_synth(uint32_t now,uint32_t input){
  (void)now; header("SID-LIKE SYNTH","triangle / saw / pulse / noise + ADSR + filter controls");
  text(8,50,"AUD0 instruments and tracker loaded with cartridge",C_LIME);
  text(8,70,"A: play arpeggio track    UP: triangle C",PRG32_COLOR_WHITE);
  text(8,84,"DOWN: saw E              LEFT: pulse G",PRG32_COLOR_WHITE);
  text(8,98,"RIGHT: noise hit          B: stop track",PRG32_COLOR_WHITE);
  if(g_pressed&PRG32_BTN_UP) prg32_audio_note_on(0,0,60,220);
  if(g_pressed&PRG32_BTN_DOWN) prg32_audio_note_on(1,1,64,210);
  if(g_pressed&PRG32_BTN_LEFT) prg32_audio_note_on_pan(2,2,67,220,PRG32_AUDIO_PAN_LEFT);
  if(g_pressed&PRG32_BTN_RIGHT) prg32_audio_note_on_pan(3,3,36,170,PRG32_AUDIO_PAN_RIGHT);
  if(g_pressed&PRG32_BTN_A) prg32_audio_play_track(0);
  text(8,132,"Procedural instruments use the portable audio ABI; no PCM.",C_LIME);
}
static void page_audio_legacy(uint32_t now,uint32_t input){
  (void)now; header("AUDIO COMPATIBILITY","legacy beep/tone/note APIs coexist with the mixer");
  text(8,50,"A: 440 Hz beep    UP: duty-cycle tone",PRG32_COLOR_WHITE);
  text(8,68,"DOWN: MIDI note   RIGHT: short note sequence",PRG32_COLOR_WHITE);
  if(g_pressed&PRG32_BTN_A) prg32_audio_beep(440,120);
  if(g_pressed&PRG32_BTN_UP) prg32_audio_tone(660,150,16000);
  if(g_pressed&PRG32_BTN_DOWN) prg32_audio_note(72,180);
  if(g_pressed&PRG32_BTN_RIGHT){ static const prg32_note_t n[]={{523,70},{659,70},{784,100}};prg32_audio_play_notes(n,3); }
  text(8,110,"Sound pages also drive the optional RGB LED VU path.",C_LIME);
  value_bar(8,140,280,(int)((g_frame*7)%256),255,C_ORANGE);
}
static void page_system(uint32_t now,uint32_t input){
  (void)now;(void)input; header("SYSTEM / MEMORY","runtime diagnostics available to cartridges");
  text(8,50,"Portable cartridges expose frame/input diagnostics",PRG32_COLOR_WHITE);
  text(8,68,"without firmware-private memory structures.",PRG32_COLOR_WHITE);
  value_bar(8,96,280,(int)(g_frame%240),240,C_LIME);
  text(8,116,"Frame counter + live input feed the debug overlay.",C_GRAY);
  prg32_debug_overlay_draw(1,224,132,prg32_input_read(),g_frame);
}
static void page_services(uint32_t now,uint32_t input){
  (void)now;(void)input; header("WIFI + SCORES","portable service APIs without network changes");
  const char *ip=prg32_wifi_current_ip(), *ssid=prg32_wifi_current_ssid();
  text(8,50,"WiFi status",PRG32_COLOR_WHITE); text(120,50,ssid?ssid:"offline",C_LIME);
  text(8,66,"Current IP",PRG32_COLOR_WHITE); text(120,66,ip?ip:"n/a",PRG32_COLOR_CYAN);
  char player[32]; int rc=prg32_score_player_get(player,sizeof(player));
  text(8,82,"Score player",PRG32_COLOR_WHITE); text(120,82,rc==0?player:"not set",PRG32_COLOR_MAGENTA);
  text(8,126,"Demo reads state only; it does not reconfigure networking.",C_GRAY);
  text(8,144,"Firmware also provides multiplayer through prg32_multiplayer.h.",C_LIME);
}
static page_draw_fn pages[]={page_overview,page_input,page_primitives,page_indexed,page_bitplanes,page_tiles,page_platform,page_synth,page_audio_legacy,page_system,page_services};
static const char *page_names[]={"overview","input","primitives","indexed","bitplanes","playfield","platform","synth","audio","memory","services"};

void devicedemo_init(void){
  g_page=0;g_prev_input=0;g_pressed=0;g_frame=0;prg32_gfx_set_fullscreen(0);
  prg32_band_set_mode(PRG32_BAND_TOP,PRG32_BAND_MODE_GAME); prg32_band_set_mode(PRG32_BAND_BOTTOM,PRG32_BAND_MODE_FPS);
  prg32_band_set_game_info("DeviceDemo+ development-c6");
  prg32_platform_actor_init(&g_actor,0,24,120,12,14);
}
void devicedemo_update(void){
  uint32_t input=prg32_input_read(); uint32_t pressed=input&~g_prev_input;
  g_pressed=pressed;
  if(g_page==7 && (pressed&PRG32_BTN_B)) {
    prg32_audio_stop_track();
    pressed&=~PRG32_BTN_B;
    g_pressed=pressed;
  }
  if(pressed&PRG32_BTN_RIGHT) g_page=(uint8_t)((g_page+1)%ARRAY_LEN(pages));
  if(pressed&PRG32_BTN_LEFT) g_page=(uint8_t)((g_page+ARRAY_LEN(pages)-1)%ARRAY_LEN(pages));
  if((pressed&PRG32_BTN_B)||(pressed&PRG32_BTN_START)) g_page=(uint8_t)((g_page+1)%ARRAY_LEN(pages));
  g_prev_input=input; g_frame++;
  prg32_band_set_text(PRG32_BAND_TOP,page_names[g_page]);
}
void devicedemo_draw(void){
  uint32_t input=prg32_input_read();
  pages[g_page](prg32_ticks_ms(),input);
}
