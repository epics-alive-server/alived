/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef LOGGING_H
#define LOGGING_H 1

#include "alived.h"



void log_init(char *name);
void log_write(  char *fmt, ...);
void log_error_write( int errnum, char *message);

void debug_init(char *name);
void debug_write(  char *fmt, ...);
void debug_error_write( int errnum, char *message);

int event_write( char *ioc_name, uint32_t timestamp, uint32_t address,
                 uint32_t message, uint8_t event);
int event_file_remove( char *ioc_name);
int event_file_send( char *ioc_name, int socket);

#endif

