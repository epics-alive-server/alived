/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/

#define ALIVEDB_VERSION "0.1.0"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "alive_client.h"


void time_string( uint32_t timeval, char *buffer)
{
  unsigned int temp;
  int days, hours, mins, secs;

  temp = timeval;
  secs = temp%60;
  temp /= 60;
  mins = temp%60;
  temp /= 60;
  hours = temp%24;
  temp /= 24;
  days = temp;

  if( days)
    sprintf(buffer, "%d day%s, %d hr", days, (days == 1) ? "" : "s", hours);
  else if( hours)
    sprintf(buffer, "%d hr, %d min", hours, mins);
  else if( mins)
    sprintf(buffer, "%d min, %d sec", mins, secs);
  else sprintf(buffer, "%d sec", secs);
}


void helper( void)
{
  printf("Usage: alivedb [-h] [-r (server)[:(port)] ] [-s | -e (var) | -p (param)]\n"
         "       ( . | (ioc1) [ioc2] [...] | (-l|-d|-c) (ioc) )\n");
  printf("Prints out information from alive database.\n"
         "To print entire database, give \'.\' as an argument.\n");
  printf("  -h  Show this help screen.\n");
  printf("  -v  Show version.\n");
  printf("  -r  Set the remote host and optionally the port.\n");
  printf("  -l  Print out event list for the specified IOC.\n");
  printf("  -d  Print out debug information for the specified IOC.\n");
  printf("  -c  Print out conflict information for the specified IOC.\n");
  printf("  -s  Print out only status information.\n");
  printf("  -e  Print out only the environment variable specified.\n");
  printf("  -p  Print out only the operating system specific parameter specified.\n"
         "      Parameter is of form os:parameter\n"
         "        vxworks: boot_device, unit_number, processor_number, boot_host_name,\n"
         "          boot_file, address, backplane_address, boot_host_address,\n"
         "          gateway_address, flags, target_name, startup_script, other\n"
         "        linux: user, group, hostname\n"
         "        darwin: user, group, hostname\n"
         "        windows: user, machine\n");
}

void print_env( struct alive_env *env, int style)
{
  int i;

  char prefix[3] = "  ";

  if( !style)
    prefix[0] = '\0';

  if( env == NULL)
    printf("%sNo Environment Variables recorded.\n", prefix);
  else
    {
      if(!style)
        printf("\n");
      printf("%sEnvironment Variables\n", prefix);
      for( i = 0; i < env->number_envvar; i++)
        printf( "%s  %s = %s\n", prefix, env->envvar_key[i], 
                env->envvar_value[i]);
              
      switch(env->extra_type)
        {
        case VXWORKS:
          {
            struct alive_iocinfo_extra_vxworks *vw;
          
            vw = env->extra;

            if(!style)
              printf("\n");
            printf("%svxWorks Boot Parameters\n", prefix);

            printf("%s  boot device = %s\n", prefix, vw->bootdev);
            printf("%s  unit number = %d\n", prefix, vw->unitnum);
            printf("%s  processor number = %d\n", prefix, vw->procnum);
            printf("%s  boot host name = %s\n", prefix, vw->boothost_name);
            printf("%s  boot file = %s\n", prefix, vw->bootfile);
            if( vw->address != NULL)
              printf("%s  IP address = %s\n", prefix, vw->address);
            if( vw->backplane_address != NULL)
              printf("%s  backplane IP address = %s\n", prefix, 
                     vw->backplane_address );
            if( vw->boothost_address != NULL)
              printf("%s  boot host IP address = %s\n", prefix, 
                     vw->boothost_address);
            if( vw->gateway_address != NULL)
              printf("%s  gateway IP address = %s\n", prefix, 
                     vw->gateway_address);
            printf("%s  flags = %d\n", prefix, vw->flags );
            if( vw->target_name != NULL)
              printf("%s  target name = %s\n", prefix, vw->target_name);
            if( vw->startup_script != NULL)
              printf("%s  startup script = %s\n", prefix, vw->startup_script);
            if( vw->other != NULL)
              printf("%s  other = %s\n", prefix, vw->other);
          }
          break;
        case LINUX:
          {
            struct alive_iocinfo_extra_linux *lnx;

            lnx = env->extra;

            if(!style)
              printf("\n");
            printf("%sLinux Parameters\n", prefix);

            printf("%s  user = %s\n", prefix, lnx->user);
            printf("%s  group = %s\n", prefix, lnx->group);
            printf("%s  hostname = %s\n", prefix, lnx->hostname);
          }
          break;
        case DARWIN:
          {
            struct alive_iocinfo_extra_darwin *dwn;

            dwn = env->extra;

            if(!style)
              printf("\n");
            printf("%sDarwin Parameters\n", prefix);

            printf("%s  user = %s\n", prefix, dwn->user);
            printf("%s  group = %s\n", prefix, dwn->group);
            printf("%s  hostname = %s\n", prefix, dwn->hostname);
          }
          break;
        case WINDOWS:
          {
            struct alive_iocinfo_extra_windows *win;

            win = env->extra;

            if(!style)
              printf("\n");
            printf("%sWindows Parameters\n", prefix);

            printf("%s  user = %s\n", prefix, win->user);
            printf("%s  machine = %s\n", prefix, win->machine);
          }
          break;
        }
    }
}


