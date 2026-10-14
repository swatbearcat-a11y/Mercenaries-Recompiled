/* The M2 multiplayer session (multiplayer_session.h) against stubbed guest
 * memory and a fake transport, through the real dev_spawn.h, puppet_drive.h
 * and the multiplayer tick: the off path; the local player's state 20 times a
 * second in gameplay only, and its controls; remote players drawn
 * interp_ms behind their newest update with the clock offset, interpolation
 * and a short carry-on; their controls fed on time; puppets removed when a
 * player leaves or the host is lost; malformed updates ignored. */
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
static unsigned removed_events,spawned_events;
static void xbox_preview_log_event(const char *tag,const char *fmt,...){
 char text[256];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);
 if(!strcmp(tag,"puppet")&&!strncmp(text,"removed",7))removed_events++;
 if(!strcmp(tag,"puppet")&&!strncmp(text,"spawned",7))spawned_events++;
}
static char puppet_lines[8][1024],session_line[512];static unsigned puppet_samples[8];
static int xbox_preview_log_enabled(void){return 1;}
static void xbox_preview_log_sample(const char *tag,const char *fmt,...){
 char text[1024];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);
 if(!strcmp(tag,"mp-puppet")){unsigned p=(unsigned)atoi(text+7);assert(p<8);strcpy(puppet_lines[p],text);puppet_samples[p]++;}
 if(!strcmp(tag,"multiplayer"))snprintf(session_line,sizeof(session_line),"%s",text);
}
/* The replay experiment and the player-state log stay off. */
int recomp_dev_player_state_log_hz(void){return 0;}
int recomp_dev_puppet_replay_delay_ms(void){return 0;}
int recomp_dev_puppet_correct_cm(void){return 200;}
int recomp_dev_puppet_yaw_mode(void){return 1;}
int recomp_dev_puppet_steer(void){return 1;}

enum {CONTROLLER=0x200000,HUMAN=0x210000,HUMAN_BODY=0x215000,PLAYER_VT=0x2E32B8,PUPPET_VT=0x2F0000,
      ACTORS=0x300000,CONTROL_FN=0x5000,TELEPORT_FN=0x5100,YAW_FN=0x5200,BODY_FN=0x5300,REMOVE_FN=0x5400,
      MODEL=0xABCDEF01u,REGISTRY_KEYS=0x230000,REGISTRY_VALUES=0x231000,MODEL_OBJECT=0x240000};
/* Spawned actor k: ACTOR(k), its spore at +0x1000 and its controller at +0x5000. */
#define ACTOR(k) (ACTORS+(uint32_t)(k)*0x10000u)
#define GUID(k) (0xB01D0001u+(uint32_t)(k))
static unsigned actors,calls,unexpected;static int alive[8];static char names[8][32];
static unsigned removes[8],teleports[8],yaw_calls[8];static float frame_set[8][3],last_input[8][2];
typedef struct {unsigned actor;uint32_t id,value;double ms;} Control;
static Control controls[16384];static unsigned control_count;
static double clock_ms;static unsigned long long now_ms;
#define GetTickCount64 session_fixture_ticks
static unsigned long long session_fixture_ticks(void){return now_ms;}
static recomp_func_t recomp_lookup(uint32_t va);
#include "dev_spawn.h"
#include "multiplayer_guest.h"

/* Fake transport: what the session sends, and an inbox the fixture fills. */
static MpConfig cfg;
const MpConfig *mp_config(void){return &cfg;}
static int fake_net;static unsigned default_calls;
MpNet *mp_net_default(void){default_calls++;return (MpNet*)&fake_net;}
typedef struct {uint8_t data[1024];size_t size;int reliable;double ms;} Sent;
static Sent sent[4096];static unsigned sent_count;
int mp_net_send(MpNet *n,const void *d,size_t s,int r){
 assert(n==(MpNet*)&fake_net&&s<=1024&&sent_count<4096);
 Sent *m=&sent[sent_count++];memcpy(m->data,d,s);m->size=s;m->reliable=r;m->ms=clock_ms;return 1;
}
static MpIncoming inbox[2048];static unsigned inbox_head,inbox_tail;
int mp_net_poll(MpNet *n,MpIncoming *o){(void)n;if(inbox_tail==inbox_head)return 0;*o=inbox[inbox_tail++%2048];return 1;}
uint8_t mp_net_local_player(MpNet *n){(void)n;return 0;}
unsigned mp_net_peer_count(MpNet *n){(void)n;return 2;}
double mp_net_now_ms(void){return clock_ms;}

