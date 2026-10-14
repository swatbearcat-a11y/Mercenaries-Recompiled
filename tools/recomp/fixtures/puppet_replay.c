/* Production M1 puppet replay (puppet_replay.h) exercised against stubbed guest
 * memory, through the real dev_spawn.h helpers and the multiplayer tick. */
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
static void putf(uint32_t p,float v){memcpy(guest_ptr(p),&v,4);}
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
static unsigned lost_events,spawn_events;
static void xbox_preview_log_event(const char *tag,const char *fmt,...){
 (void)fmt;
 if(!strcmp(tag,"puppet")){
  char text[256];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);
  if(!strncmp(text,"lost",4))lost_events++;
  if(!strncmp(text,"spawned",7))spawn_events++;
 }
}
static unsigned samples;static char line[1024];
static int xbox_preview_log_enabled(void){return 1;}
static void xbox_preview_log_sample(const char *tag,const char *fmt,...){
 va_list a;
 if(strcmp(tag,"puppet-replay"))return;
 va_start(a,fmt);vsnprintf(line,sizeof(line),fmt,a);va_end(a);samples++;
}
/* Options under test; the player-state log stays off. */
static int delay_ms,correct_cm,yaw_mode,steer;
int recomp_dev_player_state_log_hz(void){return 0;}
int recomp_dev_puppet_replay_delay_ms(void){return delay_ms;}
int recomp_dev_puppet_correct_cm(void){return correct_cm;}
int recomp_dev_puppet_yaw_mode(void){return yaw_mode;}
int recomp_dev_puppet_steer(void){return steer;}

enum {CONTROLLER=0x200000,HUMAN=0x210000,HUMAN_BODY=0x215000,OTHER_HUMAN=0x220000,OTHER_BODY=0x225000,
      PLAYER_VT=0x2E32B8,ACTOR=0x300000,SPORE=0x301000,PUPPET_VT=0x302000,ACTOR_BODY=0x305000,CONTROL_FN=0x5000,
      TELEPORT_FN=0x5100,YAW_FN=0x5200,BODY_FN=0x5300,PUPPET_GUID=0xB01D0001u,MODEL=0xABCDEF01u,
      REGISTRY_KEYS=0x230000,REGISTRY_VALUES=0x231000,MODEL_OBJECT=0x240000};
