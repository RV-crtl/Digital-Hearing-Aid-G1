/* Portable control decoder contract: valid frames, bounds and no side-effects on errors. */
#include "control_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void accepts(const char *line, ConnectivityCommand_t want) {
    ConnectivityCommand_t got=CONNECTIVITY_COMMAND_DISABLE_BOTH;
    assert(ControlProtocol_Parse(line,strlen(line),&got)==CONTROL_PROTOCOL_OK);
    assert(got==want);
}
static void rejects(const char *line) {
    ConnectivityCommand_t got=CONNECTIVITY_COMMAND_ENABLE_BOTH;
    assert(ControlProtocol_Parse(line,strlen(line),&got)==CONTROL_PROTOCOL_ERR_FORMAT);
    assert(got==CONNECTIVITY_COMMAND_ENABLE_BOTH);
}
int main(void) {
    const struct { const char *text; ConnectivityCommand_t want; } cases[]={
        {"BT ON",CONNECTIVITY_COMMAND_ENABLE_BLUETOOTH},
        {"bt off",CONNECTIVITY_COMMAND_DISABLE_BLUETOOTH},
        {"TC ON",CONNECTIVITY_COMMAND_ENABLE_TELECOIL},
        {"tc off",CONNECTIVITY_COMMAND_DISABLE_TELECOIL},
        {" ALL ON\r\n",CONNECTIVITY_COMMAND_ENABLE_BOTH},
        {"all off",CONNECTIVITY_COMMAND_DISABLE_BOTH},
        {"MODE AUTO",CONNECTIVITY_COMMAND_ENABLE_BOTH},
        {"mode bt",CONNECTIVITY_COMMAND_BLUETOOTH_ONLY},
        {"MODE TC",CONNECTIVITY_COMMAND_TELECOIL_ONLY},
        {"mode off",CONNECTIVITY_COMMAND_DISABLE_BOTH}
    };
    for (size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i)
        accepts(cases[i].text,cases[i].want);

    ConnectivityCommand_t sentinel=CONNECTIVITY_COMMAND_BLUETOOTH_ONLY;
    assert(ControlProtocol_Parse(" STATUS \r\n",10U,&sentinel)==CONTROL_PROTOCOL_STATUS_REQUEST);
    assert(sentinel==CONNECTIVITY_COMMAND_BLUETOOTH_ONLY);
    assert(ControlProtocol_Parse("status",6U,NULL)==CONTROL_PROTOCOL_STATUS_REQUEST);
    assert(ControlProtocol_Parse(NULL,3U,&sentinel)==CONTROL_PROTOCOL_ERR_ARGUMENT);
    assert(ControlProtocol_Parse("BT ON",0U,&sentinel)==CONTROL_PROTOCOL_ERR_ARGUMENT);
    assert(ControlProtocol_Parse("BT ON",5U,NULL)==CONTROL_PROTOCOL_ERR_ARGUMENT);
    assert(sentinel==CONNECTIVITY_COMMAND_BLUETOOTH_ONLY);

    const char bounded[]={'b','t',' ','o','n','X'};
    sentinel=CONNECTIVITY_COMMAND_DISABLE_BOTH;
    assert(ControlProtocol_Parse(bounded,5U,&sentinel)==CONTROL_PROTOCOL_OK);
    assert(sentinel==CONNECTIVITY_COMMAND_ENABLE_BLUETOOTH);
    assert(ControlProtocol_Parse(bounded,6U,&sentinel)==CONTROL_PROTOCOL_ERR_FORMAT);
    assert(sentinel==CONNECTIVITY_COMMAND_ENABLE_BLUETOOTH);
    assert(ControlProtocol_Parse("",0U,&sentinel)==CONTROL_PROTOCOL_ERR_ARGUMENT);
    /* Blank frames are a format error rather than a valid command. */
    rejects("  ");
    rejects("BT");
    rejects("BT 2");
    rejects("STATUS NOW");
    rejects("BT ON NOW");
    rejects("MODE MUSIC");
    rejects("ALL ENABLE");
    rejects("UNKNOWN ON");
    rejects("BT\0ON"); /* strlen() bounds this to the invalid first token. */
    puts("CONTROL PROTOCOL: accepted aliases, frame bounds, errors PASS");
    return 0;
}
