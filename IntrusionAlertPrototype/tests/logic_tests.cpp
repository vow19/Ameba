#include <assert.h>
#include <stdio.h>
#include "../IntrusionLogic.h"

static unsigned passed = 0;
#define CASE(name) printf("PASS: %s\n", name); ++passed
static uint32_t seq = 0;
static bool frame(IntrusionLogic &a, uint32_t t, bool inside, bool expanded) {
    return a.update(++seq,t,inside,expanded);
}
static void enter(IntrusionLogic &a, uint32_t t) {
    assert(!frame(a,t,true,true));
    assert(!frame(a,t+100,true,true));
    assert(frame(a,t+200,true,true));
    assert(a.warning);
}
static void leave(IntrusionLogic &a, uint32_t t) {
    for (uint32_t dt=0; dt<=1500; dt+=100) frame(a,t+dt,false,false);
    assert(!a.warning);
}
int main() {
    {
        IntrusionLogic a;
        assert(!a.warning && a.events==0 && a.stale(0));
        frame(a,0,false,false); assert(!a.stale(0) && !a.warning && a.events==0);
        CASE("startup unknown then empty frame SAFE");
    }
    PersonBox in={.4f,.1f,.6f,.8f,80};
    PersonBox outside={.01f,.1f,.15f,.8f,80};
    assert(footInRoi(in) && !footInRoi(outside));
    PersonBox low=in; low.score=49; assert(!footInRoi(low));
    low.score=50; assert(footInRoi(low)); CASE("confidence and bottom center");
    {
        PersonBox edge={.2f,.1f,.4f,AlertConfig::ROI_TOP,80};
        assert(footInRoi(edge));
        edge.ymax=AlertConfig::ROI_BOTTOM; assert(footInRoi(edge));
        edge.ymax+=.001f; assert(!footInRoi(edge));
        assert(footInRoi(edge,AlertConfig::EXIT_MARGIN)); CASE("inclusive ROI boundary and exit margin");
    }
    {
        PersonBox b=in; b.xmax=NAN; assert(!footInRoi(b));
        b=in; b.ymin=b.ymax; assert(!footInRoi(b));
        b=in; b.xmin=-.1f; assert(!footInRoi(b)); CASE("invalid coordinates rejected");
    }
    {
        IntrusionLogic a;
        for(int i=0;i<30;++i)frame(a,i*100,footInRoi(outside),false);
        assert(a.events==0 && !a.warning); CASE("person outside never triggers");
    }
    {
        IntrusionLogic a;
        frame(a,0,true,true); frame(a,100,false,false); frame(a,200,true,true);
        assert(!a.warning && a.events==0); CASE("single frame false positive rejected");
    }
    {
        IntrusionLogic a;
        a.update(10,0,true,true);
        for(int i=0;i<100;++i)a.update(10,200,true,true);
        assert(!a.warning && a.events==0); CASE("duplicate inference never confirms");
    }
    {
        IntrusionLogic a;
        frame(a,0,true,true);frame(a,1,true,true);frame(a,2,true,true);
        assert(!a.warning);frame(a,200,true,true);assert(a.warning);
        CASE("minimum duration as well as frame count");
    }
    {
        IntrusionLogic a; enter(a,0);
        for(int i=3;i<100;++i)frame(a,i*100,true,true);
        assert(a.events==1); CASE("continuous presence counts once");
        leave(a,10000); enter(a,12000); assert(a.events==2); CASE("confirmed exit then reentry counts again");
    }
    {
        IntrusionLogic a; enter(a,0);
        frame(a,300,false,false);frame(a,400,false,false);frame(a,500,true,true);
        assert(a.warning && a.events==1); CASE("brief miss retains warning and count");
        for(int i=6;i<30;++i)frame(a,i*100,false,true);
        assert(a.warning && a.events==1); CASE("boundary jitter held by expanded ROI");
    }
    {
        IntrusionLogic a; enter(a,0);
        a.tick(5000); assert(a.stale(5000) && a.warning && a.events==1);
        frame(a,5100,true,true); assert(!a.stale(5100) && a.events==1);
        CASE("stale data does not release or recount");
        frame(a,5200,false,false); frame(a,8000,false,false);
        assert(a.warning); leave(a,8100); CASE("gap cannot satisfy exit dwell");
    }
    {
        IntrusionLogic a;
        frame(a,0,true,true);frame(a,100,true,true);frame(a,2000,true,true);
        assert(!a.warning);frame(a,2100,true,true);assert(!a.warning);frame(a,2200,true,true);assert(a.warning);
        CASE("gap resets entry confirmation");
    }
    {
        IntrusionLogic a;
        PersonBox boxes[]={outside,in,outside}; bool hit=false;
        for(auto &b:boxes)hit=hit||footInRoi(b);
        frame(a,0,hit,hit);frame(a,100,hit,hit);frame(a,200,hit,hit);
        assert(a.warning&&a.events==1);
        frame(a,300,true,true);assert(a.events==1); CASE("multiple people are one occupied-region event");
    }
    {
        IntrusionLogic a; uint32_t t=UINT32_MAX-150;
        enter(a,t); assert(a.events==1 && !a.stale(t+250));
        leave(a,t+400); assert(!a.warning); CASE("millis wraparound");
    }
    {
        IntrusionLogic a; a.update(UINT32_MAX-1,0,true,true);
        a.update(UINT32_MAX,100,true,true);a.update(0,200,true,true);
        assert(a.warning);a.update(UINT32_MAX,300,false,false);assert(a.lastSequence==0);
        CASE("sequence wrap and old result ignored");
    }
    printf("TOTAL %u cases passed\n",passed);
}