static uint32_t calls,unexpected,layer_loads,spawn_calls,teleport_calls,yaw_calls,control_calls,strafe_calls,speed_calls;
typedef struct {float v[3];unsigned tick;} FrameSet;
static FrameSet frames_set[8192];static unsigned frame_count;  /* move frames set through 0x7E910 */
static unsigned strafe_tick;
static int actor_alive;
static uint32_t spawn_template,spawned_name_ok;static float spawn_matrix[16],last_yaw;
static char keys[32][48],values[32][160];static unsigned properties;
static const char *property(const char *key){for(unsigned i=0;i<properties;i++)if(!strcmp(keys[i],key))return values[i];return NULL;}
static double now_seconds;static unsigned current_tick;
typedef struct {uint32_t id,value;double recorded,fed;} Fed;
static Fed fed[4096];static unsigned fed_count;            /* replayed discrete controls */
typedef struct {uint32_t id;float value;unsigned tick;} Move;
static Move moves[8192];static unsigned move_count;        /* movement fed through 0x1E/0x1F */
static double record_time[8192];static uint32_t record_id[8192],record_count;
static recomp_func_t recomp_lookup(uint32_t va);
static unsigned long long now_ms;
#define GetTickCount64 puppet_fixture_ticks
static unsigned long long puppet_fixture_ticks(void){return now_ms;}
#include "dev_spawn.h"
#include "multiplayer_guest.h"
static void retail_call(void){
 calls++;
 uint32_t a1=guest_u32(g_esp+4),a2=guest_u32(g_esp+8);
 switch(current_va){
 case 0x8C7E0:assert(a1==0x660E4490u);g_eax=CONTROLLER;break;
 case BODY_FN:g_eax=g_ecx==HUMAN?HUMAN_BODY:g_ecx==OTHER_HUMAN?OTHER_BODY:g_ecx==ACTOR?ACTOR_BODY:0;break;
 case 0x7E910:{ /* human: set the move frame (copies the normalized vector to controller +0x1C) */
  assert(g_ecx==ACTOR&&frame_count<8192);FrameSet *f=&frames_set[frame_count++];
  memcpy(f->v,guest_ptr(a1),12);f->tick=current_tick;memcpy(guest_ptr(ACTOR_BODY+0x1C),f->v,12);break;}
 case 0x4E970:assert(g_ecx==ACTOR);strafe_calls++;strafe_tick=current_tick;memory[ACTOR_BODY+0x68]=(unsigned char)a1;break;
 case 0x13C5B0:case 0x13C5C0: /* controller: walk (+0x44) and run (+0x48) speed setters */
  assert(g_ecx==ACTOR_BODY);speed_calls++;put(ACTOR_BODY+(current_va==0x13C5B0?0x44:0x48),a1);break;
 case 0x1ED340:g_eax=(a1==dev_hash(PUPPET_TEMPLATE)&&a2==0x134603D7u)?MODEL:0;break;
 case 0x180AE0:case 0x17F490:layer_loads++;break;
 case 0x1EB010:properties=0;break;
 case 0x1EAFB0:assert(properties<32);strcpy(keys[properties],guest_ptr(a1));strcpy(values[properties++],guest_ptr(a2));break;
 case 0x1EB080:break;
 case 0x174480:
  spawn_calls++;assert(g_ecx==0x4031B0);spawn_template=a1;
  spawned_name_ok=!strcmp(guest_ptr(a2),PUPPET_NAME);
  memcpy(spawn_matrix,guest_ptr(guest_u32(g_esp+12)),64);
  memset(guest_ptr(ACTOR),0,0x1000);put(ACTOR,PUPPET_VT);put(ACTOR+8,SPORE);put(SPORE+0x28,PUPPET_GUID);
  memcpy(guest_ptr(ACTOR+0xB0),spawn_matrix,64);actor_alive=1;g_eax=ACTOR;
  putf(ACTOR_BODY+0x44,1.34f);putf(ACTOR_BODY+0x48,4.93f);break; /* the soldier's own speeds */
 case 0x1EC220:g_eax=(a1==PUPPET_GUID&&actor_alive)?ACTOR:0;break;
 case CONTROL_FN:
  assert(g_ecx==ACTOR);control_calls++;
  if(a1==0x1E||a1==0x1F){
   /* The frame the input is relative to is set first, in the same update. */
   assert(frame_count&&frames_set[frame_count-1].tick==current_tick);
   assert(move_count<8192);Move *m=&moves[move_count++];m->id=a1;memcpy(&m->value,&a2,4);m->tick=current_tick;
  }else{
   assert(fed_count<4096);Fed *f=&fed[fed_count++];f->id=a1;f->value=a2;f->fed=now_seconds;
   assert(a2>0&&a2<=record_count&&record_id[a2]==a1);f->recorded=record_time[a2];
  }
  break;
 case TELEPORT_FN:assert(g_ecx==ACTOR);memcpy(guest_ptr(ACTOR+0xB0),guest_ptr(a1),64);teleport_calls++;break;
 case YAW_FN:assert(g_ecx==ACTOR);memcpy(&last_yaw,guest_ptr(g_esp+4),4);yaw_calls++;break;
 default:unexpected++;break;
 }
}
static recomp_func_t recomp_lookup(uint32_t va){current_va=va;return retail_call;}

