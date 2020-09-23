/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "iocdb.h"
#include "iocdb_access.h"
#include "utility.h"
#include "gentypes.h"

void iocdb_make_netbuffer_env( struct netbuffer_struct *nbuff,
                               struct iocinfo_env *env)
{
  int i;

  netbuffer_add_uint8( nbuff, env == NULL ? 0 : 1 );
  if( env == NULL)
    return;
  
  netbuffer_add_uint16( nbuff, env->count);
  for( i = 0; i < env->count; i++)
    {
      netbuffer_string_write( 1, nbuff, env->key[i]);
      netbuffer_string_write( 2, nbuff, env->value[i]);
    }

  netbuffer_add_uint16( nbuff, env->extra_type);
  switch(env->extra_type)
    {
    case VXWORKS: // vxworks
      {
        struct iocinfo_extra_vxworks *vw;

        vw = env->extra;
        netbuffer_string_write( 1, nbuff, vw->bootdev);
        netbuffer_add_uint32( nbuff, vw->unitnum);
        netbuffer_add_uint32( nbuff, vw->procnum);
        netbuffer_string_write( 1, nbuff, vw->boothost_name);
        netbuffer_string_write( 1, nbuff, vw->bootfile);
        netbuffer_string_write( 1, nbuff, vw->address);
        netbuffer_string_write( 1, nbuff, vw->backplane_address);
        netbuffer_string_write( 1, nbuff, vw->boothost_address);
        netbuffer_string_write( 1, nbuff, vw->gateway_address);
        netbuffer_add_uint32( nbuff, vw->flags);
        netbuffer_string_write( 1, nbuff, vw->target_name);
        netbuffer_string_write( 1, nbuff, vw->startup_script);
        netbuffer_string_write( 1, nbuff, vw->other);
      }
      break;
    case LINUX:
      {
        struct iocinfo_extra_linux *lnx;

        lnx = env->extra;
        netbuffer_string_write( 1, nbuff, lnx->user);
        netbuffer_string_write( 1, nbuff, lnx->group);
        netbuffer_string_write( 1, nbuff, lnx->hostname);
      }
      break;
    case DARWIN:
      {
        struct iocinfo_extra_darwin *dar;

        dar = env->extra;
        netbuffer_string_write( 1, nbuff, dar->user);
        netbuffer_string_write( 1, nbuff, dar->group);
        netbuffer_string_write( 1, nbuff, dar->hostname);
      }
      break;
    case WINDOWS:
      {
        struct iocinfo_extra_windows *win;

        win = env->extra;
        netbuffer_string_write( 1, nbuff, win->user);
        netbuffer_string_write( 1, nbuff, win->machine);
      }
      break;
    }
      
}

static void ioclist_send_and_clean( int socket,
                                    struct access_info_db_struct *aids)
{
  struct access_info_struct *ais;

  struct netbuffer_struct nbuff;
  
  int i;
  
  netbuffer_init( &nbuff, 256);
  
  netbuffer_add_uint16( &nbuff, aids->number);
  for( i = 0; i < aids->number; i++)
    {
      ais = &(aids->infos[i]);

      // write ioc name
      netbuffer_string_write( 1, &nbuff, ais->ioc_name);
      netbuffer_add_uint8( &nbuff, ais->overall_status);
      netbuffer_add_uint32( &nbuff, ais->time_value);
      netbuffer_add_uint32( &nbuff, ais->ping.ip_address.s_addr);
      netbuffer_add_uint32( &nbuff, ais->ping.user_msg);

      iocdb_make_netbuffer_env( &nbuff, ais->env);
    }

  socket_writer( socket, netbuffer_data(&nbuff), netbuffer_size(&nbuff));
  netbuffer_deinit( &nbuff);
  
  iocdb_info_release(aids);
}

void iocdb_socket_send_all( int socket)
{
  if( iocdb_missing())
    return;

  ioclist_send_and_clean( socket, iocdb_info_get_all() );
}

