/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>

#include <pthread.h>


#include "alived.h"
#include "logging.h"
#include "utility.h"


// just some config strings
extern struct alived_config config;

/////////////////////////////////////////////////

int log_flag = 1;
pthread_mutex_t log_lock;
char *log_name;


void log_init(char *name)
{
  log_name = strdup(name);
  pthread_mutex_init(&log_lock, NULL);
}

void log_write(  char *fmt, ...)
{
  FILE *fptr;
  va_list args;

  char time_str[32];

  if( !log_flag)
    return;

  pthread_mutex_lock(&log_lock);
  if( (fptr = fopen( log_name, "a")) != NULL)
    {
      va_start(args, fmt);
      fprintf( fptr, "[%s] ", time_to_string( time_str, time(NULL)));
      vfprintf( fptr, fmt, args);
      va_end(args);

      fclose(fptr);
    }
  pthread_mutex_unlock(&log_lock);
}

void log_error_write( int errnum, char *message)
{
  char buffer[256];
  
  if( strerror_r( errnum, buffer, 256) )
    log_write( "%s: **invalid strerror_r call**\n", message);
  else
    log_write( "%s: %s\n", message, buffer);
}


///////////////////////////////////////////////////////


int debug_flag = 1;
pthread_mutex_t debug_lock;
char *debug_name;


void debug_init(char *name)
{
  FILE *fptr;

  debug_name = strdup(name);
  pthread_mutex_init(&debug_lock, NULL);

  // this will zero out existing file
  fptr = fopen( debug_name, "w");
  if( fptr != NULL)
    fclose(fptr);
}

void debug_write(  char *fmt, ...)
{
  FILE *fptr;
  va_list args;

  //  char time_str[32];

  if( !debug_flag)
    return;

  pthread_mutex_lock(&debug_lock);
  if( (fptr = fopen( debug_name, "a")) != NULL)
    {
      va_start(args, fmt);
      //      fprintf( fptr, "[%s] ", time_to_string( time_str, time(NULL)));
      vfprintf( fptr, fmt, args);
      va_end(args);

      fclose(fptr);
    }
  pthread_mutex_unlock(&debug_lock);
}

void debug_error_write( int errnum, char *message)
{
  char buffer[256];
  
  if( strerror_r( errnum, buffer, 256) )
    debug_write( "%s: **invalid strerror_r call**\n", message);
  else
    debug_write( "%s: %s\n", message, buffer);
}



//////////////////////////////////////////////////////

// event log file stuff

int event_write( char *ioc_name, uint32_t timestamp, uint32_t address,
                 uint32_t message, uint8_t event)
{
  FILE *fptr;
  char *filename;

  uint32_t data[4];

  char *event_strings[] = {"NONE", "FAIL", "BOOT", "RECOVER", "MESSAGE", 
                           "CONFLICT_START", "CONFLICT_STOP"};

  char time_str[32];
  char addr_str[16];


  filename = make_file_path( config.event_dir, ioc_name);
  fptr = fopen( filename, "a");
  free( filename);
  if( fptr == NULL)
    {
      log_error_write(errno, "IOC boot file write");
      return -1;
    }

  // done this way to write both at same time
  data[0] = timestamp;
  data[1] = address;
  data[2] = message;
  // really only uses first byte, leaving three bytes for later if needed
  data[3] = event; 
  fwrite( data, sizeof(uint32_t), 4, fptr);
  fclose( fptr);

  if( (fptr = fopen( config.event_file, "a")) == NULL)
    return -1;
  fprintf( fptr, "%s %s %s %s %d\n", time_to_string( time_str, timestamp),
           ioc_name, event_strings[event],
           address_to_string( addr_str, address), message);
  fclose(fptr);

  return 0;
}

int event_file_remove( char *ioc_name)
{
  char *filename;
  int ret;

  filename = make_file_path( config.event_dir, ioc_name);
  ret = unlink( filename);
  free(filename);

  if( ret)
    {
      log_error_write(errno, "IOC event remove");
      return -1;
    }

  return 0;
}


// might have file race here
int event_file_send( char *ioc_name, int socket)
{
FILE *fptr;
  char *filename;

  uint32_t data[1024];  // multiple of 16
  int number;

  filename = make_file_path( config.event_dir, ioc_name);
  fptr = fopen( filename, "r");
  free( filename);
  if( fptr == NULL)
    {
      log_error_write(errno, "IOC boot file read");
      return -1;
    }

  // transfer in 1024 byte chunks
  do
    {
      number = fread( data, sizeof(uint32_t), 1024, fptr);
      socket_writer( socket, data, number*sizeof(uint32_t) );
    }
  while( number == 1024);

  fclose( fptr);

  return 0;
}

//////////////////////////////////