static const float dt=1.0f/30.0f;
static void set_human(uint32_t human,float x,float yaw_back_z){
 float m[16]={1,0,0,0, 0,1,0,0, 0,0,yaw_back_z,0, x,2,5,1};
 memcpy(guest_ptr(human+0xB0),m,64);
}
static void tick(void){
 union{float f;uint32_t u;}d;d.f=dt;
 now_seconds+=dt;now_ms+=33;
 recomp_multiplayer_tick(0x4031B0u,d.u);
 assert(g_esp==0x800000&&g_ecx==1234&&g_eax==5678&&saves==restores);
}
static void reset(int delay,int correct,int mode){
 memset(memory,0,sizeof(memory));memset(&puppet,0,sizeof(puppet));memset(&player_state,0,sizeof(player_state));
 g_esp=0x800000;g_ecx=1234;g_eax=5678;now_ms=1000;now_seconds=0;current_tick=0;
 delay_ms=delay;correct_cm=correct;yaw_mode=mode;steer=0;  /* steering has its own checks */
 calls=unexpected=layer_loads=spawn_calls=teleport_calls=yaw_calls=control_calls=strafe_calls=speed_calls=0;
 fed_count=move_count=frame_count=strafe_tick=0;record_count=0;properties=0;actor_alive=0;samples=0;
 lost_events=spawn_events=0;saves=restores=0;
 put(0x413F6C,0x4249D707);put(0x413F68,0xC2CBD863);
 put(CONTROLLER,0x2E65E0);put(CONTROLLER+0x998,HUMAN);put(HUMAN,PLAYER_VT);put(OTHER_HUMAN,PLAYER_VT);
 put(PLAYER_VT+0x3BC,BODY_FN);
 /* resident model registry used by dev_human_available */
 put(0x6438A8,1);put(0x6438B4,REGISTRY_KEYS);put(0x6438B0,REGISTRY_VALUES);
 put(REGISTRY_KEYS,MODEL);put(REGISTRY_VALUES,MODEL_OBJECT);
 put(PUPPET_VT+0x1E4,CONTROL_FN);put(PUPPET_VT+0x30,TELEPORT_FN);put(PUPPET_VT+0x68,YAW_FN);
 put(PUPPET_VT+0x3BC,BODY_FN);
}
static void record(uint32_t id){
 assert(record_count<8191);
 record_id[++record_count]=id;record_time[record_count]=puppet.now;
 /* The value is opaque to the replay; an index lets the stub check order and lag. */
 recomp_multiplayer_player_control(HUMAN,id,record_count);
}
/* Player walks along +x at 3 m/s; its character controller holds strafe 0.25,
 * forward i/1000 and a move frame turned i/100 radians from (0,0,-1) (the frame
 * number, so the stub can check timing); the stick sends 0x1F every frame and
 * A (0x06) once at frame 15. */
static void player_frame(unsigned i){
 current_tick=i;
 set_human(HUMAN,10.0f+3.0f*(float)(i*dt),1.0f);
 putf(HUMAN_BODY+0x30,0.25f);putf(HUMAN_BODY+0x34,(float)i/1000.0f);
 putf(HUMAN_BODY+0x1C,sinf((float)i/100.0f));putf(HUMAN_BODY+0x20,0.0f);putf(HUMAN_BODY+0x24,-cosf((float)i/100.0f));
 putf(HUMAN_BODY+0x44,2.27f);putf(HUMAN_BODY+0x48,i<100?8.47f:0.0f);  /* an unset run speed is not copied */
 if(i>1)record(0x1F);
 if(i==15)record(0x06);
}
/* Gait cases: what the player's controller holds and the input the puppet should get. */
static const struct {unsigned char gait,run_allowed;float strafe,forward,want_strafe,want_forward;} gaits[]={
 {1,1,0.0f,0.987f,0.0f,0.5f},   /* walking at full stick (running off): under every run threshold */
 {0,0,0.6f,0.8f,0.3f,0.4f},     /* idle last update, running off: walk */
 {2,1,0.0f,0.8f,0.0f,1.0f},     /* still running under 0.95 (the player's hysteresis): full input */
 {0,1,0.0f,0.987f,0.0f,0.987f}, /* idle last update and free to run: as recorded */
 {1,0,0.1f,0.2f,0.1f,0.2f},     /* gentle walk input: unchanged */
 {0,1,0.005f,0.0f,0.005f,0.0f}, /* inside the 0.01 dead zone: unchanged */
};
/* Steering recording, facing -z from (10,2,5): frames 1-60 walk forward (input
 * 0.987, gait walk, 2.27 m/s along -z), 61-120 stand still, 121-180 drift +x at
 * 0.6 m/s without input (as standing up from a seat does). */