void iocdb_socket_send_multi( int socket, int number, char **ioc_names)
{
  if( iocdb_missing())
    return;

  ioclist_send_and_clean( socket, iocdb_info_get_multi(number, ioc_names));
}


void iocdb_socket_send_single( int socket, char *ioc_name)
{
  if( iocdb_missing())
    return;

  ioclist_send_and_clean( socket, iocdb_info_get_single(ioc_name));
}



///////////////////////////////////////


void iocdb_socket_send_control_list( int socket)
{
  struct access_names_db_struct *ands;

  char *name;
  int i;
  
  if( iocdb_missing())
    return;

  ands = iocdb_names_get();
  for( i = 0 ; i < ands->number; i++)
    {
      name = ands->names[i];
      send( socket, name, strlen(name), 0);
      send( socket, "\n", 1, 0);
    }
  iocdb_names_release(ands);
}




static void send_ioc_control( struct access_info_struct *ais, int socket)
{
  struct iocinfo_ping ping;

  char buffer[1024];
  char addr_str[16];

  char *p;
  int cnt;
  
  void time_send( char *tag, uint32_t timeval)
  {
    char time_str[32];
    // using socket and buffer from above
    
    cnt = snprintf( buffer, 1024, "%s = %d [%s]\n", tag, timeval,
              time_to_string( time_str, timeval) );
    buffer[1023] = '\0';  // not really needed
    send( socket, buffer, cnt, 0);
  }

  void status_time_send( uint8_t status, uint32_t timeval)
  {
    char buffer[128];
    char *p;
    int cnt = 0;

    buffer[0] = '\0';
    
    switch(status)
      {
      case STATUS_UNKNOWN:
        p = stpcpy( buffer, "overall status = UNKNOWN ");
        break;
      case STATUS_DOWN_UNKNOWN:
        p = stpcpy( buffer, "overall status = DOWN_UNKNOWN ");
        break;
      case STATUS_DOWN:
        p = stpcpy( buffer, "overall status = DOWN ");
        break;
      case STATUS_UP:
        p = stpcpy( buffer, "overall status = UP ");
        break;
      case STATUS_CONFLICT:
        p = stpcpy( buffer, "overall status = CONFLICT ");
        break;
      default:
        p = buffer; // just do empty string
      }

    if( (status == STATUS_UP) || (status == STATUS_DOWN) ||
        (status == STATUS_CONFLICT) )
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
          cnt = sprintf(p, "(%d day%s, %d hr)", days,
                        (days == 1) ? "" : "s", hours);
        else if( hours)
          cnt = sprintf(p, "(%d hr, %d min)", hours, mins);
        else if( mins)
          cnt = sprintf(p, "(%d min, %d sec)", mins, secs);
        else
          cnt = sprintf(p, "%d sec", secs);
      }
    p = stpcpy(&buffer[cnt], "\n");
    send( socket, buffer, p - buffer, 0);
  }


  // just in case
  buffer[1023] = '\0';

  
  cnt = snprintf( buffer, 1023, "ioc = %s\n", ais->ioc_name);
  send( socket, buffer, cnt, 0);

  status_time_send( ais->overall_status, ais->time_value);
  ping = ais->ping;

  cnt = snprintf( buffer, 1023,
                  "IP address = %s\n"
                  "origin port = %d\n"
                  "heartbeat = %d\n"
                  "period = %d\n"
                  "protocol version = %d4\n",
                  address_to_string( addr_str, ping.ip_address.s_addr),
                  ping.origin_port, ping.heartbeat, ping.period, 
                  ping.protocol_version);
  send( socket, buffer, cnt, 0);

  time_send( "incarnation", ping.incarnation);
  time_send( "boot time", ping.boottime);
  time_send( "ping time", ping.timestamp);

  cnt = snprintf( buffer, 1023,
                  "reply port = %d\n"
                  "user message = %d\n", 
                  ping.reply_port, ping.user_msg);
  send( socket, buffer, cnt, 0);

  if( ais->env != NULL)
    {
      struct iocinfo_env *env;
      int i;

      env = ais->env;

      cnt = snprintf( buffer, 1023, "\nenvironment variables = %d\n",
                      env->count);
      send( socket, buffer, cnt, 0);

      for( i = 0; i < env->count; i++)
        {
          cnt = snprintf( buffer, 1023, "  %s = %s\n", env->key[i],
                          env->value[i]);
          send( socket, buffer, cnt,  0);
        }
      switch(env->extra_type)
        {
        case GENERIC:
          p = stpcpy( buffer, "\nIOC type = GENERIC\n");
          send( socket, buffer, p - buffer, 0);
          break;
        case VXWORKS: // vxworks
          {
            struct iocinfo_extra_vxworks *vw;

            vw = env->extra;
            buffer[1023] = '\0';
            cnt = snprintf( buffer, 1023,
                            "\nIOC type = VXWORKS\n"
                            "  boot device = %s\n"
                            "  unit number = %d\n"
                            "  processor number = %d"
                            "  boot host name = %s\n"
                            "  boot file = %s\n"
                            "  IP address = %s\n"
                            "  backplane IP address = %s\n"
                            "  boot host IP address = %s\n"
                            "  gateway IP address = %s\n"
                            "  flags = 0x%X\n"
                            "  target name = %s\n"
                            "  startup script = %s\n"
                            "  other = %s\n",
                            vw->bootdev, vw->unitnum, vw->procnum,
                            vw->boothost_name, vw->bootfile, vw->address,
                            vw->backplane_address, vw->boothost_address,
                            vw->gateway_address, vw->flags,
                            vw->target_name, vw->startup_script, vw->other);
            send( socket, buffer, cnt, 0);
          }
          break;
        case LINUX:
          {
            struct iocinfo_extra_linux *lnx;
            
            lnx = env->extra;
            buffer[1023] = '\0';
            cnt = snprintf( buffer, 1023,
                            "\nIOC type = LINUX\n"
                            "  user = %s\n"
                            "  group = %s\n"
                            "  hostname = %s\n",
                            lnx->user, lnx->group, lnx->hostname);
            send( socket, buffer, cnt, 0);
          }
          break;
        case DARWIN:
          {
            struct iocinfo_extra_darwin *dar;
            
            dar = env->extra;
            buffer[1023] = '\0';
            cnt = snprintf( buffer, 1023,
                            "\nIOC type = DARWIN\n"
                            "  user = %s\n"
                            "  group = %s\n"
                            "  hostname = %s\n",
                            dar->user, dar->group, dar->hostname);
            send( socket, buffer, cnt, 0);
          }
          break;
        case WINDOWS:
          {
            struct iocinfo_extra_windows *win;
            
            win = env->extra;
            buffer[1023] = '\0';
            cnt = snprintf( buffer, 1023,
                            "\nIOC type = WINDOWS\n"
                            "  user = %s\n"
                            "  machine = %s\n",
                            win->user, win->machine);
            send( socket, buffer, cnt, 0);
          }
          break;
        }
    }
}


