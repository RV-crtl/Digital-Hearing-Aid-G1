/* FSM integration scenarios using the real portable production controller.
 * These assertions validate software transitions, NOT radio hardware.
 */
#include "connectivity_controller.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

static ConnectivityOutputs_t run(ConnectivityController_t *c, uint32_t t,
        int ready, int bt, int tc, int paired) {
    ConnectivityInputs_t in={0}; ConnectivityOutputs_t out={0};
    in.now_ms=t;in.interfaces_ready=ready!=0;
    in.bluetooth_connected=bt!=0;in.telecoil_valid=tc!=0;
    in.paired_device_available=paired!=0;
    assert(Connectivity_Update(c,&in,&out)==CONNECTIVITY_OK);
    if(out.mode==CONNECTIVITY_M2_ACTIVE) { assert(!out.muted); assert(out.active_source!=AUDIO_SOURCE_NONE); }
    if(out.mode==CONNECTIVITY_M4_FAULT || out.mode==CONNECTIVITY_M3_IDLE) assert(out.muted);
    return out;
}
int main(void){
    ConnectivityController_t c;
    ConnectivityConfig_t cfg=Connectivity_DefaultConfig();
    assert(Connectivity_Init(&c,&cfg,0)==CONNECTIVITY_OK);
    ConnectivityOutputs_t out=run(&c,10,1,1,1,1);
    assert(out.mode==CONNECTIVITY_M2_ACTIVE && out.active_source==AUDIO_SOURCE_BLUETOOTH);
    out=run(&c,20,1,0,1,1);
    assert(out.mode==CONNECTIVITY_M4_FAULT && out.fault==CONNECTIVITY_FAULT_BT_LINK_LOSS && out.muted);
    out=run(&c,30,1,0,1,1); /* recovery candidate telecoil */
    assert(out.mode==CONNECTIVITY_M4_FAULT);
    out=run(&c,30+cfg.recovery_stable_ms,1,0,1,1);
    assert(out.mode==CONNECTIVITY_M2_ACTIVE && out.active_source==AUDIO_SOURCE_TELECOIL);
    out=run(&c,230,1,1,1,1);
    assert(out.active_source==AUDIO_SOURCE_BLUETOOTH);
    /* intentional disable is not a fault */
    assert(Connectivity_SetEnabled(&c,0,1,240)==CONNECTIVITY_OK);
    out=run(&c,241,1,1,1,1);
    assert(out.mode==CONNECTIVITY_M2_ACTIVE && out.active_source==AUDIO_SOURCE_TELECOIL);
    /* all input lost -> mute and FAULT; after configured timeout -> IDLE */
    out=run(&c,300,1,0,0,0);
    assert(out.mode==CONNECTIVITY_M4_FAULT && out.muted);
    out=run(&c,300+cfg.fault_to_idle_ms+1,1,0,0,0);
    assert(out.mode==CONNECTIVITY_M3_IDLE && out.active_source==AUDIO_SOURCE_NONE && out.request_low_power);
    /* out-of-order service timestamps rejected without advancing state */
    ConnectivityInputs_t stale={0}; stale.now_ms=50;
    assert(Connectivity_Update(&c,&stale,&out)==CONNECTIVITY_ERR_TIME_BACKWARDS);
    /* timer wraparound and stable recovery */
    assert(Connectivity_Init(&c,&cfg,UINT32_MAX-100)==CONNECTIVITY_OK);
    out=run(&c,20,1,0,1,0);
    assert(out.mode==CONNECTIVITY_M2_ACTIVE && out.active_source==AUDIO_SOURCE_TELECOIL);
    printf("CONNECTIVITY FSM: BT precedence, loss, mute, stable recovery, disable, idle, stale time, wrap PASS\n");
    return 0;
}
