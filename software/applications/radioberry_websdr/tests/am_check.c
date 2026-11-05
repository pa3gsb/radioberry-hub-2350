#include "websdr_receiver.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
#define PI 3.14159265358979323846
static double measure(float carrier,float modulation,float offset,float tone,double *dc)
{
    rb_dsp_am a; rb_dsp_am_init(&a); double real=0,imag=0,sum=0,power=0; unsigned count=0;
    for(unsigned n=0;n<96000u;++n) {
        double phase=2*PI*offset*n/48000.,envelope=carrier*(1+modulation*cos(2*PI*tone*n/48000.));
        float audio;
        if(rb_dsp_am_push(&a,(float)(envelope*cos(phase)),(float)(envelope*sin(phase)),&audio)&&n>=48000u) {
            double angle=2*PI*tone*n/48000.; real+=audio*cos(angle);imag+=audio*sin(angle);
            sum+=audio;power+=audio*audio;++count;
            if(!isfinite(audio)||fabsf(audio)>.90001f)return -1;
        }
    }
    *dc=sum/count;
    double amplitude=2*hypot(real,imag)/count;
    if(tone==0)return sqrt(power/count);
    return amplitude;
}
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
typedef struct{unsigned blocks,gaps;} capture;
static void block(void *context,const int16_t *pcm){(void)pcm;((capture *)context)->blocks++;}
static void gap(void *context){((capture *)context)->gaps++;}
int rb_test_am(void)
{
    double dc=0,amplitude=measure(.1f,.5f,0,1000,&dc);
    CHECK(amplitude>.08&&amplitude<.2&&fabs(dc)<.001);
    double shifted=measure(.1f,.5f,1200,1000,&dc);
    CHECK(shifted>amplitude*.85&&shifted<amplitude*1.1);
    double weak=measure(.001f,.5f,0,1000,&dc);
    CHECK(fabs(weak/amplitude-1)<.03);
    double carrier=measure(.1f,0,0,0,&dc);CHECK(carrier<.00001);
    double silence=measure(0,0,0,0,&dc);CHECK(silence==0);
    double alias=measure(.1f,.5f,0,9000,&dc);CHECK(alias<amplitude*.01);
    rb_dsp_am a,b;rb_dsp_am_init(&a);rb_dsp_am_init(&b);float first,second;
    for(unsigned n=0;n<6000;++n)rb_dsp_am_push(&a,.1f,0,&first);
    rb_dsp_am_reset(&a);
    for(unsigned n=0;n<6000;++n){bool x=rb_dsp_am_push(&a,.1f,0,&first),y=rb_dsp_am_push(&b,.1f,0,&second);CHECK(x==y&&(!x||first==second));}
    CHECK(!rb_dsp_am_push(&a,NAN,0,&first));
    rb_am_rx rx;rb_am_rx_init(&rx);unsigned blocks=0;
    for(unsigned n=0;n<1920u;++n){CHECK(rb_am_rx_word(&rx,0)==0);int ready=rb_am_rx_word(&rx,0x01000000u);if(ready)++blocks;}
    CHECK(blocks==2&&rx.used==0);CHECK(rb_am_rx_word(&rx,0x01000000u)==-1);
    CHECK(rb_am_rx_word(&rx,0x800000)==0);CHECK(rb_am_rx_word(&rx,0x01800000)==0);
    CHECK(rb_am_rx_word(&rx,0x02000000u)==-1&&rx.used==0&&!rx.have_i);
    uint32_t words[1024];
    for(unsigned n=0;n<1024u;++n)words[n]=n%2u?0u:0x01000000u;
    rb_am_rx_reset(&rx);capture got={0};
    /* Actual board order: Q,I,...,Q,I. The trailing I belongs to the Q at
     * the start of the NEXT buffer. Four buffers yield two audio blocks. */
    for(unsigned n=0;n<4u;++n)rb_am_rx_feed(&rx,words,1024,block,gap,&got);
    CHECK(got.gaps==1&&got.blocks==2&&rx.have_i);
    rb_am_rx_reset(&rx);got=(capture){0};
    words[0]=0;words[1]=0; /* Duplicate I; the second I is a resync point. */
    rb_am_rx_feed(&rx,words,1024,block,gap,&got);
    CHECK(got.gaps==1&&rx.have_i);
    rb_am_queue q;rb_am_queue_init(&q);CHECK(rb_am_queue_open(&q,123));CHECK(!rb_am_queue_open(&q,124));
    int16_t pcm[240]={0};pcm[0]=-32768;pcm[1]=32767;
    uint8_t frame[RB_AM_FRAME_BYTES];
    for(unsigned n=0;n<10u;++n)rb_am_queue_publish(&q,pcm,7100000,1);
    CHECK(q.dropped==2&&q.count==8);CHECK(rb_am_queue_read(&q,frame));
    CHECK(!memcmp(frame,"RBA2",4)&&frame[4]==2&&frame[5]==1&&u32(frame+8)==2);
    CHECK(u32(frame+16)==480&&u32(frame+32)==7100000&&u32(frame+36)==1);
    CHECK(frame[40]==0&&frame[41]==128&&frame[42]==255&&frame[43]==127);
    uint32_t generation=q.generation;rb_am_queue_gap(&q);CHECK(q.generation!=generation&&q.count==0);
    rb_am_queue_publish(&q,pcm,7101000,2);CHECK(rb_am_queue_read(&q,frame));
    CHECK(frame[5]==1&&u32(frame+8)==11&&u32(frame+16)==2640&&u32(frame+36)==2);
    rb_am_queue_close(&q);CHECK(!rb_am_queue_read(&q,frame));CHECK(rb_am_queue_open(&q,124));
    rb_am_queue_publish(&q,pcm,7100000,1);CHECK(rb_am_queue_read(&q,frame)&&u32(frame+8)==0&&u32(frame+16)==0);
    printf("AM DSP: 1 kHz amplitude %.5f, offset-carrier %.5f, weak-signal %.5f, unmodulated RMS %.8f, alias response %.8f; reset, pacing, tags and bounded queue passed.\n",amplitude,shifted,weak,carrier,alias);
    return 0;
}