static int actor_index(uint32_t a){for(unsigned k=0;k<actors;k++)if(ACTOR(k)==a)return (int)k;return -1;}
static void retail_call(void){
 calls++;
 uint32_t a1=guest_u32(g_esp+4),a2=guest_u32(g_esp+8);
 int k=actor_index(g_ecx);
 switch(current_va){
 case 0x8C7E0:assert(a1==0x660E4490u);g_eax=CONTROLLER;break;
 case BODY_FN:g_eax=g_ecx==HUMAN?HUMAN_BODY:k>=0?ACTOR(k)+0x5000:0;break;
 case 0x7E910:assert(k>=0);memcpy(frame_set[k],guest_ptr(a1),12);memcpy(guest_ptr(ACTOR(k)+0x501C),frame_set[k],12);break;
 case 0x4E970:assert(k>=0);memory[ACTOR(k)+0x5068]=(unsigned char)a1;break;
 case 0x13C5B0:case 0x13C5C0:assert(actor_index(g_ecx-0x5000)>=0);put(g_ecx+(current_va==0x13C5B0?0x44:0x48),a1);break;
 case 0x1ED340:g_eax=(a1==dev_hash(PUPPET_TEMPLATE)&&a2==0x134603D7u)?MODEL:0;break;
 case 0x180AE0:case 0x17F490:case 0x1EB010:case 0x1EAFB0:case 0x1EB080:break;
 case 0x174480:{
  assert(g_ecx==0x4031B0&&actors<8);unsigned j=actors++;uint32_t a=ACTOR(j);float m[16];
  snprintf(names[j],sizeof(names[j]),"%s",(const char*)guest_ptr(a2));
  memcpy(m,guest_ptr(guest_u32(g_esp+12)),64);
  memset(guest_ptr(a),0,0x6000);put(a,PUPPET_VT);put(a+8,a+0x1000);put(a+0x1028,GUID(j));
  memcpy(guest_ptr(a+0xB0),m,64);alive[j]=1;g_eax=a;putf(a+0x5044,1.34f);putf(a+0x5048,4.93f);break;}
 case 0x1EC220:g_eax=0;for(unsigned j=0;j<actors;j++)if(a1==GUID(j)&&alive[j])g_eax=ACTOR(j);break;
 case REMOVE_FN:assert(k>=0&&alive[k]);alive[k]=0;removes[k]++;break;
 case CONTROL_FN:{
  assert(k>=0&&control_count<16384);Control *c=&controls[control_count++];c->actor=(unsigned)k;c->id=a1;c->value=a2;c->ms=clock_ms;
  if(a1==0x1E||a1==0x1F)memcpy(&last_input[k][a1-0x1E],&a2,4);
  break;}
 case TELEPORT_FN:assert(k>=0);memcpy(guest_ptr(ACTOR(k)+0xB0),guest_ptr(a1),64);teleports[k]++;break;
 case YAW_FN:assert(k>=0);yaw_calls[k]++;break;
 default:unexpected++;break;
 }
}
static recomp_func_t recomp_lookup(uint32_t va){current_va=va;return retail_call;}