void print_events( char *server, int port, char *iocname)
{
  struct alive_ioc_event_db *events;
  struct alive_ioc_event_item *item;

  time_t current_time;
  struct tm *ct;
  char timestring[256];

  int i;

  char *event_strings[] = 
    { "None          ", "Fail          ", "Boot          ", 
      "Recover       ", "Message       ", "Conflict_Start", 
      "Conflict_Stop "};
  
  events = alive_get_ioc_event_db( server, port, iocname);
  if( (events != NULL) && (events->number) )
    {
      printf("\n%s Events\n", iocname);

      item = events->instances;
      for( i = 0; i < events->number; i++)
        {
          current_time = (time_t) item->time;
          ct = localtime( &current_time);
          strftime( timestring, 255, "%Y-%m-%d %H:%M:%S", ct);
          
          printf("  %s [%d] %s (%d.%d.%d.%d) - ", 
                 event_strings[item->event], item->user_msg,
                 timestring, item->ip_address[0], item->ip_address[1],
                 item->ip_address[2], item->ip_address[3] );

          time_string( events->current_time - item->time, timestring);
          printf("%s ago\n", timestring);

          item++;
        }
    }
  alive_free_ioc_event_db( events);
}


void print_parameter( struct alive_env *env, char *os, char *par)
{
  if( env == NULL)
    return;

  //  printf("(%s) (%s)\n", os, par);

  if( !strcasecmp( os, "vxworks") )
    {
      struct alive_iocinfo_extra_vxworks *vw;

      if( env->extra_type != VXWORKS)
        return;
          
      vw = env->extra;

      if( !strcasecmp( "boot_device", par) )
        printf("%s\n", vw->bootdev);
      else if( !strcasecmp( "unit_number", par) )
        printf("%d\n", vw->unitnum);
      else if( !strcasecmp( "processor_number", par) )
        printf("%d\n", vw->procnum);
      else if( !strcasecmp( "boot_host_name", par) )
        printf("%s\n", vw->boothost_name);
      else if( !strcasecmp( "boot_file", par) )
        printf("%s\n", vw->bootfile);
      else if( !strcasecmp( "address", par) )
        {
          if( vw->address != NULL)
            printf("%s\n", vw->address);
        }
      else if( !strcasecmp( "backplane_address", par) )
        {
          if( vw->backplane_address != NULL)
            printf("%s\n", vw->backplane_address );
        }
      else if( !strcasecmp( "boot_host_address", par) )
        {
          if( vw->boothost_address != NULL)
            printf("%s\n", vw->boothost_address);
        }
      else if( !strcasecmp( "gateway_address", par) )
        {
          if( vw->gateway_address != NULL)
            printf("%s\n", vw->gateway_address);
        }
      else if( !strcasecmp( "flags", par) )
        printf("%d\n", vw->flags );
      else if( !strcasecmp( "target_name", par) )
        {
          if( vw->target_name != NULL)
            printf("%s\n", vw->target_name);
        }
      else if( !strcasecmp( "startup_script", par) )
        {
          if( vw->startup_script != NULL)
            printf("%s\n", vw->startup_script);
        }
      else if( !strcasecmp( "other", par) )
        {
          if( vw->other != NULL)
            printf("%s\n", vw->other);
        }
    }
  else if( !strcasecmp( os, "linux") )
    {
      struct alive_iocinfo_extra_linux *lnx;
      
      if( env->extra_type != LINUX)
        return;
      
      lnx = env->extra;

      if( !strcasecmp( "user", par) )
        printf("%s\n", lnx->user);
      else if( !strcasecmp( "group", par) )
        printf("%s\n", lnx->group);
      else if( !strcasecmp( "hostname", par) )
        printf("%s\n", lnx->hostname);
    }
  else if( !strcasecmp( os, "darwin") )
    {
      struct alive_iocinfo_extra_darwin *dwn;
      
      if( env->extra_type != DARWIN)
        return;
      
      dwn = env->extra;

      if( !strcasecmp( "user", par) )
        printf("%s\n", dwn->user);
      else if( !strcasecmp( "group", par) )
        printf("%s\n", dwn->group);
      else if( !strcasecmp( "hostname", par) )
        printf("%s\n", dwn->hostname);
    }
  else if( !strcasecmp( os, "windows") )
    {
      struct alive_iocinfo_extra_windows *win;
      
      if( env->extra_type != WINDOWS)
        return;
      
      win = env->extra;

      if( !strcasecmp( "user", par) )
        printf("%s\n", win->user);
      else if( !strcasecmp( "machine", par) )
        printf("%s\n", win->machine);
    }
}


