/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "config_parse.h"

int main( int argc, char *argv[])
{
  char *event_strings[] = 
    { "None", "Fail", "Boot", "Recover", "Message", "Conflict_Start", 
      "Conflict_Stop", "Daemon_Start" };

  union
  {
    uint32_t raw_ipaddr;
    unsigned char ipaddr[4];
  } address;

  time_t current_time;
  struct tm *ct;
  char timestring[256];

  uint32_t data[4];
  FILE *fptr;

  char *dir, *name;

  char *p;
  
  if( argc != 2)
    {
      printf("event_dump <IOC name>\n");
      return 0;
    }

  if( config_find( "event_dir", &dir) )
    return 1;
  
  if( (strchr( argv[1], '/') == NULL))
    {
      name = malloc( strlen(argv[1]) + strlen("/") + strlen( dir) + 1);
      p = stpcpy( name, dir);
      p = stpcpy( p, "/");
      strcpy( p, argv[1]);
    }
  else
    name = strdup( argv[1]);

  if( (fptr = fopen( name, "r")) == NULL)
    {
      printf("Can't open file \"%s\".\n", name);
      return 1;
    }

  while( fread( data, sizeof(uint32_t), 4, fptr) == 4)
    {
      /* if( (data[0] < 1498595029) || (data[0] > 1498595044) ) */
      /*   continue; */
      /* if( data[3] != 2) */
      /*   continue; */

      current_time = (time_t) data[0];
      ct = localtime( &current_time);
      strftime( timestring, 255, "%Y-%m-%d %H:%M:%S", ct);
          
      address.raw_ipaddr = data[1];

      printf("  %-14s - %d - %s (%d) - %d.%d.%d.%d\n",
             event_strings[data[3]], data[2], timestring, data[0], 
             address.ipaddr[0], address.ipaddr[1], address.ipaddr[2], 
             address.ipaddr[3] );
    }

  fclose( fptr);
  

  return 0;
}