/* Remote players' clocks run SENDER_GAP ms behind ours; updates take LATENCY ms. */
#define SENDER_GAP 5000.0
#define LATENCY 20.0
typedef struct {double at;MpIncoming in;} Pending;
static Pending pending[4096];static unsigned pending_count;
static void schedule(double at,MpIncomingKind kind,uint8_t player,const uint8_t *data,size_t size){
 assert(pending_count<4096);Pending *p=&pending[pending_count++];memset(p,0,sizeof(*p));
 p->at=at;p->in.kind=kind;p->in.player=player;p->in.size=(uint16_t)size;if(size)memcpy(p->in.data,data,size);
}
static void remote_state(uint8_t player,double sender_ms,float x,float z,float yaw){
 static uint16_t seq;uint8_t m[60]={0};float position[3]={x,2,z},move[2]={0,0.987f},frame[3]={1,0,0},speed[2]={2.27f,8.47f};
 m[0]=16;m[1]=player;++seq;memcpy(m+2,&seq,2);memcpy(m+4,&sender_ms,8);memcpy(m+12,position,12);memcpy(m+24,&yaw,4);
 memcpy(m+28,move,8);memcpy(m+36,frame,12);memcpy(m+48,speed,8);m[56]=1;
 schedule(sender_ms+SENDER_GAP+LATENCY,MP_IN_MESSAGE,player,m,60);
}
static const float dt=1.0f/30.0f;
static void tick(void){
 union{float f;uint32_t u;}d;d.f=dt;
 clock_ms+=1000.0/30.0;now_ms=(unsigned long long)clock_ms;
 for(unsigned i=0;i<pending_count;i++)if(pending[i].at<=clock_ms){
  pending[i].in.receive_ms=pending[i].at;inbox[inbox_head++%2048]=pending[i].in;pending[i].at=INFINITY;
 }
 recomp_multiplayer_tick(0x4031B0u,d.u);
 assert(g_esp==0x800000&&g_ecx==1234&&g_eax==5678&&saves==restores);
}
static void reset(int mode){
 memset(memory,0,sizeof(memory));memset(&mp,0,sizeof(mp));memset(&cfg,0,sizeof(cfg));
 cfg.mode=mode;cfg.interp_ms=100;cfg.port=27410;strcpy(cfg.address,"127.0.0.1");
 g_esp=0x800000;g_ecx=1234;g_eax=5678;clock_ms=10000.0;now_ms=10000;
 actors=calls=unexpected=control_count=sent_count=inbox_head=inbox_tail=pending_count=default_calls=0;
 removed_events=spawned_events=saves=restores=0;
 memset(alive,0,sizeof(alive));memset(removes,0,sizeof(removes));memset(teleports,0,sizeof(teleports));
 memset(yaw_calls,0,sizeof(yaw_calls));memset(puppet_samples,0,sizeof(puppet_samples));
 put(0x413F6C,0x4249D707);put(0x413F68,0xC2CBD863);
 put(CONTROLLER,0x2E65E0);put(CONTROLLER+0x998,HUMAN);put(HUMAN,PLAYER_VT);put(PLAYER_VT+0x3BC,BODY_FN);
 put(0x6438A8,1);put(0x6438B4,REGISTRY_KEYS);put(0x6438B0,REGISTRY_VALUES);put(REGISTRY_KEYS,MODEL);put(REGISTRY_VALUES,MODEL_OBJECT);
 put(PUPPET_VT+0x10,REMOVE_FN);put(PUPPET_VT+0x1E4,CONTROL_FN);put(PUPPET_VT+0x30,TELEPORT_FN);
 put(PUPPET_VT+0x68,YAW_FN);put(PUPPET_VT+0x3BC,BODY_FN);
 /* The local player stands at (100,2,100) facing -z, well away from every remote player. */
 float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 100,2,100,1};memcpy(guest_ptr(HUMAN+0xB0),m,64);
 putf(HUMAN_BODY+0x30,0.25f);putf(HUMAN_BODY+0x34,0.75f);putf(HUMAN_BODY+0x24,-1.0f);
 putf(HUMAN_BODY+0x44,2.27f);putf(HUMAN_BODY+0x48,8.47f);put(HUMAN_BODY+0xC,1);
}
static float target_x(unsigned player){
 PuppetFrame t;float v[2];assert(mp_remote_target(&mp.remotes[player],mp.remotes[player].render_ms,&t,v));return t.matrix[12];
}

