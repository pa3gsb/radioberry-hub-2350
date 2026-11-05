#include "radioberry/agc.h"
#include "websdr_service.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
static void feed(rb_dsp_agc *a,float level,unsigned count){for(unsigned n=0;n<count;++n)rb_dsp_agc_detect(a,level);}
static float release_gain(uint32_t mode)
{
    rb_dsp_agc a;rb_dsp_agc_init(&a,12000,.25f);rb_dsp_agc_set(&a,mode,-80);
    feed(&a,.5f,12000);feed(&a,.005f,1200);return a.gain;
}
static bool accepted;
static unsigned calls;
static bool apply(void *context,uint32_t mode,int32_t threshold){(void)context;(void)mode;(void)threshold;++calls;return accepted;}
static int request(rb_web_service *s,const char *body,rb_web_response *r)
{
    char data[512];int n=snprintf(data,sizeof(data),"POST /api/agc HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: %u\r\n\r\n%s",(unsigned)strlen(body),body);
    rb_web_request(s,data,(size_t)n,1000,r);return r->status;
}
int rb_test_agc(void)
{
    rb_dsp_agc a;CHECK(rb_dsp_agc_init(&a,12000,.25f));feed(&a,.01f,12000);CHECK(fabsf(a.gain-25.f)<.05f);
    float old=a.gain;CHECK(!rb_dsp_agc_set(&a,4,-80)&&a.gain==old);CHECK(!rb_dsp_agc_set(&a,2,-101));
    CHECK(!rb_dsp_agc_set(&a,2,-19));CHECK(!rb_dsp_agc_init(&a,0,.25f));
    CHECK(rb_dsp_agc_set(&a,RB_AGC_NORMAL,-20));feed(&a,.0001f,24000);CHECK(fabsf(a.gain-2.5f)<.001f);
    CHECK(rb_dsp_agc_set(&a,RB_AGC_OFF,-100));feed(&a,.0001f,12000);CHECK(fabsf(a.gain-1.f)<.001f);
    CHECK(rb_dsp_agc_set(&a,RB_AGC_NORMAL,-100));feed(&a,0,96000);CHECK(a.gain<=10000.f&&a.gain>9900.f);
    float fast=release_gain(RB_AGC_FAST),normal=release_gain(RB_AGC_NORMAL),slow=release_gain(RB_AGC_SLOW);
    CHECK(fast>normal*3&&normal>slow*1.1f);
    CHECK(rb_dsp_agc_init(&a,48000,.35f));feed(&a,.001f,48000);feed(&a,.5f,2400);CHECK(a.gain<.8f);
    rb_dsp_agc_reset(&a);CHECK(a.level==0&&a.gain==1&&a.mode==RB_AGC_NORMAL);
    rb_web_service service;rb_web_response response;rb_web_init_live(&service,1,NULL,NULL);service.agc=apply;
    accepted=true;CHECK(request(&service,"{\"agc_mode\":1,\"agc_threshold_dbfs\":-80}",&response)==200);
    CHECK(calls==1&&service.agc_mode==1&&service.agc_threshold_dbfs==-80&&service.agc_revision==2&&service.config_id==1);
    CHECK(request(&service,"{\"agc_threshold_dbfs\":-80,\"agc_mode\":1}",&response)==200&&calls==1&&service.agc_revision==2);
    accepted=false;CHECK(request(&service,"{\"agc_mode\":3,\"agc_threshold_dbfs\":-60}",&response)==503&&service.agc_mode==1);
    const char *bad[]={"{}","{\"agc_mode\":2}","{\"agc_mode\":4,\"agc_threshold_dbfs\":-80}",
        "{\"agc_mode\":2,\"agc_threshold_dbfs\":-101}","{\"agc_mode\":true,\"agc_threshold_dbfs\":-80}",
        "{\"agc_mode\":2.0,\"agc_threshold_dbfs\":-80}","{\"agc_mode\":02,\"agc_threshold_dbfs\":-80}",
        "{\"agc_mode\":2,\"agc_mode\":2}","{\"agc_mode\":2,\"agc_threshold_dbfs\":-80,\"extra\":1}",
        "{\"agc_mode\":2,\"agc_threshold_dbfs\":-999999999999999999999}"};
    for(unsigned n=0;n<sizeof(bad)/sizeof(bad[0]);++n)CHECK(request(&service,bad[n],&response)==400&&service.agc_mode==1);
    printf("AGC: unity OFF, gain knee/cap, attack and release (fast %.3f / normal %.3f / slow %.3f), acknowledged/idempotent API, backend failure and strict JSON passed.\n",fast,normal,slow);
    return 0;
}
