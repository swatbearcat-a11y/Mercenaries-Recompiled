/* Production player-state logger (multiplayer M0) exercised against stubbed
 * guest memory, through the real dev_spawn.h helpers and multiplayer tick. */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dev_menu.h"
static unsigned char memory[0x4000000];
static uint32_t g_esp,g_ecx,g_eax,g_xbox_mem_offset=0x10000,current_va;
typedef void (*recomp_func_t)(void);
typedef struct {uint32_t esp,ecx,eax;} recomp_saved_guest_cpu_context;
static void *guest_ptr(uint32_t p){assert(p<sizeof(memory));return memory+p;}
static uint8_t guest_u8(uint32_t p){assert(p<sizeof(memory));return memory[p];}
static uint32_t guest_u32(uint32_t p){uint32_t v;memcpy(&v,guest_ptr(p),4);return v;}
static float guest_f32(uint32_t p){float v;memcpy(&v,guest_ptr(p),4);return v;}
static void put(uint32_t p,uint32_t v){memcpy(guest_ptr(p),&v,4);}
static void putf(uint32_t p,float x,float y,float z){float v[3]={x,y,z};memcpy(guest_ptr(p),v,12);}
static void recomp_guest_push_u32(uint32_t v){g_esp-=4;put(g_esp,v);}
static unsigned saves,restores;
static void recomp_save_guest_cpu_context(recomp_saved_guest_cpu_context *s){saves++;*s=(recomp_saved_guest_cpu_context){g_esp,g_ecx,g_eax};}
static void recomp_restore_guest_cpu_context(const recomp_saved_guest_cpu_context *s){restores++;g_esp=s->esp;g_ecx=s->ecx;g_eax=s->eax;}
/* dev_spawn.h dependencies that these checks never reach. */
unsigned recomp_dev_vehicle_count(void){return 0;}
const DevVehicle *recomp_dev_vehicle_at(unsigned i){(void)i;return NULL;}
unsigned recomp_dev_troop_count(void){return 0;}
const DevVehicle *recomp_dev_troop_at(unsigned i){(void)i;return NULL;}
void recomp_dev_battle_refresh_player(void){}
void recomp_dev_relations_tick(void){}
void recomp_dev_request_boids(void){}
unsigned recomp_dev_battle_flags(void){return 0;}
void recomp_dev_battle_set(unsigned v){(void)v;}
int recomp_dev_take_spawn(unsigned *i){(void)i;return 0;}
void recomp_dev_spawn_result(int ok,const char *m){(void)ok;(void)m;}
void recomp_dev_spawn_progress(const char *m){(void)m;}
int recomp_freecam_enabled(void){return 0;}
int recomp_freecam_focus(float *p,float *d){(void)p;(void)d;return 0;}
static void xbox_preview_log_event(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
/* Option, log switch and captured output of the logger under test. */
static int logging,hz;static unsigned samples;static char line[1024];
static int xbox_preview_log_enabled(void){return logging;}
static void xbox_preview_log_sample(const char *tag,const char *fmt,...){
 va_list a;
 if(strcmp(tag,"player-state"))return;
 va_start(a,fmt);vsnprintf(line,sizeof(line),fmt,a);va_end(a);samples++;
}
int recomp_dev_player_state_log_hz(void){return hz;}
/* The puppet replay experiment stays off in this fixture. */
int recomp_dev_puppet_replay_delay_ms(void){return 0;}
int recomp_dev_puppet_correct_cm(void){return 200;}
int recomp_dev_puppet_yaw_mode(void){return 1;}
int recomp_dev_puppet_steer(void){return 1;}
/* What the stubbed retail calls return. */
static uint32_t retail_controller,retail_human,retail_weapon;static unsigned lookups,weapon_calls;
static void retail_call(void){
 switch(current_va){
 case 0x8C7E0:assert(guest_u32(g_esp+4)==0x660E4490u);lookups++;g_eax=retail_controller;break;
 case 0x4CEE0:assert(g_ecx==retail_human);weapon_calls++;g_eax=retail_weapon;break;
 default:assert(!"unexpected retail call");
 }
}
static recomp_func_t recomp_lookup(uint32_t va){current_va=va;return retail_call;}
static unsigned long long now_ms;
#define GetTickCount64 player_state_fixture_ticks
static unsigned long long player_state_fixture_ticks(void){return now_ms;}
#include "dev_spawn.h"
#include "multiplayer_guest.h"
/* Multiplayer stays off in this fixture. */
const MpConfig *mp_config(void){static MpConfig off;return &off;}
MpNet *mp_net_default(void){return NULL;}
int mp_net_send(MpNet *n,const void *d,size_t s,int r){(void)n;(void)d;(void)s;(void)r;return 0;}
int mp_net_poll(MpNet *n,MpIncoming *o){(void)n;(void)o;return 0;}
uint8_t mp_net_local_player(MpNet *n){(void)n;return MP_NO_PLAYER;}
unsigned mp_net_peer_count(MpNet *n){(void)n;return 0;}
double mp_net_now_ms(void){return 0;}

enum {CONTROLLER=0x200000,HUMAN=0x210000,OTHER_HUMAN=0x220000,WEAPON=0x260000,
      SEAT=0x250000,MANAGER=0x251000,VEHICLE=0x252000};
static void tick(float seconds){
 union{float f;uint32_t u;}d;d.f=seconds;
 recomp_multiplayer_tick(0x330000u,d.u);
 assert(g_esp==0x800000&&g_ecx==1234&&g_eax==5678&&saves==restores);
}
static void reset(void){
 memset(memory,0,sizeof(memory));memset(&player_state,0,sizeof(player_state));
 g_esp=0x800000;g_ecx=1234;g_eax=5678;now_ms=1000;line[0]=0;
 samples=lookups=weapon_calls=saves=restores=0;logging=1;hz=5;
 retail_controller=CONTROLLER;retail_human=HUMAN;retail_weapon=WEAPON;
 put(0x413F6C,0x4249D707);put(0x413F68,0xC2CBD863);
 put(CONTROLLER,0x2E65E0);put(CONTROLLER+0x10,HUMAN);put(CONTROLLER+0x998,HUMAN);
 put(HUMAN,PLAYER_STATE_HUMAN_VTABLE);putf(HUMAN+0xD0,0,0,-1);putf(HUMAN+0xE0,10,1,20);
 float hp=80,max=100;memcpy(guest_ptr(HUMAN+0x98),&hp,4);memcpy(guest_ptr(HUMAN+0x170),&max,4);
 put(WEAPON,0x2F0000);put(WEAPON+0x1A0,7);
}
int main(void){
 /* Off (option or logging): no guest calls and no output. */
 reset();hz=0;tick(1/30.f);assert(!samples&&!lookups&&!saves);
 reset();logging=0;tick(1/30.f);assert(!samples&&!lookups&&!saves);
 /* Outside gameplay: a marker line and no guest calls. */
 reset();put(0x413F6C,0);tick(1/30.f);
 assert(samples==1&&!lookups&&!saves&&strstr(line,"state=C2CBD863 gameplay=0"));
 /* First sample, then nothing until the 200 ms interval has passed. */
 reset();tick(1/30.f);
 assert(samples==1&&lookups==1&&weapon_calls==1);
 assert(strstr(line,"seq=1 ticks=1 ")&&strstr(line,"world=00330000 state=C2CBD863"));
 assert(strstr(line,"ctrl=00200000 human=00210000 hvt=002E32B8"));
 assert(strstr(line,"pos=10.000,1.000,20.000")&&strstr(line,"yaw=0.0 ")&&strstr(line,"vel_ok=0"));
 assert(strstr(line,"target=00210000 tvt=002E32B8 tpos=10.000,1.000,20.000"));
 assert(strstr(line,"seat=00000000 vehicle=00000000")&&strstr(line,"hp=80.00/100.00"));
 assert(strstr(line,"weapon=00260000 wvt=002F0000 wtype=7"));
 for(int i=0;i<5;i++){now_ms+=33;tick(1/30.f);}
 assert(samples==1&&lookups==1);
 /* Velocity uses the guest frame time accumulated since the last sample. */
 putf(HUMAN+0xE0,12,1,20);putf(HUMAN+0xD0,-1,0,0);now_ms=1200;tick(1/30.f);
 assert(samples==2&&strstr(line,"seq=2 ticks=6 dt=0.2000"));
 assert(strstr(line,"vel=10.000,0.000,0.000 vel_ok=1")&&strstr(line,"yaw=90.0 "));
 /* Seat -> manager -> vehicle chain, and the entity the controller reports. */
 put(HUMAN+0x768,SEAT);put(SEAT,MANAGER);put(MANAGER,VEHICLE);
 put(VEHICLE,0x2E21A8);put(VEHICLE+0x58,0xABCD);putf(VEHICLE+0xE0,30,2,40);put(CONTROLLER+0x10,VEHICLE);
 now_ms=1400;tick(1/30.f);
 assert(strstr(line,"seat=00250000 vehicle=00252000 vvt=002E21A8 vmodel=0000ABCD"));
 assert(strstr(line,"target=00252000 tvt=002E21A8 tpos=30.000,2.000,40.000"));
 /* Only the player's human class gets the retail weapon getter. */
 put(HUMAN,0x2E2CE0);now_ms=1600;tick(1/30.f);
 assert(weapon_calls==3&&strstr(line,"hvt=002E2CE0")&&strstr(line,"weapon=00000000 wvt=00000000 wtype=0"));
 /* A different human never produces a velocity from the previous one. */
 retail_human=OTHER_HUMAN;put(CONTROLLER+0x998,OTHER_HUMAN);
 put(OTHER_HUMAN,PLAYER_STATE_HUMAN_VTABLE);putf(OTHER_HUMAN+0xE0,500,1,500);
 now_ms=1800;tick(1/30.f);assert(strstr(line,"human=00220000")&&strstr(line,"vel_ok=0"));
 now_ms=2000;tick(1/30.f);assert(strstr(line,"vel=0.000,0.000,0.000 vel_ok=1"));
 /* No controller: no human, no weapon call, no fabricated position. */
 retail_controller=0;now_ms=2200;tick(1/30.f);
 assert(strstr(line,"ctrl=00000000 human=00000000")&&strstr(line,"pos=0.000,0.000,0.000")&&weapon_calls==5);
 /* Faster rates and invalid frame times. */
 reset();hz=20;tick(NAN);now_ms+=49;tick(2.0f);assert(samples==1);
 now_ms+=1;tick(-1.0f);assert(samples==2&&strstr(line,"ticks=2 dt=0.0000"));
 puts("PASS: off/disabled/no-gameplay paths, rate limit, position/heading/velocity, vehicle chain, controller entity, weapon getter gate, human change, missing controller, invalid frame times and CPU-context restoration");
 return 0;
}