int main(void){
 /* Off: nothing at all, not even the transport. */
 reset(MP_MODE_OFF);
 for(unsigned i=0;i<30;i++)tick();
 recomp_multiplayer_player_control(HUMAN,6,1);
 assert(!calls&&!saves&&!sent_count&&!default_calls&&!mp.out_count);

 /* Host: the local player's state 20 times a second, unreliable, in gameplay only. */
 reset(MP_MODE_HOST);
 for(unsigned i=0;i<30;i++)tick();
 unsigned states=0;const Sent *last=NULL;uint16_t previous_seq=0;
 for(unsigned i=0;i<sent_count;i++)if(sent[i].data[0]==MP_MSG_STATE){
  uint16_t seq;memcpy(&seq,sent[i].data+2,2);assert(!states||seq==previous_seq+1);previous_seq=seq;
  assert(sent[i].size==MP_STATE_SIZE&&!sent[i].reliable);states++;last=&sent[i];
 }
 assert(states>=19&&states<=21&&default_calls==1);
 {double sender;PuppetFrame f;assert(mp_decode_state(last->data,last->size,&sender,&f)&&sender==last->ms);
  assert(f.matrix[12]==100.0f&&f.matrix[13]==2.0f&&f.matrix[14]==100.0f&&fabsf(puppet_yaw(f.matrix+8))<1e-6f);
  assert(f.move[0]==0.25f&&f.move[1]==0.75f&&f.frame[2]==-1.0f&&f.speed[0]==2.27f&&f.speed[1]==8.47f);
  assert(f.gait==1&&!f.run_allowed&&!f.strafe&&strstr(session_line,"sent_states="));}
 put(0x413F68,0x9F19476E);unsigned before=sent_count;           /* a menu: nothing sent */
 for(unsigned i=0;i<30;i++)tick();
 assert(sent_count==before);put(0x413F68,0xC2CBD863);tick();
 /* Controls: kept with their time, sent reliably with the next update; movement and 0x13 are not. */
 double pressed=clock_ms;before=sent_count;
 recomp_multiplayer_player_control(HUMAN,0x06,1);recomp_multiplayer_player_control(HUMAN,0x1F,2);
 recomp_multiplayer_player_control(HUMAN,0x13,3);recomp_multiplayer_player_control(HUMAN+0x100,0x06,4);
 tick();
 {unsigned events=0;
  for(unsigned i=before;i<sent_count;i++)if(sent[i].data[0]==MP_MSG_EVENTS){
   uint16_t count;double t;uint32_t id,value;memcpy(&count,sent[i].data+2,2);memcpy(&t,sent[i].data+4,8);
   memcpy(&id,sent[i].data+12,4);memcpy(&value,sent[i].data+16,4);
   assert(sent[i].reliable&&count==1&&sent[i].size==4+16&&t==pressed&&id==6&&value==1);events++;
  }
  assert(events==1&&mp.out_count==0);}

 /* A remote player walking +x at 2 m/s, updates every 50 ms of its clock for 3 s, and one
  * control (0x06) at 1 s. Drawn 100 ms behind its newest update: render = now - offset - 100. */
 reset(MP_MODE_JOIN);
 double s0=clock_ms-SENDER_GAP+100.0;
 for(unsigned k=0;k<=60;k++)remote_state(1,s0+50.0*k,10.0f+2.0f*(50.0f*k)/1000.0f,5.0f,0.0f);
 {uint8_t e[20]={MP_MSG_EVENTS,1};uint16_t count=1;double t=s0+1000.0;uint32_t id=6,value=7;
  memcpy(e+2,&count,2);memcpy(e+4,&t,8);memcpy(e+12,&id,4);memcpy(e+16,&value,4);
  schedule(t+SENDER_GAP+LATENCY,MP_IN_MESSAGE,1,e,20);}
 int fed_at=-1;unsigned spawn_tick=0,logged=0,checked_behind=0;
 for(unsigned i=1;i<=150;i++){
  tick();
  if(!spawn_tick&&actors)spawn_tick=i;
  if(!mp.remotes[1].active)continue;
  MpRemote *r=&mp.remotes[1];
  if(puppet_samples[1]!=logged){                                      /* while updates flow: 50-100 ms behind */
   const char *b=strstr(puppet_lines[1],"behind_ms=");logged=puppet_samples[1];
   if(r->render_ms>s0&&r->render_ms<s0+2900.0){double behind=atof(b+10);assert(behind>=50.0&&behind<=101.0);checked_behind++;}
  }
  assert(fabs(r->offset_ms-(SENDER_GAP+LATENCY))<1e-6&&fabs(r->render_ms-(clock_ms-SENDER_GAP-LATENCY-100.0))<1e-6);
  double render=r->render_ms;
  if(render>=s0&&render<=s0+3000.0)                                   /* interpolated */
   assert(fabsf(target_x(1)-(10.0f+2.0f*(float)(render-s0)/1000.0f))<1e-3f);
  else if(render>s0+3000.0){                                          /* carried on 200 ms, then held */
   double ahead=render-(s0+3000.0);float x=16.0f+2.0f*(float)(ahead<200.0?ahead:200.0)/1000.0f;
   assert(fabsf(target_x(1)-x)<1e-3f);
  }
  for(unsigned c=0;c<control_count;c++)if(controls[c].id==6&&fed_at<0){
   assert(controls[c].value==7&&controls[c].actor==0);fed_at=(int)i;
   assert(render>=s0+1000.0&&render-1000.0/30.0<s0+1000.0);           /* the first update that reaches it */
  }
 }
 assert(spawn_tick&&actors==1&&!strcmp(names[0],"mp_player_1")&&alive[0]&&spawned_events==1&&fed_at>0);
 {unsigned sixes=0;for(unsigned c=0;c<control_count;c++)sixes+=controls[c].id==6;assert(sixes==1);}
 assert(fabsf(last_input[0][1]-0.5f)<1e-5f&&yaw_calls[0]>0&&!unexpected);  /* walking input, heading set */
 assert(puppet_samples[1]>10&&strstr(puppet_lines[1],"player=1 ")&&strstr(puppet_lines[1],"updates=61"));
 assert(checked_behind>=10);

 /* Out-of-order and malformed updates change nothing. */
 {uint32_t head=mp.remotes[1].snapshot_head;
  remote_state(1,s0+10.0,0,0,0);                                      /* older than the newest */
  uint8_t bad[60]={MP_MSG_STATE,1};float nan_value=NAN;double t=s0+9000.0;memcpy(bad+4,&t,8);memcpy(bad+12,&nan_value,4);
  schedule(clock_ms,MP_IN_MESSAGE,1,bad,60);
  schedule(clock_ms,MP_IN_MESSAGE,1,bad,59);
  tick();tick();assert(mp.remotes[1].snapshot_head==head);}

 /* A second player standing at x=30; then player 1 leaves; then the host is lost. */
 double s1=clock_ms-SENDER_GAP;
 for(unsigned k=0;k<10;k++)remote_state(2,s1+50.0*k,30.0f,5.0f,1.0f);
 for(unsigned i=0;i<20;i++)tick();
 assert(actors==2&&!strcmp(names[1],"mp_player_2")&&alive[0]&&alive[1]&&mp.remotes[2].active);
 schedule(clock_ms,MP_IN_LEFT,1,NULL,0);
 tick();
 assert(removes[0]==1&&!alive[0]&&!mp.remotes[1].active&&alive[1]&&removed_events==1);
 put(0x413F68,0x9F19476E);                                           /* in a menu: noted, applied on return */
 schedule(clock_ms,MP_IN_DISCONNECTED,MP_NO_PLAYER,NULL,0);
 tick();
 assert(alive[1]&&mp.remotes[2].leaving);
 put(0x413F68,0xC2CBD863);tick();
 assert(removes[1]==1&&!alive[1]&&!mp.remotes[2].active&&removed_events==2);
 /* Player 1 comes back: a fresh puppet. */
 double s2=clock_ms-SENDER_GAP;
 for(unsigned k=0;k<5;k++)remote_state(1,s2+50.0*k,40.0f,5.0f,0.0f);
 for(unsigned i=0;i<10;i++)tick();
 assert(actors==3&&!strcmp(names[2],"mp_player_1")&&alive[2]&&mp.remotes[1].received==5);

 puts("PASS: off path, local state at 20 Hz in gameplay only, controls sent reliably, clock offset and "
      "interp delay, interpolation, carry-on and hold, controls fed on time, out-of-order and malformed updates "
      "ignored, two remote players, leaving, losing the host in a menu, rejoining");
 return 0;
}
