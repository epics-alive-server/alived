/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef NOTIFYDB_H
#define NOTIFYDB_H 1


#include "alived.h"

/////////////////////////

#include "iocdb.h"

int notifydb_start(void);  // starts daemon service
void notifydb_report_event( char *ioc_name, struct iocinfo_ping ping,
                            struct iocinfo_env *env, int event_type, 
                            uint32_t currtime);

int notifydb_stop(void);

void notifyds_send_subscribers_control( int socket);

#endif