int iocdb_socket_send_control_ioc( int socket, char *ioc_name)
{
  struct access_info_db_struct *aids;
  int returner;

  int i;
  
 
  if( iocdb_missing())
    return 0;

  // set up to return multiple, even though it asks for a single
  aids = iocdb_info_get_single(ioc_name);
  for( i = 0; i < aids->number; i++)
    send_ioc_control( &(aids->infos[i]), socket);

  returner = aids->number;
  iocdb_info_release(aids);

  return returner;
}


/////////////////////////////////////

static void send_detail_socket(struct access_detail_struct *ads, int socket)
{
  struct access_instance_struct *ais;
  struct iocinfo_ping ping;

  struct netbuffer_struct nbuff;
  
  int i;
  
  netbuffer_init( &nbuff, 256);
 
  // write ioc name
  netbuffer_string_write( 1, &nbuff, ads->ioc_name);
  netbuffer_add_uint8( &nbuff, ads->overall_status);
  netbuffer_add_uint32( &nbuff, ads->time_value);
  netbuffer_add_uint32( &nbuff, ads->instance_count);

  ais = ads->instances;
  for( i = 0; i < ads->instance_count; i++)
    {
      ping = ais->ping;

      netbuffer_add_uint8( &nbuff, ais->status);
      netbuffer_add_uint32( &nbuff, ping.ip_address.s_addr);
      netbuffer_add_uint16( &nbuff, ping.origin_port);
      netbuffer_add_uint32( &nbuff, ping.heartbeat);
      netbuffer_add_uint16( &nbuff, ping.period);
      netbuffer_add_uint32( &nbuff, ping.incarnation);
      netbuffer_add_uint32( &nbuff, ping.boottime);
      netbuffer_add_uint32( &nbuff, ping.timestamp);
      netbuffer_add_uint16( &nbuff, ping.reply_port);
      netbuffer_add_uint32( &nbuff, ping.user_msg);

      iocdb_make_netbuffer_env( &nbuff, ais->env);

      ais++;
    }

  socket_writer( socket, netbuffer_data(&nbuff), netbuffer_size(&nbuff));
  netbuffer_deinit( &nbuff);
}