static void steer_frame(unsigned i){
 int walking=i<=60;
 float x=10.0f+(i>120?0.6f*dt*(float)(i-120):0.0f),z=5.0f-2.27f*dt*(float)((i<60?i:60)-1);
 float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, x,2,z,1};
 current_tick=i;
 memcpy(guest_ptr(HUMAN+0xB0),m,64);
 put(HUMAN_BODY+0xC,walking?1:0);memory[HUMAN_BODY+0x65]=0;
 putf(HUMAN_BODY+0x30,0.0f);putf(HUMAN_BODY+0x34,walking?0.987f:0.0f);
 putf(HUMAN_BODY+0x1C,0.0f);putf(HUMAN_BODY+0x20,0.0f);putf(HUMAN_BODY+0x24,-1.0f);
 putf(HUMAN_BODY+0x44,2.27f);putf(HUMAN_BODY+0x48,8.47f);
}
static void place_puppet(float x,float z){putf(ACTOR+0xE0,x);putf(ACTOR+0xE8,z);}
/* Put the puppet at an offset from the frame the next update replays (found the way the tick finds it). */
static void place_from_target(float dx,float dz){
 const PuppetFrame *t=puppet_frame_at(puppet.now+(double)dt-delay_ms/1000.0);
 if(t&&actor_alive)place_puppet(t->matrix[12]+dx,t->matrix[14]+dz);
}
static unsigned steer_tick;
static void steer_step(float dx,float dz){steer_frame(++steer_tick);place_from_target(dx,dz);tick();}
static float fed_value(uint32_t id){for(unsigned i=move_count;i-->0;)if(moves[i].id==id)return moves[i].value;return NAN;}
static void gait_frame(unsigned i){
 unsigned k=(i-1)/60;
 current_tick=i;
 set_human(HUMAN,10.0f,1.0f);
 put(HUMAN_BODY+0xC,gaits[k].gait);memory[HUMAN_BODY+0x65]=gaits[k].run_allowed;
 memory[HUMAN_BODY+0x68]=i>=100;                       /* strafe mode from frame 100 */
 putf(HUMAN_BODY+0x30,gaits[k].strafe);putf(HUMAN_BODY+0x34,gaits[k].forward);
 putf(HUMAN_BODY+0x1C,0.0f);putf(HUMAN_BODY+0x20,0.0f);putf(HUMAN_BODY+0x24,-1.0f);
}
int main(void){
 /* Off: nothing recorded, no guest calls. */
 reset(0,200,1);
 for(unsigned i=1;i<=60;i++){player_frame(i);tick();}
 assert(!calls&&!saves&&puppet.event_head==0&&!samples);

 /* On, 1 s delay, yaw mode 1, 2 m corrections. */
 reset(1000,200,1);
 unsigned spawn_tick=0;
 for(unsigned i=1;i<=150;i++){
  player_frame(i);tick();
  if(!spawn_tick&&spawn_calls)spawn_tick=i;
 }
 assert(!unexpected&&!layer_loads&&spawn_calls==1&&spawn_events==1);
 assert(spawn_tick>=30&&spawn_tick<=32);               /* first frame older than the delay */
 assert(spawn_template==dev_hash(PUPPET_TEMPLATE)&&spawned_name_ok);
 assert(!strcmp(property("name"),PUPPET_NAME)&&!strcmp(property("aiType"),"none"));
 assert(!strcmp(property("path"),"none")&&!strcmp(property("encounter"),"none")&&!strcmp(property("squad"),"none"));
 assert(fabsf(spawn_matrix[12]-(10.0f+3.0f*dt))<0.01f&&spawn_matrix[13]==2.0f&&spawn_matrix[15]==1.0f);
 assert(guest_u8(ACTOR+0x124)&1u);                      /* invincible */
 /* Raw stick events are not replayed; the A press is, one delay later. */
 assert(fed_count==1&&fed[0].id==0x06);
 double lag=fed[0].fed-fed[0].recorded;
 assert(lag>=0.999&&lag<1.0+2*dt);
 /* Movement: every update after the spawn feeds the controller input recorded one delay earlier. */
 unsigned updates=150-spawn_tick+1;
 assert(move_count==2*updates);
 for(unsigned i=0;i<move_count;i++){
  if(moves[i].id==0x1E){assert(moves[i].value==0.25f);continue;}
  int frame=(int)lroundf(moves[i].value*1000.0f),expected=(int)moves[i].tick-30;
  assert(frame==expected||frame==expected-1);
 }
 /* ...relative to the move frame recorded with it, set every update before the input. */
 assert(frame_count==updates);
 for(unsigned i=0;i<frame_count;i++){
  const float *v=frames_set[i].v;
  int frame=(int)lroundf(atan2f(v[0],-v[2])*100.0f),expected=(int)frames_set[i].tick-30;
  assert(fabsf(v[1])<1e-6f&&(frame==expected||frame==expected-1));
 }
 assert(!strafe_calls);                                  /* the player never strafed */
 /* The player's walk and run speeds replace the soldier's once; a zero speed is ignored. */
 assert(speed_calls==2&&guest_f32(ACTOR_BODY+0x44)==2.27f&&guest_f32(ACTOR_BODY+0x48)==8.47f);
 /* The stub puppet never walks, so it drifts 3 m/s behind and is teleported at > 2 m. */
 assert(puppet.teleports>=3&&teleport_calls==puppet.teleports);
 assert(yaw_calls>=100);
 assert(fabsf(last_yaw)<0.001f);                        /* back (0,0,1): yaw = -atan2(0,1) = 0 */
 assert(samples>=20&&strstr(line,"actor=00300000 ai=00000000")&&strstr(line,"move=0.250,")&&strstr(line,"yaw_mode=1"));
 {/* The logged puppet frame is the one just set, the recorded frame of the same update. */
  float a[3],b[3];const char *p=strstr(line," frame="),*q=strstr(line,"rec_frame=");
  assert(p&&q&&sscanf(p," frame=%f,%f,%f",&a[0],&a[1],&a[2])==3&&sscanf(q,"rec_frame=%f,%f,%f",&b[0],&b[1],&b[2])==3);
  assert(fabsf(a[0]-b[0])<0.002f&&fabsf(a[2]-b[2])<0.002f&&fabsf(a[0])>0.5f&&strstr(line,"gait=0 rec_gait=0 speeds=2.27,8.47"));
 }

 /* Gait: the recorded input is scaled so the puppet picks the player's gait; strafe mode follows. */
 reset(1000,0,1);
 unsigned cases=sizeof(gaits)/sizeof(gaits[0]),checked=0;
 for(unsigned i=1;i<=60*cases;i++){gait_frame(i);tick();}
 assert(!unexpected&&spawn_calls==1&&teleport_calls==0);
 for(unsigned i=0;i<move_count;i++){
  unsigned t=moves[i].tick;                              /* replays frame t-30 or t-31; frame f is in case (f-1)/60 */
  if(t<33||(t-32)/60!=(t-31)/60||(t-32)/60>=cases)continue;
  unsigned k=(t-32)/60;
  float want=moves[i].id==0x1E?gaits[k].want_strafe:gaits[k].want_forward;
  assert(fabsf(moves[i].value-want)<1e-5f);checked++;
 }
 assert(checked>=2*50*(cases-1));
 assert(strafe_calls==1&&memory[ACTOR_BODY+0x68]==1&&(strafe_tick==130||strafe_tick==131));

 /* Steering, on steer_frame's recording (replayed 30-31 updates later). */
 reset(1000,0,1);steer=1;steer_tick=0;
 while(steer_tick<69)steer_step(0.0f,0.0f);                /* on the recorded path: as recorded */
 assert(spawn_calls==1&&!unexpected);
 const float *v=frames_set[frame_count-1].v;
 assert(fabsf(v[0])<1e-4f&&fabsf(v[2]+1.0f)<1e-4f&&fabsf(fed_value(0x1F)-0.5f)<1e-5f&&fabsf(guest_f32(ACTOR_BODY+0x44)-2.27f)<0.01f);
 steer_step(1.0f,0.0f);                                    /* 1 m right: turn left atan2(1.5,2.27), 1.2x speed */
 v=frames_set[frame_count-1].v;
 assert(fabsf(v[0]+0.551f)<0.003f&&fabsf(v[1])<1e-6f&&fabsf(v[2]+0.834f)<0.003f);
 assert(fabsf(guest_f32(ACTOR_BODY+0x44)-2.720f)<0.01f&&fabsf(fed_value(0x1F)-0.5f)<1e-5f&&fed_value(0x1E)==0.0f);
 assert(strstr(line,"catch_up=0")!=NULL);
 steer_step(5.0f,0.0f);                                    /* 5 m right: turn 73 degrees, at most 1.5x */
 v=frames_set[frame_count-1].v;
 assert(fabsf(v[0]+0.957f)<0.003f&&fabsf(v[2]+0.290f)<0.003f&&fabsf(guest_f32(ACTOR_BODY+0x44)-3.405f)<0.005f);
 steer_step(5.0f,-3.0f);                                   /* and 3 m ahead: at most 90 degrees */
 v=frames_set[frame_count-1].v;
 assert(fabsf(v[0]+1.0f)<1e-4f&&fabsf(v[2])<1e-4f);
 steer_step(0.0f,0.5f);                                    /* 0.5 m behind: no turn, faster */
 v=frames_set[frame_count-1].v;
 assert(fabsf(v[0])<1e-4f&&fabsf(v[2]+1.0f)<1e-4f&&fabsf(guest_f32(ACTOR_BODY+0x44)-3.020f)<0.01f);
 steer_step(0.0f,-0.5f);                                   /* 0.5 m ahead: slower */
 assert(fabsf(guest_f32(ACTOR_BODY+0x44)-1.520f)<0.01f);
 while(steer_tick<99)steer_step(0.0f,0.0f);
 assert(fed_value(0x1F)==0.0f&&!puppet.catching_up);       /* the recording stands still, on the spot */
 steer_step(0.0f,0.5f);                                    /* 0.5 m from it: walk there */
 v=frames_set[frame_count-1].v;
 assert(fabsf(v[0])<1e-6f&&fabsf(v[2]+1.0f)<1e-6f&&fabsf(fed_value(0x1F)-0.5f)<1e-5f&&puppet.catching_up);
 assert(fabsf(guest_f32(ACTOR_BODY+0x44)-0.75f)<0.005f);  /* 1.5/s * 0.5 m */
 steer_step(0.2f,0.0f);                                    /* 0.2 m: keeps going, at the 0.3x floor */
 v=frames_set[frame_count-1].v;
 assert(fabsf(v[0]+1.0f)<1e-6f&&fabsf(v[2])<1e-6f&&fabsf(fed_value(0x1F)-0.5f)<1e-5f&&fabsf(guest_f32(ACTOR_BODY+0x44)-0.681f)<0.005f);
 steer_step(0.05f,0.0f);                                   /* inside 0.1 m: stops, recorded frame and speeds */
 v=frames_set[frame_count-1].v;
 assert(fed_value(0x1F)==0.0f&&fed_value(0x1E)==0.0f&&!puppet.catching_up&&fabsf(v[2]+1.0f)<1e-6f);
 assert(guest_f32(ACTOR_BODY+0x44)==2.27f);
 steer_step(0.2f,0.0f);                                    /* 0.2 m without catching up: stays */
 assert(fed_value(0x1F)==0.0f&&!puppet.catching_up);
 while(steer_tick<160)steer_step(0.0f,0.0f);               /* the recording drifts +x without input: follow */
 v=frames_set[frame_count-1].v;
 assert(puppet.catching_up&&fabsf(v[0]-1.0f)<1e-3f&&fabsf(v[2])<1e-3f&&fabsf(fed_value(0x1F)-0.5f)<1e-5f);
 assert(fabsf(guest_f32(ACTOR_BODY+0x44)-0.681f)<0.005f&&!unexpected&&teleport_calls==0);

 /* Stuck check: driven, over 0.5 m off and under 0.25 m of movement in a second. */
 memset(&puppet,0,sizeof(puppet));puppet.now=5.0;
 assert(!puppet_stuck(0,0,0.6f,1));                        /* starts the check */
 puppet.now=5.9;assert(!puppet_stuck(0.2f,0,0.6f,1));
 puppet.now=6.01;assert(puppet_stuck(0.2f,0,0.6f,1));       /* 0.2 m in a second */
 assert(!puppet_stuck(0,0,0.6f,1));puppet.now=7.1;assert(!puppet_stuck(0.3f,0,0.6f,1)); /* 0.3 m: moving */
 assert(!puppet_stuck(0,0,0.6f,1));puppet.now=7.5;assert(!puppet_stuck(0,0,0.4f,1));   /* close enough: reset */
 puppet.now=8.6;assert(!puppet_stuck(0,0,0.6f,1));                                      /* restarted */
 assert(!puppet_stuck(0,0,0.6f,0));puppet.now=9.7;assert(!puppet_stuck(0,0,0.6f,1));   /* not driven: reset */
 /* In the game: the stub puppet never moves, so it is stuck once the walking recording is
  * 0.5 m ahead for a second (drift corrections only past 10 m here); never onto the spot
  * where the player stands still. */
 reset(1000,1000,1);steer=1;steer_tick=0;
 while(steer_tick<80){steer_frame(++steer_tick);tick();}
 assert(spawn_calls==1&&teleport_calls==1&&puppet.teleports==1&&puppet.stuck_teleports==1&&!unexpected);
 while(steer_tick<130){steer_frame(++steer_tick);tick();}
 assert(teleport_calls==1&&puppet.occupied&&strstr(line,"occupied=1"));

 /* Mode 2 teleports every update instead of setting yaw. */
 reset(1000,200,2);
 for(unsigned i=1;i<=90;i++){player_frame(i);tick();}
 assert(spawn_calls==1&&yaw_calls==0&&teleport_calls>=55);

 /* Correction off: no teleports even when far behind. */
 reset(1000,0,1);
 for(unsigned i=1;i<=150;i++){player_frame(i);tick();}
 assert(spawn_calls==1&&teleport_calls==0&&puppet.teleports==0);

 /* Losing the puppet: logged, then respawned two seconds later. */
 reset(1000,200,1);
 for(unsigned i=1;i<=60;i++){player_frame(i);tick();}
 assert(spawn_calls==1);
 actor_alive=0;player_frame(61);tick();
 assert(lost_events==1&&puppet.guid==0);
 unsigned i=62;
 for(;i<62+59;i++){player_frame(i);tick();}
 assert(spawn_calls==1);
 for(;i<62+62;i++){player_frame(i);tick();}
 assert(spawn_calls==2&&spawn_events==2);

 /* A new player human restarts the recording: no target until a full delay has passed. */
 reset(1000,200,1);
 for(unsigned j=1;j<=60;j++){player_frame(j);tick();}
 put(CONTROLLER+0x998,OTHER_HUMAN);unsigned before=control_calls;
 for(unsigned j=0;j<20;j++){set_human(OTHER_HUMAN,50.0f,1.0f);tick();}
 assert(puppet.human==OTHER_HUMAN&&control_calls==before&&strstr(line,"err=-1.000"));

 /* Outside normal gameplay: no guest calls. */
 reset(1000,200,1);
 put(0x413F68,0x9F19476E);
 for(unsigned j=1;j<=60;j++){player_frame(j);tick();}
 assert(!calls&&!saves);

 puts("PASS: off path, spawn timing/placement/properties/invincibility, delayed movement-input replay from the character controller, move frame set before the input, walk/run speeds, gait scaling, strafe mode, steering toward the recorded position, stuck teleports, delayed discrete controls without raw stick events, yaw and teleport modes, drift corrections, respawn after loss, recording reset on a new human, non-gameplay gate and CPU-context restoration");
 return 0;
}
