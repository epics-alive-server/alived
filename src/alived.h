/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/


#ifndef ALIVED_H
#define ALIVED_H 1

#include <netinet/in.h>

//////////////////


struct alived_config
{
  uint16_t heartbeat_udp_port;
  uint16_t database_tcp_port;
  uint16_t subscription_udp_port;

  uint8_t fail_number_heartbeats;

  uint16_t fail_check_period;
  uint32_t instance_retain_time;

  char *log_file;
  char *event_file;
  char *info_file;

  char *control_socket;

  char *event_dir;
  char *state_dir;
};

///////////////////////////


enum events { NONE, FAIL, BOOT, RECOVER, MESSAGE, CONFLICT_START, 
              CONFLICT_STOP, DAEMON_START };


#define API_PROCOTOL_VERSION (4)
#define SUBSCRIPTION_PROCOTOL_VERSION (1)

#endif