void iocdb_socket_send_debug( int socket, char *ioc_name)
{
  struct access_detail_db_struct *adds;
  
  if( iocdb_missing())
    return;

  adds = iocdb_get_debug( ioc_name);
  if( adds->number > 0)
    send_detail_socket( &(adds->details[0]), socket);

  iocdb_debug_release(adds);
}


void iocdb_socket_send_conflicts( int socket, char *ioc_name)
{
  struct access_detail_db_struct *adds;
  
  if( iocdb_missing())
    return;

  adds = iocdb_get_conflict( ioc_name);
  if( adds->number > 0)
    send_detail_socket( &(adds->details[0]), socket);

  iocdb_debug_release(adds);
}


////////////////////////////////////


static void snapshot_maker(struct access_info_struct *ais, int which,
                           FILE **fptrs)
{
  struct iocinfo_ping ping;

  char time_str[32];
  char addr_str[16];
  

  fprintf(fptrs[0], "%d;%s;", which, ais->ioc_name);
  switch(ais->overall_status)
    {
    case STATUS_UNKNOWN:
      fprintf(fptrs[0], "unknown;");
      break;
    case STATUS_DOWN_UNKNOWN:
    case STATUS_DOWN:
      fprintf(fptrs[0], "down;");
      break;
    case STATUS_UP:
      fprintf(fptrs[0], "up;");
      break;
    case STATUS_CONFLICT:
      fprintf(fptrs[0], "conflict;");
      break;
    }
  ping = ais->ping;
  fprintf(fptrs[0], "%s;%d;%s;%d;", 
          time_to_string( time_str, ping.timestamp), ping.protocol_version,
          address_to_string( addr_str, ping.ip_address.s_addr),
          ping.origin_port);
  fprintf(fptrs[0], "%s;", time_to_string( time_str, ping.boottime) );
  fprintf(fptrs[0], "%s;", time_to_string( time_str, ping.incarnation) );
  fprintf(fptrs[0], "%d;%d;%d;", ping.reply_port, ping.period, ping.user_msg);
  if( ais->env != NULL)
    {
      switch( ais->env->extra_type)
        {
        case GENERIC:
          fprintf(fptrs[0], "generic");
          break;          
        case VXWORKS:
          fprintf(fptrs[0], "vxworks");
          break;          
        case LINUX:
          fprintf(fptrs[0], "linux");
          break;          
        case DARWIN:
          fprintf(fptrs[0], "darwin");
          break;          
        case WINDOWS:
          fprintf(fptrs[0], "windows");
          break;          
        }
    }
  fprintf(fptrs[0], "\n");

  if( ais->env != NULL)
    {
      struct iocinfo_env *env;
      int i;

      env = ais->env;

      for( i = 0; i < env->count; i++)
        fprintf(fptrs[1], "%d;%s;%s\n", which, env->key[i], env->value[i]);

      switch(env->extra_type)
        {
        case VXWORKS: // vxworks
          {
            struct iocinfo_extra_vxworks *vw;
        
            vw = env->extra;
            fprintf( fptrs[2], 
                     "%d;%s;%d;%d;%s;%s;%s;%s;%s;%s;%s;%s;%d;%s;%s;%s\n",
                     which, vw->bootdev, vw->unitnum, vw->procnum,
                     vw->boothost_name, vw->bootfile, vw->address,
                     vw->backplane_address, vw->boothost_address,
                     vw->gateway_address, vw->boothost_username,
                     vw->boothost_password, vw->flags, vw->target_name, 
                     vw->startup_script, vw->other);
          }
          break;
        case LINUX:
          {
            struct iocinfo_extra_linux *lnx;
        
            lnx = env->extra;
            fprintf( fptrs[3], "%d;%s;%s;%s\n",        
                     which, lnx->user, lnx->group, lnx->hostname);
          }
          break;
        case DARWIN:
          {
            struct iocinfo_extra_darwin *dar;
            
            dar = env->extra;
            fprintf( fptrs[4], "%d;%s;%s;%s\n",        
                     which, dar->user, dar->group, dar->hostname);
          }
          break;
        case WINDOWS:
          {
            struct iocinfo_extra_windows *win;
            
            win = env->extra;
            fprintf( fptrs[5], "%d;%s;%s\n", 
                     which, win->user, win->machine);
          }
          break;
        }
    }
}