int main(int argc, char *argv[])
{
  struct alive_db *db;
  struct alive_ioc *ioc;

  // 0 is normal, 1 is event, 2 is debug, 3 is conflict
  int mode_flag = 0;

  char *server;
  int port;

  char *cl_server = NULL;

  int verbosity_flag = 0;

  int i, j;
  char *p;

  int vartype = 0;
  char *varval = NULL;

  char timestring_prefix[32], timestring[256];

  int opt;

  while((opt = getopt( argc, argv, "r:e:p:hldcsv")) != -1)
    {
      switch(opt)
        {
        case 'l':
          mode_flag = 1;
          break;
        case 'd':
          mode_flag = 2;
          break;
        case 'c':
          mode_flag = 3;
          break;

        case 'r':
          cl_server = strdup( optarg);
          break;
        case 's':
          verbosity_flag = 1;
          break;
        case 'h':
          helper();
          exit(0);
        case 'e':
          vartype = 1;
          varval = strdup( optarg);
          break;
        case 'p':
          vartype = 2;
          varval = strdup( optarg);
          break;
        case 'v':
          printf("alivedb %s using libaliveclient %s\n", ALIVEDB_VERSION,
                 alive_client_api_version() );
          return 0;
          break;
        case ':':
          // option normally resides in 'optarg'
          printf("Error: option missing its value!\n");
          return -1;
          break;
        }
    }

  server = alive_default_database( &port);
  if( cl_server != NULL)
    {
      free( server);
      server = cl_server;
      if( (p = strchr(server, ':')) != NULL)
        {
          *p = '\0';
          port = atoi( p+1);
        }
    }
  
  if( mode_flag)
    {
      struct alive_detailed_ioc *dioc;
      struct alive_instance *inst;

      time_t t;
      struct tm *ct;
      
      if((argc - optind) != 1)
        {
          helper();
          return 1;
        }

      switch(mode_flag)
        {
        case 1:
          print_events( server, port, argv[optind]);
          return 0;
          break;
        case 2:
        case 3:
          if( mode_flag == 2)
            dioc = alive_get_debug( server, port, argv[optind]);
          else
            dioc = alive_get_conflicts( server, port, argv[optind]);

          if( dioc == NULL)
            {
              printf("No IOC known as \"%s\".\n", argv[optind]);
              return 0;
            }

          if( dioc->number_instances == 0)
            {
              if( mode_flag == 2)
                printf("No known IOC instances for \"%s\".\n", argv[optind]);
              else
                printf("No known IOC conflict for \"%s\".\n", argv[optind]);
            }
      
          inst = dioc->instances;
          for( i = 0; i < dioc->number_instances; i++)
            {
              if( mode_flag == 2)
                printf("%s Instance #%d\n", dioc->ioc_name, i+1);
              else
                printf("%s Conflict #%d\n", dioc->ioc_name, i+1);

              switch( inst->status)
                {
                case INSTANCE_STATUS_UP:
                  printf("  Status = UP\n");
                  break;
                case INSTANCE_STATUS_DOWN:
                  printf("  Status = DOWN\n");
                  break;
                case INSTANCE_STATUS_UNTIMED_DOWN:
                  printf("  Status = UNTIMED_DOWN\n");
                  break;
                case INSTANCE_STATUS_MAYBE_UP:
                  printf("  Status = MAYBE_UP\n");
                  break;
                case INSTANCE_STATUS_MAYBE_DOWN:
                  printf("  Status = MAYBE_DOWN\n");
                  break;
                }
              printf("  Address and Port = %d.%d.%d.%d:%d\n",
                     inst->ip_address[0], inst->ip_address[1], 
                     inst->ip_address[2], inst->ip_address[3], 
                     inst->origin_port);
              printf("  Incarnation = %d, Period = %d, Heartbeat = %d\n", 
                     inst->incarnation, inst->period, inst->heartbeat);

              t = (time_t) inst->boottime;
              ct = localtime( &t);
              strftime( timestring, 255, "%Y-%m-%d %H:%M:%S", ct);
              printf("  Boot Time = %s\n", timestring);
  
              t = (time_t) inst->timestamp;
              ct = localtime( &t);
              strftime( timestring, 255, "%Y-%m-%d %H:%M:%S", ct);
              printf("  Ping Timestamp = %s\n", timestring);
  
              printf("  Reply Port = %d, User Message = %d\n", 
                     inst->reply_port, inst->user_msg);

              print_env( inst->environment, 1);
              printf("\n");
              
              inst++;
            }
          return 0;
          break;
        }
    }

  if( (argc - optind) == 0)
    {
      helper();
      return 0;
    }
  else if( ((argc - optind) == 1) && !strcmp( argv[optind], ".") )
    db = alive_get_db( server, port);
  else if( (argc - optind) > 1)
    db = alive_get_iocs( server, port, argc - optind, &(argv[optind]) );
  else  // (argc - optind) == 1
    db = alive_get_ioc( server, port, argv[optind]);
  if( db == NULL)
    // error written in library
    return 1;

  for( i = 0; i < db->number_ioc; i++)
    {
      ioc = &db->ioc[i];

      if( vartype == 0)
        {
          switch( ioc->status)
            {
            case STATUS_UNKNOWN:
              strcpy( timestring_prefix, "Uncertain");
              timestring[0] = '\0';
              break;
            case STATUS_DOWN_UNKNOWN:
              strcpy( timestring_prefix, "Down time: > ");
              time_string( db->current_time - db->start_time, timestring);
              break;
            case STATUS_DOWN:
              strcpy( timestring_prefix, "Down time: ");
              time_string( db->current_time - ioc->time_value, timestring);
              break;
            case STATUS_UP:
              strcpy( timestring_prefix, "Up time: ");
              time_string( db->current_time - ioc->time_value, timestring);
              break;
            case STATUS_CONFLICT:
              strcpy( timestring_prefix, "Conflict time: ~ ");
              time_string( db->current_time - ioc->time_value, timestring);
              break;
            }

          printf("%s (%d.%d.%d.%d) %d - %s%s\n",
                 ioc->ioc_name, ioc->ip_address[0], ioc->ip_address[1], 
                 ioc->ip_address[2], ioc->ip_address[3], ioc->user_msg,
                 timestring_prefix, timestring );

          if( verbosity_flag)
            continue;

          print_env( ioc->environment, 0);
          
          if( i < (db->number_ioc - 1) )
            printf("\n\n");
        }
      else
        {
          if( vartype == 1)
            {
              if( ioc->environment == NULL)
                printf("No Environment Variables recorded.\n");
              else
                {
                  for( j = 0; j < ioc->environment->number_envvar; j++)
                    if( !strcmp( varval, ioc->environment->envvar_key[j]) )
                      {
                        printf( "%s\n", ioc->environment->envvar_value[j]);
                        break;
                      }
                }
            }
          else
            {
              char *os, *par;

              os = strdup(varval);
              par = strchr( os, ':');
              if( par != NULL)
                {
                  *par = '\0';
                  par++;
                  print_parameter( ioc->environment, os, par);
                }
            }
        }        
    }
  alive_free_db( db);
  
  return 0;
}

