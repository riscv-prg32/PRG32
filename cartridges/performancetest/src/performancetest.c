#include "prg32.h"
#include <stdint.h>

#define CASES 5u
#define MODES 2u
#define FRAMES 60u

static uint32_t g_case, g_mode, g_frame, g_global, g_last_input;
static uint64_t g_update_start, g_update_end;
static int g_running, g_done, g_x, g_y, g_vx, g_vy, g_scroll;
static uint32_t g_seed;
static prg32_performance_summary_t g_summary;
static char g_text[48];

typedef struct {
  uint32_t fps_x100;
  uint32_t frame_us_mean;
  uint32_t update_us;
  uint32_t draw_us;
  uint32_t present_us;
  uint32_t frame_us_p95;
} display_result_t;

static display_result_t g_results[MODES][CASES];
static uint32_t g_frame_times[FRAMES];
static uint32_t g_update_total, g_draw_total, g_present_total, g_frame_total;
static uint32_t g_result_mode;

static const char *case_name(uint32_t n);

static char *append_text(char *out,const char *text){
  while(text&&*text)*out++=*text++;
  *out='\0';return out;
}
static char *append_u32(char *out,uint32_t value){
  char digits[10];uint32_t count=0;
  do{digits[count++]=(char)('0'+value%10u);value/=10u;}while(value);
  while(count)*out++=digits[--count];
  *out='\0';return out;
}
static void draw_u32_at(int x,int y,uint32_t value,uint16_t color){
  append_u32(g_text,value);prg32_gfx_text8(x,y,g_text,color,0);
}
static void draw_fixed_x100(int x,int y,uint32_t value,uint16_t color){
  char *out=append_u32(g_text,value/100u);*out++='.';
  *out++=(char)('0'+(value/10u)%10u);
  *out++=(char)('0'+value%10u);*out='\0';
  prg32_gfx_text8(x,y,g_text,color,0);
}
static void draw_ms_tenths(int x,int y,uint32_t microseconds,uint16_t color){
  uint32_t tenths=(microseconds+50u)/100u;
  char *out=append_u32(g_text,tenths/10u);*out++='.';
  *out++=(char)('0'+tenths%10u);*out='\0';
  prg32_gfx_text8(x,y,g_text,color,0);
}
static void draw_result_endpoint(int y){
  const char *ip=prg32_wifi_current_ip();
  char *out=append_text(g_text,"http://");
  append_text(out,ip&&ip[0]&&ip[0]!='-'?ip:"<board-ip>");
  prg32_gfx_text8(24,y,g_text,PRG32_COLOR_CYAN,0);
  prg32_gfx_text8(24,y+11,"/api/performance.json",PRG32_COLOR_CYAN,0);
}
static void draw_results_table(void){
  uint32_t frame_total=0,update_total=0,draw_total=0,present_total=0;
  uint16_t mode_color=g_result_mode?PRG32_COLOR_GREEN:PRG32_COLOR_CYAN;
  prg32_gfx_text8(4,5,"PERFORMANCE TEST COMPLETE",PRG32_COLOR_WHITE,0);
  prg32_gfx_text8(4,18,g_result_mode?"INDEXED COLOR":"RGB565",mode_color,0);
  prg32_gfx_text8(4,36,"WORKLOAD",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(124,36,"FPS",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(174,36,"TU",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(198,36,"TD",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(238,36,"TP",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(278,36,"P95",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(124,47,"HZ",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(174,47,"US",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(198,47,"MS",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(238,47,"MS",PRG32_COLOR_YELLOW,0);
  prg32_gfx_text8(278,47,"MS",PRG32_COLOR_YELLOW,0);
  for(uint32_t i=0;i<CASES;i++){
    int y=64+(int)i*18;display_result_t *result=&g_results[g_result_mode][i];
    prg32_gfx_text8(4,y,case_name(i),PRG32_COLOR_WHITE,0);
    draw_fixed_x100(124,y,result->fps_x100,PRG32_COLOR_WHITE);
    draw_u32_at(174,y,result->update_us,PRG32_COLOR_WHITE);
    draw_ms_tenths(198,y,result->draw_us,PRG32_COLOR_WHITE);
    draw_ms_tenths(238,y,result->present_us,PRG32_COLOR_WHITE);
    draw_ms_tenths(278,y,result->frame_us_p95,PRG32_COLOR_WHITE);
    frame_total+=result->frame_us_mean;update_total+=result->update_us;
    draw_total+=result->draw_us;present_total+=result->present_us;
  }
  uint32_t mean=(uint32_t)(frame_total/CASES),y=158;
  prg32_gfx_text8(4,y,"OVERALL",mode_color,0);
  draw_fixed_x100(124,y,mean?100000000u/mean:0,mode_color);
  draw_u32_at(174,y,(uint32_t)(update_total/CASES),mode_color);
  draw_ms_tenths(198,y,(uint32_t)(draw_total/CASES),mode_color);
  draw_ms_tenths(238,y,(uint32_t)(present_total/CASES),mode_color);
  prg32_gfx_text8(278,y,"--",mode_color,0);
  draw_result_endpoint(180);
  prg32_gfx_text8(4,216,"LEFT/RIGHT MODE  A RUN AGAIN",PRG32_COLOR_WHITE,0);
}

static const uint16_t probe_rgb[16] = {
  PRG32_COLOR_RED,PRG32_COLOR_GREEN,PRG32_COLOR_BLUE,PRG32_COLOR_YELLOW,
  PRG32_COLOR_GREEN,PRG32_COLOR_BLUE,PRG32_COLOR_YELLOW,PRG32_COLOR_RED,
  PRG32_COLOR_BLUE,PRG32_COLOR_YELLOW,PRG32_COLOR_RED,PRG32_COLOR_GREEN,
  PRG32_COLOR_YELLOW,PRG32_COLOR_RED,PRG32_COLOR_GREEN,PRG32_COLOR_BLUE
};
static const uint8_t probe_pixels[4]={0x1b,0x6c,0xb1,0xc6};
static const uint16_t probe_palette[4]={
  PRG32_COLOR_RED,PRG32_COLOR_GREEN,PRG32_COLOR_BLUE,PRG32_COLOR_YELLOW
};
static prg32_indexed_sprite_t probe_indexed;

enum {
  IDX_BLACK=0,IDX_WHITE=1,IDX_RED=2,IDX_GREEN=3,IDX_BLUE=4,
  IDX_YELLOW=5,IDX_CYAN=6,IDX_MAGENTA=7,IDX_DARK_BLUE=32
};

static uint8_t color_index(uint16_t color){
  switch(color){case PRG32_COLOR_BLACK:return IDX_BLACK;
  case PRG32_COLOR_WHITE:return IDX_WHITE;case PRG32_COLOR_RED:return IDX_RED;
  case PRG32_COLOR_GREEN:return IDX_GREEN;case PRG32_COLOR_BLUE:return IDX_BLUE;
  case PRG32_COLOR_YELLOW:return IDX_YELLOW;case PRG32_COLOR_CYAN:return IDX_CYAN;
  case PRG32_COLOR_MAGENTA:return IDX_MAGENTA;default:return IDX_DARK_BLUE;}
}
static void bench_clear(uint16_t color){
  if(g_mode)prg32_gfx_clear_indexed(color_index(color));else prg32_gfx_clear(color);
}
static void bench_pixel(int x,int y,uint16_t color){
  if(g_mode)prg32_gfx_pixel_indexed(x,y,color_index(color));else prg32_gfx_pixel(x,y,color);
}
static void bench_rect(int x,int y,int w,int h,uint16_t color){
  if(g_mode)prg32_gfx_rect_indexed(x,y,w,h,color_index(color));else prg32_gfx_rect(x,y,w,h,color);
}

static const char *case_name(uint32_t n) {
  switch(n){case 0:return "clear-fill";case 1:return "text-overlay";
  case 2:return "sprite-storm";case 3:return "scrolling";
  default:return "mixed-gameplay";}
}
static const char *case_goal(uint32_t n) {
  switch(n){case 0:return "viewport clear and large rectangle fill bandwidth";
  case 1:return "8x8 text drawing and status overlay load";
  case 2:return "many moving sprite-sized rectangles";
  case 3:return "horizontal scrolling and parallax-like stars";
  default:return "combined text sprites scrolling and playfield objects";}
}
static void reset_scene(void){g_x=24;g_y=32;g_vx=3;g_vy=2;g_scroll=0;g_seed=0x13579bdfu;}
static void reset_case_measurements(void){
  g_update_total=0;g_draw_total=0;g_present_total=0;g_frame_total=0;
}
static void sort_frame_times(void){
  for(uint32_t i=1;i<FRAMES;i++){
    uint32_t value=g_frame_times[i],j=i;
    while(j&&g_frame_times[j-1]>value){g_frame_times[j]=g_frame_times[j-1];--j;}
    g_frame_times[j]=value;
  }
}
static void finish_case_measurements(void){
  display_result_t *result=&g_results[g_mode][g_case];
  sort_frame_times();
  uint32_t mean=(uint32_t)(g_frame_total/FRAMES);
  result->fps_x100=mean?100000000u/mean:0;
  result->frame_us_mean=mean;
  result->update_us=(uint32_t)(g_update_total/FRAMES);
  result->draw_us=(uint32_t)(g_draw_total/FRAMES);
  result->present_us=(uint32_t)(g_present_total/FRAMES);
  result->frame_us_p95=g_frame_times[(FRAMES*95u+99u)/100u-1u];
}
static int begin_case(void){
  prg32_perf_case_desc_t d;
  d.abi_version=PRG32_PERF_ABI_VERSION;d.struct_size=sizeof(d);
  d.case_index=g_case;
  d.color_mode=g_mode?PRG32_PERF_COLOR_INDEXED:PRG32_PERF_COLOR_RGB565;
  d.name=case_name(g_case);d.metric_goal=case_goal(g_case);
  reset_scene();reset_case_measurements();g_frame=0;
  return prg32_perf_case_begin(&d,FRAMES);
}
static void start(void){
  prg32_perf_suite_desc_t s;
  s.abi_version=PRG32_PERF_ABI_VERSION;s.struct_size=sizeof(s);
  s.suite_version=1;s.name="setup-performance-test";
  g_case=g_mode=g_frame=g_global=0;g_done=0;g_result_mode=0;
  g_summary.frames=0;
  g_running=prg32_perf_begin(&s)==0&&begin_case()==0;
}
static void draw_probe(void){
  for(int i=0;i<24;i++){int x=(i*13+(int)g_frame)%316,y=28+(i*29)%116;
    if(g_mode)prg32_sprite_draw_indexed(x,y,&probe_indexed,0);
    else prg32_sprite_draw_frame(x,y,4,4,probe_rgb,0,PRG32_COLOR_WHITE);}
}
static void header(void){
  prg32_gfx_text8(8,8,case_name(g_case),PRG32_COLOR_WHITE,0);
  prg32_gfx_text8(8,20,g_mode?"indexed":"rgb565",PRG32_COLOR_GREEN,0);
  bench_rect(248,8,(int)((g_frame+1u)*60u/FRAMES),5,PRG32_COLOR_CYAN);
}
static void workload(void){
  g_seed=g_seed*1664525u+1013904223u;g_scroll=(g_scroll+3)%PRG32_GAME_W;
  g_x+=g_vx;g_y+=g_vy;if(g_x<=0||g_x>=308)g_vx=-g_vx;
  if(g_y<=24||g_y>=188)g_vy=-g_vy;
  bench_clear((g_case==1)?PRG32_COLOR_BLACK:0x0008);header();
  if(g_case==0){for(int i=0;i<8;i++)bench_rect(10+i*7,38+i*18,300-i*22,10,(i&1)?PRG32_COLOR_BLUE:PRG32_COLOR_MAGENTA);}
  else if(g_case==1){for(int y=32;y<190;y+=8)prg32_gfx_text8(8,y,"REGISTER TRACE  FRAME BUDGET  ABI PRG2",y&8?PRG32_COLOR_GREEN:PRG32_COLOR_CYAN,0);}
  else if(g_case==2){for(int i=0;i<36;i++){int x=(i*23+g_frame*(2+(i&3)))%306;int y=30+(i*19+(g_seed>>(i&7)))%150;bench_rect(x,y,12,12,(i&1)?PRG32_COLOR_RED:PRG32_COLOR_BLUE);}}
  else {int count=g_case==3?72:42;for(int i=0;i<count;i++){int x=(i*37+g_scroll*(1+(i&3)))%320;int y=28+(i*17+g_frame*(i&1))%125;bench_pixel(x,y,(i&1)?PRG32_COLOR_WHITE:PRG32_COLOR_CYAN);}for(int y=152;y<200;y+=9)for(int x=-(g_scroll%44);x<320;x+=44)bench_rect(x,y,20,2,PRG32_COLOR_YELLOW);}
  if(g_case==4){bench_rect(g_x,g_y,12,12,PRG32_COLOR_RED);prg32_gfx_text8(8,136,"MIXED GAMEPLAY",PRG32_COLOR_WHITE,0);}
  draw_probe();
}
void performancetest_init(void){
  prg32_palette_set(IDX_DARK_BLUE,0x0008);
  probe_indexed.pixels=probe_pixels;probe_indexed.palette=probe_palette;
  probe_indexed.width=4;probe_indexed.height=4;probe_indexed.frame_count=1;
  probe_indexed.palette_count=4;probe_indexed.bits_per_pixel=PRG32_SPRITE_BPP_2;
  probe_indexed.transparent_index=-1;g_last_input=0;
  prg32_gfx_set_fullscreen(1);start();
}
void performancetest_update(void){
  uint32_t input=prg32_input_read(),pressed=input&~g_last_input;g_last_input=input;
  if(g_running&&(pressed&(PRG32_BTN_B|PRG32_BTN_SELECT))){prg32_perf_abort();g_running=0;g_done=2;}
  else if(g_done==1&&(pressed&(PRG32_BTN_LEFT|PRG32_BTN_RIGHT)))g_result_mode^=1u;
  else if(g_done&&(pressed&PRG32_BTN_A))start();
  g_update_start=prg32_perf_now_us();
  if(g_running){g_scroll=(g_scroll+1)%320;}
  g_update_end=prg32_perf_now_us();
}
void performancetest_draw(void){
  if(!g_running){prg32_gfx_clear(PRG32_COLOR_BLACK);
    if(g_done==1){
      draw_results_table();
    }else{
      prg32_gfx_text8(24,18,"PERFORMANCE TEST ABORTED",PRG32_COLOR_YELLOW,0);
      prg32_gfx_text8(24,54,"NO RESULTS WERE SAVED",PRG32_COLOR_WHITE,0);
      prg32_gfx_text8(24,78,"A: RUN AGAIN",PRG32_COLOR_WHITE,0);
    }
    return;}
  uint64_t draw0=prg32_perf_now_us();workload();uint64_t draw1=prg32_perf_now_us();
  uint64_t present0=prg32_perf_now_us();prg32_gfx_present();uint64_t present1=prg32_perf_now_us();
  prg32_perf_sample_t s;
  s.abi_version=PRG32_PERF_ABI_VERSION;s.struct_size=sizeof(s);
  s.frame_index=g_global;s.update_us=(uint32_t)(g_update_end-g_update_start);
  s.draw_us=(uint32_t)(draw1-draw0);s.present_us=(uint32_t)(present1-present0);
  s.frame_total_us=(uint32_t)(present1-g_update_start);s.input_mask=g_last_input;
  if(prg32_perf_record(&s)!=0){prg32_perf_abort();g_running=0;g_done=2;return;}
  g_frame_times[g_frame]=s.frame_total_us;
  g_update_total+=s.update_us;g_draw_total+=s.draw_us;
  g_present_total+=s.present_us;g_frame_total+=s.frame_total_us;
  ++g_frame;++g_global;
  if(g_frame==FRAMES){finish_case_measurements();
    if(prg32_perf_case_end()!=0){prg32_perf_abort();g_running=0;g_done=2;return;}
    ++g_case;if(g_case==CASES){g_case=0;++g_mode;}
    if(g_mode==MODES){
      if(prg32_perf_end()!=0||prg32_perf_get_summary(&g_summary)!=0){
        prg32_perf_abort();g_running=0;g_done=2;
      }else{g_running=0;g_done=1;}
    }
    else if(begin_case()!=0){prg32_perf_abort();g_running=0;g_done=2;}}
}