// FFFIIIXXX MMMEEE!!!
void iocdb_snapshot_tree( char *prefix)
{
  struct access_info_db_struct *aids;
  
  char buffer[64];
  char *tbuffer;

  // order of 2-5 is same as "enum os_type" from iocdb.h
  char *suffixes[6] = {".csv", "_envvars.csv", "_vxworks.csv", "_linux.csv",
                       "_darwin.csv", "_windows.csv" };
  // 0: all, 1: envvars, 2: vxworks, 3: linux, 4: darwin, 5: windows
  FILE *fptrs[6];

  int i, j;


  if( iocdb_missing())
    return;

  
  // add more than enough for suffixes
  tbuffer = malloc( strlen(prefix) + 32);
  for( i = 0; i < 6; i++)
    {
      sprintf( tbuffer, "%s%s", prefix, suffixes[i]);
      if( (fptrs[i] = fopen( tbuffer, "wt") ) == NULL)
        {
          for( j = 0; j < i; j++)
            fclose(fptrs[j]);
          free(tbuffer);
          return;
        }
    }
  free(tbuffer);

  strcpy( buffer, "# ");
  time_to_string( buffer + 2, time(NULL));
  strcat( buffer, "\n");

  fprintf(fptrs[0], "%s", buffer);
  fprintf(fptrs[0],
          "entry;ioc;status;time;protocol;ipaddress;originport;boottime;"
          "incarnation;replyport;period;usermsg;ioctype\n");
  fprintf(fptrs[1], "entry;variable;value\n");
  fprintf( fptrs[2],
           "entry;bootdev;unitnum;procnum;hostname;bootfile;ipaddress;"
           "backplane_address;boothost_address;gateway_address;user;"
           "password;flags;target_name;startup_script;other\n");
  fprintf( fptrs[3], "entry;user;group;host\n");
  fprintf( fptrs[4], "entry;user;group;host\n");
  fprintf( fptrs[5], "entry;user;machine\n");


  aids = iocdb_info_get_all();
  for( i = 0; i < aids->number; i++)
    snapshot_maker( &(aids->infos[i]), i+1, fptrs);
  iocdb_info_release(aids);

  for( i = 0; i < 6; i++)
    fclose(fptrs[i]);
}




/*


fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

EAGAIN
WOULDBLOCK



 */


