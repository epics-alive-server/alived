/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/




#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include "alived.h"
#include "llrb_db.h"
#include "iocdb.h"
#include "logging.h"
#include "utility.h"

#include "notifydb.h"

#define MIN_PROTOCOL_VERSION (4)
#define MAX_PROTOCOL_VERSION (5)


// just some config strings
extern struct alived_config config;

struct ping_callback_info
{
  // to callback
  char *ioc_name;
  struct iocinfo_ping ping;
  uint16_t ioc_flags;
  // from callback
  char read_flag;
  uint8_t status;
  int event;
};

struct event_adder_data
{
  int event;
  time_t currtime;
};

struct timeout_data
{
  time_t currtime;
};


/////////////////////////////

// GLOBAL
struct 
{
  struct tree_db *ioc_db;
} db = { NULL};


////////////////////////////////////

// state file stuff

static void *load_callback( void *data)
{
  return data;
}

static int state_files_load( void)
{
  DIR *dp = NULL;
  struct dirent *dptr = NULL;

  char *ioc_name;
  struct iocinfo *ioc;
  struct iocinfo_data *infodata;

  FILE *fptr;
  char *filename;

  uint8_t o8;

  int i;

  if((dp = opendir(config.state_dir)) == NULL)
    {
      log_error_write(errno, "Reading state directory");
      return 1;
    }
  
  while((dptr = readdir(dp)) != NULL)
    {
      if( dptr->d_name[0] == '.')
        continue;

      filename = make_file_path( config.state_dir, dptr->d_name);
      fptr = fopen( filename, "r");
      free( filename);
      if( fptr == NULL)
        {
          log_error_write(errno, "IOC state file read");
          return 1;
        }

      infodata = calloc( 1, sizeof( struct iocinfo_data) );
      if( infodata == NULL)
        {
          fclose( fptr);
          continue;
        }
      infodata->next = NULL;

      fread( &(infodata->status), sizeof(uint8_t), 1, fptr);
      if( (infodata->status == INSTANCE_UP) ||
          (infodata->status == INSTANCE_MAYBE_UP) )
        infodata->status = INSTANCE_MAYBE_UP;
      else // DOWN , UNTIMED_DOWN
        infodata->status = INSTANCE_MAYBE_DOWN;

      fread( &o8, sizeof(uint8_t), 1, fptr); // throw away
      fread( &o8, sizeof(uint8_t), 1, fptr); // throw away
      fread( &o8, sizeof(uint8_t), 1, fptr); // throw away

      ioc_name = file_string_grab( 1, fptr);
      if( ioc_name == NULL)
        {
          fclose( fptr);
          free( infodata);
          continue;
        }

      // no failing out from here

      ioc = calloc( 1, sizeof( struct iocinfo));
      ioc->ioc_name = ioc_name;
      ioc->conflict_flag = 0;
      if( infodata->status == INSTANCE_MAYBE_UP)
        {
          ioc->data_up = infodata;
          ioc->data_down = NULL;
        }
      else
        {
          ioc->data_up = NULL;
          ioc->data_down = infodata;
        }

      fread( &(infodata->ping.period), sizeof(uint16_t), 1, fptr);
      fread( &(infodata->ping.ip_address.s_addr), sizeof(uint32_t), 1, fptr);
      fread( &(infodata->ping.origin_port), sizeof(uint16_t), 1, fptr);
      fread( &(infodata->ping.incarnation), sizeof(uint32_t), 1, fptr);
      fread( &(infodata->ping.boottime), sizeof(uint32_t), 1, fptr);
      fread( &(infodata->ping.reply_port), sizeof(uint16_t), 1, fptr);
      
      // fill out fields
      infodata->ping.heartbeat = 0;
      infodata->ping.timestamp = 0;
      infodata->ping.user_msg = 0;

      fread( &o8, sizeof(uint8_t), 1, fptr); // env flag
      if( !o8)
        infodata->env = NULL;
      else
        {
          struct iocinfo_env *env;

          env = infodata->env = malloc( sizeof( struct iocinfo_env));

          // REF
          sharedint_init( &(env->ref), 1);

          fread( &(env->count), sizeof(uint16_t), 1, fptr);
          env->key = malloc( env->count * sizeof( char *) );
          env->value = malloc( env->count * sizeof( char *) );
          for( i = 0; i < env->count; i++)
            {
              env->key[i] = file_string_grab( 1, fptr);
              env->value[i] = file_string_grab( 2, fptr);
            }

          fread( &(env->extra_type), sizeof(uint16_t), 1, fptr);
          env->extra = NULL;
          switch(env->extra_type)
            {
            case VXWORKS: // vxworks
              {
                struct iocinfo_extra_vxworks *vw;
                vw = env->extra = 
                  malloc( sizeof( struct iocinfo_extra_vxworks));
            
                vw->bootdev = file_string_grab( 1, fptr);
                fread( &(vw->unitnum), sizeof(uint32_t), 1, fptr);
                fread( &(vw->procnum), sizeof(uint32_t), 1, fptr);
                vw->boothost_name = file_string_grab( 1, fptr);
                vw->bootfile = file_string_grab( 1, fptr);
                vw->address = file_string_grab( 1, fptr);
                vw->backplane_address = file_string_grab( 1, fptr);
                vw->boothost_address = file_string_grab( 1, fptr);
                vw->gateway_address = file_string_grab( 1, fptr);
                vw->boothost_username = file_string_grab( 1, fptr);
                vw->boothost_password = file_string_grab( 1, fptr);
                fread( &(vw->flags), sizeof(uint32_t), 1, fptr);
                vw->target_name = file_string_grab( 1, fptr);
                vw->startup_script = file_string_grab( 1, fptr);
                vw->other = file_string_grab( 1, fptr);
              }
              break;
            case LINUX:
              {
                struct iocinfo_extra_linux *lnx;
                lnx = env->extra = 
                  malloc( sizeof( struct iocinfo_extra_linux));
                
                lnx->user = file_string_grab( 1, fptr);
                lnx->group = file_string_grab( 1, fptr);
                lnx->hostname = file_string_grab( 1, fptr);
              }
              break;
            case DARWIN:
              {
                struct iocinfo_extra_darwin *dar;
                dar = env->extra = 
                  malloc( sizeof( struct iocinfo_extra_darwin));
            
                dar->user = file_string_grab( 1, fptr);
                dar->group = file_string_grab( 1, fptr);
                dar->hostname = file_string_grab( 1, fptr);
              }
              break;
            case WINDOWS:
              {
                struct iocinfo_extra_windows *win;
                win = env->extra = 
                  malloc( sizeof( struct iocinfo_extra_windows));
            
                win->user = file_string_grab( 1, fptr);
                win->machine = file_string_grab( 1, fptr);
              }
              break;
            }
        }

      fclose( fptr);

      db_add( db.ioc_db, ioc_name, load_callback, NULL, ioc);

      // add this to a daemon event file that the IOCs could cross-reference
      // daemon_event_write( dptr->d_name, (uint32_t) time(NULL), 0, 0, START);
    }


  closedir( dp);

  return 0;
}


static int state_write( char *ioc_name, uint8_t status)
{
  FILE *fptr;
  char *filename;

  filename = make_file_path( config.state_dir, ioc_name);
  fptr = fopen( filename, "r+");
  free( filename);
  if( fptr == NULL)
    {
      log_error_write(errno, "IOC state write");
      return -1;
    }

  fwrite( &status, sizeof(uint8_t), 1, fptr);

  fclose( fptr);

  return 0;
}

static int state_file_remove( char *ioc_name)
{
  char *filename;
  int ret;

  filename = make_file_path( config.state_dir, ioc_name);
  ret = unlink( filename);
  free(filename);

  if( ret)
    {
      log_error_write(errno, "IOC state remove");
      return -1;
    }

  return 0;
}


static int state_info_write( char *ioc_name, uint8_t status, 
                      struct iocinfo_ping *ping, struct iocinfo_env *env)
{
  FILE *fptr;
  char *filename;

  uint8_t o8 = 0;

  int i;


  filename = make_file_path( config.state_dir, ioc_name);
  fptr = fopen( filename, "w");
  free( filename);
  if( fptr == NULL)
    {
      log_error_write(errno, "IOC state and info write");
      return -1;
    }

  fwrite( &status, sizeof(uint8_t), 1, fptr);
  fwrite( &o8, sizeof(uint8_t), 1, fptr);
  fwrite( &o8, sizeof(uint8_t), 1, fptr);
  fwrite( &o8, sizeof(uint8_t), 1, fptr);

  file_string_write( 1, fptr, ioc_name);
  fwrite( &(ping->period), sizeof(uint16_t), 1, fptr);
  fwrite( &(ping->ip_address.s_addr), sizeof(uint32_t), 1, fptr);
  fwrite( &(ping->origin_port), sizeof(uint16_t), 1, fptr);
  fwrite( &(ping->incarnation), sizeof(uint32_t), 1, fptr);
  fwrite( &(ping->boottime), sizeof(uint32_t), 1, fptr);
  fwrite( &(ping->reply_port), sizeof(uint16_t), 1, fptr);

  if( env == NULL)
    {
      o8 = 0; // no env
      fwrite( &o8, sizeof(uint8_t), 1, fptr);
    }
  else
    {
      o8 = 1; // env
      fwrite( &o8, sizeof(uint8_t), 1, fptr);

      // write environment fields
      fwrite( &(env->count), sizeof(uint16_t), 1, fptr);
      for( i = 0; i < env->count; i++)
        {
          file_string_write( 1, fptr, env->key[i]);
          file_string_write( 2, fptr, env->value[i]);
        }

      fwrite( &(env->extra_type), sizeof(uint16_t), 1, fptr);
      switch(env->extra_type)
        {
        case VXWORKS: // vxworks
          {
            struct iocinfo_extra_vxworks *vw;

            vw = env->extra;
            file_string_write( 1, fptr, vw->bootdev);
            fwrite( &(vw->unitnum), sizeof(uint32_t), 1, fptr);
            fwrite( &(vw->procnum), sizeof(uint32_t), 1, fptr);
            file_string_write( 1, fptr, vw->boothost_name);
            file_string_write( 1, fptr, vw->bootfile);
            file_string_write( 1, fptr, vw->address);
            file_string_write( 1, fptr, vw->backplane_address);
            file_string_write( 1, fptr, vw->boothost_address);
            file_string_write( 1, fptr, vw->gateway_address);
            file_string_write( 1, fptr, vw->boothost_username);
            file_string_write( 1, fptr, vw->boothost_password);
            fwrite( &(vw->flags), sizeof(uint32_t), 1, fptr);
            file_string_write( 1, fptr, vw->target_name);
            file_string_write( 1, fptr, vw->startup_script);
            file_string_write( 1, fptr, vw->other);
          }
          break;
        case LINUX:
          {
            struct iocinfo_extra_linux *lnx;

            lnx = env->extra;
            file_string_write( 1, fptr, lnx->user);
            file_string_write( 1, fptr, lnx->group);
            file_string_write( 1, fptr, lnx->hostname);
          }
          break;
        case DARWIN:
          {
            struct iocinfo_extra_darwin *dar;

            dar = env->extra;
            file_string_write( 1, fptr, dar->user);
            file_string_write( 1, fptr, dar->group);
            file_string_write( 1, fptr, dar->hostname);
          }
          break;
        case WINDOWS:
          {
            struct iocinfo_extra_windows *win;

            win = env->extra;
            file_string_write( 1, fptr, win->user);
            file_string_write( 1, fptr, win->machine);
          }
          break;
        }
    }
  fclose( fptr);

  return 0;
}


///////////////////////////////////

static int event_process( char *ioc_name, uint32_t timestamp, uint8_t event,
                          struct iocinfo_ping iocping, struct iocinfo_env *env)
{
  event_write( ioc_name, timestamp, iocping.ip_address.s_addr,
               iocping.user_msg, event);

  // send data from ping, not the pointer, which might change!!!!!!

  /* NOTIFY */
  notifydb_report_event(ioc_name, iocping, env, event, timestamp);

  return 0;
}


/////////////////////////////

static struct iocinfo_extra_vxworks *parse_vxworks_info( char *string)
{
  struct iocinfo_extra_vxworks *vw;

  // this first case does not need to goto error
  if( (vw = malloc( sizeof( struct iocinfo_extra_vxworks)) ) == NULL)
    return NULL;

  vw->bootdev = NULL;
  vw->unitnum = 0;
  vw->procnum = 0;
  vw->boothost_name = NULL;
  vw->bootfile = NULL;
  vw->address = NULL;
  vw->backplane_address = NULL;
  vw->boothost_address = NULL;
  vw->gateway_address = NULL;
  vw->boothost_username = NULL;
  vw->boothost_password = NULL;
  vw->flags = 0;
  vw->target_name = NULL;
  vw->startup_script = NULL;
  vw->other = NULL;

  vw->bootdev = buffer_string_grab( &string);
  vw->unitnum = ntohl( *((uint32_t *) string) );
  string += 4;
  vw->procnum = ntohl( *((uint32_t *) string) );
  string += 4;
  vw->boothost_name = buffer_string_grab( &string);
  vw->bootfile = buffer_string_grab( &string);
  vw->address = buffer_string_grab( &string);
  vw->backplane_address = buffer_string_grab( &string);
  vw->boothost_address = buffer_string_grab( &string);
  vw->gateway_address = buffer_string_grab( &string);
  vw->boothost_username = buffer_string_grab( &string);
  vw->boothost_password = buffer_string_grab( &string);
  vw->flags = ntohl( *((uint32_t *) string) );
  string += 4;
  vw->target_name = buffer_string_grab( &string);
  vw->startup_script = buffer_string_grab( &string);
  vw->other = buffer_string_grab( &string);

  if( (vw->bootdev == NULL) || (vw->boothost_name == NULL) || 
      (vw->bootfile == NULL) || (vw->address == NULL) || 
      (vw->backplane_address == NULL) || (vw->boothost_address == NULL) || 
      (vw->gateway_address == NULL) || (vw->boothost_username == NULL) || 
      (vw->boothost_password == NULL) || (vw->target_name == NULL) || 
      (vw->startup_script == NULL) || (vw->other == NULL) )
    {
      free(vw->bootdev);
      free(vw->boothost_name);
      free(vw->bootfile);
      free(vw->address);
      free(vw->backplane_address);
      free(vw->boothost_address);
      free(vw->gateway_address);
      free(vw->boothost_username);
      free(vw->boothost_password);
      free(vw->target_name);
      free(vw->startup_script);
      free(vw->other);
      free(vw);

      return NULL;
    }

  return vw;
}



struct iocinfo_env *attach_iocenv(struct iocinfo_env *env)
{
  if( env == NULL)
    return NULL;

  sharedint_alter( &(env->ref), 1);
  return env;
}

static void free_iocenv(struct iocinfo_env *env)
{
  int i;

  if( env == NULL)
    return;

  if( sharedint_alter( &(env->ref), -1) > 0)
    return;

  sharedint_uninit( &(env->ref) );

  for( i = 0; i < env->count; i++)
    {
      free( env->key[i]);
      free( env->value[i]);
    }
  free( env->key);
  free( env->value);
  switch( env->extra_type)
    {
    case VXWORKS:
      {
        struct iocinfo_extra_vxworks *vw;

        vw = (struct iocinfo_extra_vxworks *) env->extra;

        free(vw->bootdev);
        free(vw->boothost_name);
        free(vw->bootfile);
        free(vw->address);
        free(vw->backplane_address);
        free(vw->boothost_address);
        free(vw->gateway_address);
        free(vw->boothost_username);
        free(vw->boothost_password);
        free(vw->target_name);
        free(vw->startup_script);
        free(vw->other);
        free(vw);
      }
      break;
    case LINUX:
      {
        struct iocinfo_extra_linux *lnx;
          
        lnx = (struct iocinfo_extra_linux *) env->extra;
        
        free( lnx->user);
        free( lnx->group);
        free( lnx->hostname);
        free( lnx);
      }
      break;
    case DARWIN:
      {
        struct iocinfo_extra_darwin *dwn;
        
        dwn = (struct iocinfo_extra_darwin *) env->extra;
        
        free( dwn->user);
        free( dwn->group);
        free( dwn->hostname);
        free( dwn);
      }
      break;
    case WINDOWS:
      {
        struct iocinfo_extra_windows *win;
        
        win = (struct iocinfo_extra_windows *) env->extra;
        
        free( win->user);
        free( win->machine);
        free( win);
      }
      break;
    }
  free(env);
}

/////////////////////////////


static void *new_ping_callback( void *data)
{
  struct iocinfo *ioc;
  struct ping_callback_info *pci;

  ioc = malloc( sizeof( struct iocinfo));
  pci = data;
 
  pci->event = BOOT;

  ioc->ioc_name = strdup(pci->ioc_name);
  ioc->conflict_flag = 0;
  
  ioc->data_up = calloc( 1, sizeof( struct iocinfo_data));
  ioc->data_up->next = NULL;
  ioc->data_up->ping = pci->ping;
  ioc->data_up->env = NULL;

  ioc->data_down = NULL;

  pci->status = ioc->data_up->status = INSTANCE_UP;

  // suppress read flag
  if( pci->ioc_flags & 2)
    pci->read_flag = 0;
  else
    pci->read_flag = 1;

  return (void *) ioc;
}

static int check_ping_id_match( struct iocinfo_ping *ping1, 
                                struct iocinfo_ping *ping2)
{
  if( (ping1->ip_address.s_addr == ping2->ip_address.s_addr) &&
      (ping1->origin_port == ping2->origin_port) &&
      (ping1->incarnation == ping2->incarnation) )
    return 1;
  else return 0;
}

static void existing_ping_callback( void *entry, void *data)
{
  struct iocinfo *ioc;
  struct iocinfo_data *iocdata;
  struct iocinfo_data *curr, *prev;

  struct ping_callback_info *pci;

  int found_flag = 0;

  ioc = entry;
  pci = data;

  pci->event = NONE;
  pci->read_flag = 0;
  if( ioc->data_up != NULL)
    {
      if( check_ping_id_match( &(ioc->data_up->ping), &(pci->ping)) )
        {
          // old or bad packet
          if( pci->ping.heartbeat <= ioc->data_up->ping.heartbeat)
            return;

          found_flag = 1;
        }
      else  // check rest of data_top
        {
          prev = ioc->data_up;
          curr = prev->next;
          while( curr != NULL)
            {
              if( check_ping_id_match( &(curr->ping), &(pci->ping)) )
                {
                  // old or bad packet
                  if( pci->ping.heartbeat <= curr->ping.heartbeat)
                    return;
                    
                  found_flag = 1;

                  if( !ioc->conflict_flag)
                    pci->event = CONFLICT_START;
                  ioc->conflict_flag = 1;

                  // move to front of data_up list
                  iocdata = curr;
                  prev->next = curr->next;
                  iocdata->next = ioc->data_up;
                  ioc->data_up = iocdata;
                  break;
                }
              prev = curr;
              curr = prev->next;
            }
        }
    }
  if( !found_flag && (ioc->data_down != NULL))
    {
      if( check_ping_id_match( &(ioc->data_down->ping), &(pci->ping)) )
        {
          // old or bad packet
          if( pci->ping.heartbeat <= ioc->data_down->ping.heartbeat)
            return;
          
          found_flag = 1;

          // remove from head of data_down list
          iocdata = ioc->data_down;
          ioc->data_down = iocdata->next;
        }
      else
        {
          prev = ioc->data_down;
          curr = prev->next;

          while( curr != NULL)
            {
              if( check_ping_id_match( &(curr->ping), &(pci->ping)) )
                {
                  // old or bad packet
                  if( pci->ping.heartbeat <= curr->ping.heartbeat)
                    return;
                    
                  found_flag = 1;

                  iocdata = curr;
                  prev->next = curr->next;
                  break;
                }
              prev = curr;
              curr = prev->next;
            }
        }
      if( found_flag)
        {
          if( ioc->data_up != NULL)
            {
              if( !ioc->conflict_flag)
                pci->event = CONFLICT_START;
              ioc->conflict_flag = 1;
            }
          else 
            pci->event = RECOVER;

          iocdata->next = ioc->data_up;
          ioc->data_up = iocdata;
        }
    }

  if( !found_flag) //have to create new entry
    {
      pci->event = BOOT;

      // need to add entry at top
      iocdata = calloc( 1, sizeof( struct iocinfo_data));
      iocdata->next = ioc->data_up;
      ioc->data_up = iocdata;
    }

  //  where is this RECOVER or UP being reported?

    ///////////

  iocdata = ioc->data_up;
  pci->status = iocdata->status = INSTANCE_UP;

  // the env data gets wiped at boot or if IOC says don't read
  // this keeps old env data being associated with new boot
  if( (pci->event == BOOT) || (pci->ioc_flags & 1) || 
      (pci->ping.reply_port != iocdata->ping.reply_port) )
    {
      free_iocenv( iocdata->env);
      iocdata->env = NULL;
      if(!(pci->ioc_flags & 2) )
        pci->read_flag = 1;
    }
  if( (pci->ping.user_msg != iocdata->ping.user_msg) &&
      (pci->event == NONE) )
    pci->event = MESSAGE;

  iocdata->ping = pci->ping;

  // existing data not replaced
  return;
}

static void *existing_ping_callback_stub( void *entry, void *data)
{
  existing_ping_callback( entry, data);

  // it's not going to replace ever
  return NULL;
}



struct update_env_struct
{
  struct iocinfo_ping *ping;
  struct iocinfo_env *env;
};

static void update_env_callback(void *entry, void *data)
{
  struct update_env_struct *ues;

  struct iocinfo *ioc;
  struct iocinfo_data *iocdata;

  ioc = entry;
  ues = data;

  iocdata = ioc->data_up;

  while( iocdata != NULL)
    {
      if( check_ping_id_match( &(iocdata->ping), ues->ping) )
        break;
      iocdata = iocdata->next;
    }

  if( iocdata == NULL)
    {
      free_iocenv(ues->env);
    }
  else
    {
      free_iocenv( iocdata->env);
      iocdata->env = ues->env;
    }

}

static int delete_callback( void *entry, void *data)
{
  struct iocinfo *ioc;
  struct iocinfo_data *iocdata, *iocnext;

  ioc = entry;

  iocdata = ioc->data_up;
  while( iocdata != NULL)
    {
      iocnext = iocdata->next;
      
      free_iocenv( iocdata->env);
      free( iocdata);

      iocdata = iocnext;
    }

  iocdata = ioc->data_down;
  while( iocdata != NULL)
    {
      iocnext = iocdata->next;
      
      free_iocenv( iocdata->env);
      free( iocdata);

      iocdata = iocnext;
    }
  
  free( ioc->ioc_name);

  free( entry);


  return 1; // it was deleted
}

static void destroy_callback( void *entry, void *data)
{
  delete_callback( entry, data);
}

///////////////////////////////////////



static struct iocinfo_data *get_overall_status_timeval( struct iocinfo *ioc,
                                                        uint8_t *status,
                                                        uint32_t *timeval)
{
  if( ioc->conflict_flag)
    {
      struct iocinfo_data *iocdata;
      uint32_t latest_time = 0;

      *status = STATUS_CONFLICT; // CONFLICT

      iocdata = ioc->data_up;
      while( iocdata != NULL)
        {
          if( iocdata->ping.boottime > latest_time)
            latest_time = iocdata->ping.boottime;
          iocdata = iocdata->next;
        }
      *timeval = latest_time;
    }
  else if( ioc->data_up != NULL)
    {
      if( ioc->data_up->status == INSTANCE_UP)
        {
          *status = STATUS_UP;  // UP 
          *timeval = ioc->data_up->ping.boottime;
        }
      else // MAYBE_UP
        {
          *status = STATUS_UNKNOWN; // unknown
          *timeval = 0;
        }
    }
  else
    {
      switch( ioc->data_down->status)
        {
        case INSTANCE_MAYBE_DOWN:
          *status = STATUS_UNKNOWN; // unknown
          *timeval = 0;
          break;
        case INSTANCE_DOWN:
          *status = STATUS_DOWN;  // down
          *timeval = ioc->data_down->ping.timestamp;
          break;
        case INSTANCE_UNTIMED_DOWN:
          *status = STATUS_DOWN_UNKNOWN; // use start time as minimum
          *timeval = 0;
          break;
        }
    }

  if( ioc->data_up != NULL)
    return ioc->data_up;
  else
    return ioc->data_down;
}


////////////////////////////////


static void timeout_init(void *ioc_entry, void *data)
{
  //  struct timeout_data *td;
  struct iocinfo *ioc;
  struct iocinfo_data *iocdata;

  //  td = data;
  ioc = ioc_entry;

  // turns MAYBE_DOWN to DOWN
  iocdata = ioc->data_down;
  while(iocdata != NULL)
    {
      if( iocdata->status == INSTANCE_MAYBE_DOWN) 
        {
          iocdata->status = INSTANCE_UNTIMED_DOWN;
          state_write( ioc->ioc_name, iocdata->status);
        }
      iocdata = iocdata->next;
    }

  /* if( ioc->data_up != NULL) */
  /*   { */
  /*     state_info_write( ioc->ioc_name, ioc->data_up->status,  */
  /*                       &(ioc->data_up->ping), ioc->data_up->env); */
  /*   } */
  /* else */
  /*   { */
  /*     state_info_write( ioc->ioc_name, ioc->data_down->status,  */
  /*                       &(ioc->data_down->ping), ioc->data_down->env); */
  /*   } */

}


/* void debug_db_print(struct iocinfo *ioc) */
/* {   */
/*   char addr_str[16]; */
/*   struct iocinfo_data *iocdata; */

/*   printf("%s\n", ioc->ioc_name); */
/*   printf("UP\n"); */
/*   iocdata = ioc->data_up; */
/*   while(iocdata != NULL) */
/*     { */
/*       printf("  %s:%d %d\n",  */
/*              address_to_string( addr_str, iocdata->ping.ip_address.s_addr), */
/*              iocdata->ping.origin_port, iocdata->ping.incarnation); */
/*       iocdata = iocdata->next; */
/*     } */
/*   printf("DOWN\n"); */
/*   iocdata = ioc->data_down; */
/*   while(iocdata != NULL) */
/*     { */
/*       printf("  %s:%d %d\n",  */
/*              address_to_string( addr_str, iocdata->ping.ip_address.s_addr), */
/*              iocdata->ping.origin_port, iocdata->ping.incarnation); */
/*       iocdata = iocdata->next; */
/*     } */
/*   printf("---------------\n"); */
/* } */


// basically looks for FAIL events
static void timeout_checker(void *ioc_entry, void *data)
{
  struct timeout_data *td;
  struct iocinfo *ioc;
  struct iocinfo_data *iocdata, **iocptr, **iocothpos;

  int conflict_flag;
  
  td = data;
  ioc = ioc_entry;

  /* debug_db_print( ioc); */


  iocptr = &(ioc->data_up);
  iocdata = *iocptr;
  iocothpos = &(ioc->data_down);
  while( iocdata != NULL)
    {
      // FAIL ,  add 1 to allow some slop for final heartbeat
      if( (td->currtime - iocdata->ping.timestamp) >= 
            (config.fail_number_heartbeats * iocdata->ping.period + 1) )
        {
          if( iocdata->status == INSTANCE_UP)
            iocdata->status = INSTANCE_DOWN;
          else
            iocdata->status = INSTANCE_UNTIMED_DOWN;

          // if it's the first entry....
          if(ioc->data_up == iocdata)
            {
              state_write( ioc->ioc_name, iocdata->status);

              // only write state change down if no conflict 
              if( !ioc->conflict_flag)
                {
                  event_process( ioc->ioc_name, td->currtime, FAIL, 
                                 iocdata->ping, iocdata->env);
                }
            }

          // move to data_down
          *iocptr = iocdata->next;
          iocdata->next = *iocothpos;
          *iocothpos = iocdata;
          iocothpos = &(iocdata->next);
        }
      else
        iocptr = &(iocdata->next);
      
      iocdata = *iocptr;
    }

  conflict_flag = 0;
  if( ioc->data_up != NULL)
    {
      uint32_t boot;

      boot = ioc->data_up->ping.boottime;
      iocdata = ioc->data_up->next;
      while( iocdata != NULL)
        {
          if( iocdata->ping.timestamp > boot)
            {
              conflict_flag = 1;
              break;
            }
          iocdata = iocdata->next;
        }
    }

  if( ioc->conflict_flag )
    {
      if(!conflict_flag)
        {
          ioc->conflict_flag = 0;
              
          event_process( ioc->ioc_name, td->currtime, CONFLICT_STOP, 
                         ioc->data_up->ping, ioc->data_up->env);

          // to make sure last written item was the last running ioc
          if( ioc->data_up != NULL)
            state_info_write( ioc->ioc_name, ioc->data_up->status, 
                              &(ioc->data_up->ping), ioc->data_up->env);
          else
            state_info_write( ioc->ioc_name, ioc->data_down->status, 
                              &(ioc->data_down->ping), ioc->data_down->env);
        }
    }
  else
    {
      if( conflict_flag)
        {
          ioc->conflict_flag = 1;

          event_process( ioc->ioc_name, td->currtime, CONFLICT_START, 
                         ioc->data_up->ping, ioc->data_up->env);
        }
    }


  // remove dead entries from down list
  if( ioc->data_up != NULL)
    iocptr = &(ioc->data_down);
  else  // if none up, skip first entry, which SHOULD exist!!
    iocptr = &(ioc->data_down->next);
  iocdata = *iocptr;
  while( iocdata != NULL)
    {
      if( (td->currtime - iocdata->ping.timestamp) >
          config.instance_retain_time)
        {
          *iocptr = iocdata->next;

          free_iocenv( iocdata->env);
          free( iocdata);
        }
      else
        iocptr = &(iocdata->next);

      iocdata = *iocptr;
    }

}


///////////////////////////////


// This is done slightly differently becuase the information has not 
// been added to the system yet, for pure logging purposes.
// There is no called function, as nothing is searched.

static void ioc_log_info( pthread_mutex_t *info_lock_ptr, char *ioc_name, 
                          struct iocinfo_ping *ping, struct iocinfo_env *env)
{
  char addr_str[16];
  char time_str[32];

  int i;

  FILE *info_ptr = NULL;
          
  pthread_mutex_lock(info_lock_ptr);
  if( (info_ptr = fopen(config.info_file, "a")) == NULL)
    log_error_write(errno, "Open log file for writing");
  else
    {
      fprintf( info_ptr, "%s  -  ", ioc_name );
      fprintf( info_ptr, "v%d ", ping->protocol_version );
      fprintf( info_ptr, "[%s] ", time_to_string( time_str, ping->timestamp));
      fprintf( info_ptr, "%s - %d\n  ",  
               address_to_string( addr_str, ping->ip_address.s_addr),
               ping->user_msg);
      fprintf( info_ptr, "Boot=[%s] ", 
               time_to_string( time_str, ping->boottime));
      fprintf( info_ptr, "Incarnation=%d ", ping->incarnation);
      fprintf( info_ptr, "Port=%u ", ping->reply_port);
      fprintf( info_ptr, "Period=%u\n", ping->period);
  
      if( env != NULL)
        {
          for( i = 0; i < env->count; i++)
            fprintf( info_ptr, "%s=%s\n", env->key[i], env->value[i]);
              
          switch( env->extra_type)
            {
            case 1:
              {
                struct iocinfo_extra_vxworks *vw;
                
                vw = env->extra;
                fprintf( info_ptr, "vxWorks bootline: %s(%d,%d)%s:%s", 
                         vw->bootdev, vw->unitnum, vw->procnum, 
                         vw->boothost_name, vw->bootfile);
                if( vw->address[0] != '\0')
                  fprintf( info_ptr, " e=%s", vw->address);
                if( vw->backplane_address[0] != '\0')
                  fprintf( info_ptr, " b=%s", vw->backplane_address);
                if( vw->boothost_address[0] != '\0')
                  fprintf( info_ptr, " h=%s", vw->boothost_address);
                if( vw->gateway_address[0] != '\0')
                  fprintf( info_ptr, " g=%s", vw->gateway_address);
                if( vw->boothost_username[0] != '\0')
                  fprintf( info_ptr, " u=%s", vw->boothost_username);
                if( vw->boothost_password[0] != '\0')
                  fprintf( info_ptr, " pw=%s", vw->boothost_password);
                if( vw->flags != 0)
                  fprintf( info_ptr, " f=0x%X", vw->flags);
                if( vw->target_name[0] != '\0')
                  fprintf( info_ptr, " tn=%s", vw->target_name);
                if( vw->startup_script[0] != '\0')
                  fprintf( info_ptr, " s=%s", vw->startup_script);
                if( vw->other[0] != '\0')
                  fprintf( info_ptr, " o=%s", vw->other);
                fprintf( info_ptr, "\n");
              }
              break;
            case 2:
              {
                struct iocinfo_extra_linux *lnx;
        
                lnx = env->extra;
                fprintf( info_ptr, 
                         "Linux user, group, and host: %s(%s)@%s\n", 
                         lnx->user, lnx->group, lnx->hostname);
              }
              break;
            case 3:
              {
                struct iocinfo_extra_darwin *dwn;
                
                dwn = env->extra;
                fprintf( info_ptr, 
                         "Darwin user, group, and host: %s(%s)@%s\n", 
                         dwn->user, dwn->group, dwn->hostname);
              }
              break;
            case 4:
              {
                struct iocinfo_extra_windows *win;
                
                win = env->extra;
                fprintf( info_ptr, 
                         "Windows user and machine: %s@%s\n", 
                         win->user, win->machine);
              }
              break;
            }
        }
      fprintf( info_ptr, "\n");

      fclose( info_ptr);
    }
  pthread_mutex_unlock(info_lock_ptr);
}



///////////////////


void retrieve_ioc_env_func(void *entry, void *data)
{
  struct iocinfo *ioc;
  struct iocinfo_env **env;

  ioc = entry;
  env = data;

  if( ioc->data_up != NULL)
    *env = attach_iocenv(ioc->data_up->env);
  else
    *env = attach_iocenv(ioc->data_down->env);
   
}

struct iocinfo_env *retrieve_ioc_env( char *ioc_name)
{
  struct iocinfo_env *env;

  env = NULL;
  db_find( db.ioc_db, ioc_name, retrieve_ioc_env_func, &env);
  return env;
}


struct get_ioc_info_struct
{
  pthread_mutex_t *info_lock_ptr;
  char *ioc_name;
  char read_flag;
  int event;

  struct iocinfo_ping ping;
  uint8_t status;
};


// needs to free all data
// if it bombs out, go to Events, as they MUST be processed
static void *get_ioc_info( void *data)
{
  struct get_ioc_info_struct *gii;

  struct iocinfo_env *env;

  gii = data;

  env = NULL;

  if( gii->read_flag)
    {  
      int ioc_sockfd;
      struct sockaddr_in ioc_addr;
      int result;

#define PACKET_BUFFER (10001)

      uint16_t version;
      uint32_t length;
      uint16_t ioc_type;

      uint8_t len8;
      uint16_t len16;

      char data_buffer[PACKET_BUFFER];

      int len, addlen;

      char *p;
      int i;
  

      struct update_env_struct ues;

  
      ioc_sockfd = socket(AF_INET, SOCK_STREAM, 0);

      bzero( &ioc_addr, sizeof(ioc_addr) );
      ioc_addr.sin_family = AF_INET;
      ioc_addr.sin_port = htons(gii->ping.reply_port);
      ioc_addr.sin_addr = gii->ping.ip_address;
                  
      result = connect(ioc_sockfd, (struct sockaddr *)&ioc_addr, 
                       sizeof(ioc_addr) );
      if( result == -1)
        {
          char addr_str[16];

          log_write("get_ioc_info: can't connect to %s (%s@%d).\n",
                    gii->ioc_name, 
                    address_to_string( addr_str, gii->ping.ip_address.s_addr),
                    gii->ping.reply_port );
          close( ioc_sockfd);

          goto ReadFail;
        }

      len = 0;
      while( (addlen = read( ioc_sockfd, data_buffer + len, 
                             PACKET_BUFFER - len)) != 0)
        len += addlen;
      close( ioc_sockfd);
      
      // 10 is the minimum length
      if( len < 10)
        {
          log_write("get_ioc_info: Message too small from %s.\n",
                    gii->ioc_name );
          goto ReadFail;
        }

      p = data_buffer;
      version = ntohs( *((uint16_t *) p));
      p += 2;
      ioc_type = ntohs( *((uint16_t *) p));
      p += 2;
      length = ntohl( *((uint32_t *) p));
      p += 4;

      if((version < MIN_PROTOCOL_VERSION) && (version > MAX_PROTOCOL_VERSION))
        {
          log_write("get_ioc_info: Unsupported message format "
                    "V%d from %s.\n", version, gii->ioc_name );
          goto ReadFail;
        }
      if(len != length)
        {
          log_write("get_ioc_info: Message length from "
                    "%s doesn't match internal value.\n", gii->ioc_name );
          goto ReadFail;
        }

      env = malloc( sizeof( struct iocinfo_env));
      if( env == NULL)
        {
          log_write("get_ioc_info: Can't allocate memory.\n" );
          goto ReadFail;
        }
      // value is 2 because the database will get it, then it goes to
      // the loggers and notifiers
      sharedint_init( &(env->ref), 2);

      env->count = ntohs( *((uint16_t *) p));
      p += 2;
      
      env->key = malloc( env->count * sizeof( char *) );
      env->value = malloc( env->count * sizeof( char *) );
      
      if( (env->key == NULL) || (env->value == NULL) )
        {
          log_write("get_ioc_info: Can't allocate memory.\n" );
          free( env->key);
          free( env->value);
          free( env);
          env = NULL;
          
          goto ReadFail;
        }
      
      /* FIX ME! */
      /* need to check each strdup for success! */
      
      for( i = 0; i < env->count; i++)
        {
          len8 = *((uint8_t *) (p++));
          env->key[i] = strndup(p, len8);
          p += len8;
          
          len16 = ntohs(*((uint16_t *) p));
          p += 2;
          env->value[i] = strndup(p, len16);
          p += len16;
        }

      env->extra_type = GENERIC;
      env->extra = NULL;
      switch(ioc_type)
        {
        case 1: // VXWORKS
          env->extra = (void *) parse_vxworks_info( p);
          if( env->extra != NULL)
            env->extra_type = VXWORKS;
          break;
        case 2:
          {
            struct iocinfo_extra_linux *lnx;
              
            if( (lnx = malloc( sizeof( struct iocinfo_extra_linux)) ) == NULL)
              goto ReadFail;
            env->extra_type = LINUX;
            env->extra = (void *) lnx;
              
            lnx->user = buffer_string_grab( &p);
            lnx->group = buffer_string_grab( &p);
            lnx->hostname = buffer_string_grab( &p);
          }
          break;
        case 3:
          {
            struct iocinfo_extra_darwin *dar;
              
            if((dar = malloc( sizeof( struct iocinfo_extra_darwin)) ) == NULL)
              goto ReadFail;
            env->extra_type = DARWIN;
            env->extra = (void *) dar;
              
            dar->user = buffer_string_grab( &p);
            dar->group = buffer_string_grab( &p);
            dar->hostname = buffer_string_grab( &p);
          }
          break;
        case 4:
          {
            struct iocinfo_extra_windows *win;
              
            if((win = malloc( sizeof( struct iocinfo_extra_windows)) ) == NULL)
              goto ReadFail;
            env->extra_type = WINDOWS;
            env->extra = (void *) win;
              
            win->user = buffer_string_grab( &p);
            win->machine = buffer_string_grab( &p);
          }
          break;
        }

      ues.ping = &(gii->ping);
      ues.env = env;
      // env pointer gets "taken" here, but is accessible for events later
      // as it was initialized with 2 holds, one removed later in function
      db_find( db.ioc_db, gii->ioc_name, update_env_callback, &ues);
      
    ReadFail:
      // At this point, either env will be filled or will be NULL
      // both are fine for this purpose

      // dump information to log file
      ioc_log_info( gii->info_lock_ptr, gii->ioc_name, &(gii->ping), env);

      state_info_write( gii->ioc_name, gii->status, &(gii->ping), env);

      if( gii->event != NONE)
        event_process( gii->ioc_name, (uint32_t) time(NULL), gii->event,
                       gii->ping, env);
    }
  else
    {
      if( gii->event != NONE)
        {
          env = retrieve_ioc_env( gii->ioc_name);
          // load the env information

          event_process( gii->ioc_name, (uint32_t) time(NULL), gii->event,
                         gii->ping, env);
      
          if( gii->event == RECOVER)
            state_write( gii->ioc_name, gii->status);
        }
    }


  // remove local hold on env, as second lock held by db
  free_iocenv(env);

  free(gii->ioc_name);
  free(data);

  return NULL;
}


// return whether an env update is needed
static int packet_insert( char *ioc_name, struct iocinfo_ping ping, 
                          uint16_t ioc_flags, char *read_flag, 
                          uint8_t *status, int *event )
{
  struct ping_callback_info pci;

  if( db.ioc_db == NULL)
    return 0;

  pci.ioc_name = ioc_name;
  pci.ping = ping;
  pci.ioc_flags = ioc_flags;

  // db_find does not lock whole tree, while db_add does
  if( !db_find( db.ioc_db, ioc_name, existing_ping_callback, &pci ))
    if( db_add( db.ioc_db, ioc_name, new_ping_callback, 
                existing_ping_callback_stub, &pci) )
      return 0; // unresolvable problem, don't process anymore

  if( !pci.read_flag && (pci.event == NONE) )
    return 0;

  *read_flag = pci.read_flag;
  *status = pci.status;
  *event = pci.event;
  
  return 1;
}



static void ioc_process_packet( struct in_addr ip_address, uint16_t origin_port,
                                char *data_buffer, int data_length, 
                                pthread_mutex_t *info_lock)
{
  char ioc_name[64];

  char address_string[16];

  struct iocinfo_ping ping;
  uint32_t ioc_timestamp;
  uint16_t ioc_flags;

  char read_flag;
  uint8_t status;
  int event;

  uint16_t version;

  char *p;

  if( db.ioc_db == NULL)
    return;

  address_to_string( address_string, (uint32_t) ip_address.s_addr);

  // 28 is length of static fields, minimum iocname is 1 char //
  //    plus 1 null termination --> 30
  if( (data_length < 30) || (data_buffer[data_length-1] != '\0') )
    {
      if( data_length < 30)
        log_write("(%s) Bad packet length: %d\n", address_string, data_length);
      else
        log_write("(%s) Packet not null terminated\n", address_string);

      return;
    }

  // check packet for magic number
  p = data_buffer;
  if( ntohl( *((uint32_t *) p)) != 0x12345678)
    {
      log_write("(%s) Bad magic number: %d\n", address_string, 
                ntohl( *((uint32_t *) p)));
      return;
    }
  p += 4;

  // check packet version
  version = ntohs( *((uint16_t *) p));
  if( (version < MIN_PROTOCOL_VERSION) || 
      (version > MAX_PROTOCOL_VERSION) )
    {
      log_write("(%s) Bad version: %d\n", address_string, version);
      return;
    }
  p += 2;

  ping.timestamp = time(NULL);
  ping.ip_address = ip_address;
  ping.origin_port =  origin_port;
  ping.protocol_version = version;

  // back to reading buffer

  ping.incarnation = ntohl( *((uint32_t *) p)) + 631152000;
  p += 4;
  ioc_timestamp = ntohl( *((uint32_t *) p)) + 631152000;
  p += 4;
  ping.heartbeat = ntohl( *((uint32_t *) p));
  p += 4;
  if( version >= 5)
    {
      ping.period = ntohs( *((uint16_t *) p));
      p += 2;
    }
  else
    ping.period = 15; // default period
  ioc_flags = ntohs( *((uint16_t *) p));
  p += 2;
  ping.reply_port = ntohs( *((uint16_t *) p));
  p += 2;
  ping.user_msg = ntohl( *((uint32_t *) p));
  p += 4;

  strncpy( ioc_name, p, 63);
  ioc_name[63] = '\0';

  // this weirdness is because the IOC time might be different
  // so we use the time difference, and apply locally
  ping.boottime = ping.timestamp - (ioc_timestamp - ping.incarnation);
      
  if( packet_insert( ioc_name, ping, ioc_flags, &read_flag, &status, &event ) )
    {
      pthread_t thread;
      pthread_attr_t attr;

      struct get_ioc_info_struct *gii;

      gii = malloc( sizeof( struct get_ioc_info_struct) );

      gii->ioc_name = strdup(ioc_name);
      gii->info_lock_ptr = info_lock;
      gii->read_flag = read_flag;
      gii->event = event;
      gii->ping = ping;
      gii->status = status;
      
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      pthread_create(&thread, &attr, get_ioc_info, (void *) gii );
      pthread_attr_destroy( &attr);
    }

}




struct process_heartbeat_struct
{
  int socket;
};


static void *process_heartbeat( void *data)
{
  int sockfd;

  // info file lock
  pthread_mutex_t info_lock;

  int data_length;
  char data_buffer[256];  // packet can't be over 200

  struct sockaddr_in r_addr;
  socklen_t r_len;

  sockfd = ( (struct process_heartbeat_struct *) data)->socket;
  free(data);

  pthread_mutex_init(&info_lock, NULL);

  while(1 )
    {
      r_len = sizeof( struct sockaddr_in);
      data_length = 
        recvfrom( sockfd, (void *) data_buffer, 256 * sizeof( char), 
                  0, (struct sockaddr *) &r_addr, &r_len);

      ioc_process_packet( r_addr.sin_addr, ntohs(r_addr.sin_port),
                          data_buffer, data_length, &info_lock);
    }

  return NULL;
}


static void *monitor_data( void *data)
{
  struct timeout_data td;


  sleep(config.fail_check_period);

  td.currtime = time(NULL);  // not really needed
  db_walk( db.ioc_db, timeout_init, &td );

  while(1)
    {
      td.currtime = time(NULL);
      db_walk( db.ioc_db, timeout_checker, &td );

      sleep(5);
    }


  return NULL;
}


///////////////////////////////////
// External functions 
//////////////////////////////////////

int iocdb_start(void)
{
  int sockfd;
  int flag;

  struct sockaddr_in ip_addr;
  struct process_heartbeat_struct *phs;

  pthread_t thread;
  pthread_attr_t attr;

  ///////////////////

  db.ioc_db = db_create( (void * (*)(void *)) strdup, free,
                         (int (*)(const void *, const void *)) strcmp, 1);

  if( db.ioc_db != NULL)
    state_files_load();

  //////////////////

   // bind UDP socket for receiving packets from IOCs, then start thread
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  flag = 1;
  if( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, 
                 sizeof(flag)) == -1) 
    {
      log_error_write(errno, "UDP setsockopt");
      return 1;
    }
  //  fcntl(hb_udp_sockfd, F_SETFL, fcntl(hb_udp_sockfd, F_GETFL) | O_NONBLOCK);
  bzero( &ip_addr, sizeof(ip_addr) );
  ip_addr.sin_family = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ip_addr.sin_port = htons(config.heartbeat_udp_port);
  if( bind(sockfd, (struct sockaddr *) &ip_addr, sizeof( ip_addr) ))
    {
      log_error_write(errno, "UDP bind");
      return 1;
    }

  phs = malloc( sizeof( struct process_heartbeat_struct) );
  phs->socket = sockfd;
         
  pthread_attr_init(&attr);
  //  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &attr, process_heartbeat, (void *) phs );
  pthread_attr_destroy( &attr);

  //////////////////////////

  // startes the thread with the monitoring process

  pthread_attr_init(&attr);
  //  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &attr, monitor_data, NULL );
  pthread_attr_destroy( &attr);

  return 0;
}


void iocdb_stop(void)
{  
  struct tree_db *ioc_db;

  if( db.ioc_db == NULL)
    return;

  ioc_db = db.ioc_db;
  db.ioc_db = NULL;  // stops any more access
  sleep(1); // time for working threads to finish (or not)
  db_destroy( ioc_db, destroy_callback, NULL);
}


int iocdb_number_iocs(void)
{
  return db_count( db.ioc_db);
}

int iocdb_missing(void)
{
  if( db.ioc_db == NULL)
    return 1;
  return 0;
}



int iocdb_remove( char *ioc_name, int files_flag)
{
  int ret;

  if( db.ioc_db == NULL)
    return 0;
  ret = db_delete( db.ioc_db, ioc_name, delete_callback, NULL);
  if( ret && files_flag)
    {
      state_file_remove( ioc_name);
      event_file_remove( ioc_name);
    }

  return ret;
}

////////////////////////////////////////



struct names_get_struct
{
  int counter;
  struct access_names_db_struct *ands;
};


void iocdb_names_release(struct access_names_db_struct *name_db)
{
  int i;

  for( i = 0; i < name_db->number; i++)
    free( name_db->names[i]);
  free( name_db->names);
  free( name_db);
}

static void get_name_init(void *arg, int number)
{
  struct names_get_struct *ngs = arg;

  ngs->ands->number = number;
  ngs->ands->names = calloc( number, sizeof( char *));
}

static void get_name_func(void *entry, void *arg)
{
  struct iocinfo *ioc = entry;
  struct names_get_struct *ngs = arg;

  ngs->ands->names[ngs->counter] = (char *) strdup( ioc->ioc_name);
  ngs->counter++;
}

struct access_names_db_struct *iocdb_names_get(void)
{
  struct names_get_struct ngs;
  
  ngs.counter = 0;
  ngs.ands = calloc( 1, sizeof( struct access_names_db_struct));
  db_walk_init( db.ioc_db, get_name_init, get_name_func, (void *) &ngs );

  return ngs.ands;
}


///////

struct info_get_struct
{
  int counter;
  int num_wanted; // set to 0 for all, will get overwritten with db_num
  struct access_info_db_struct *aids;
};


void iocdb_info_release(struct access_info_db_struct *info_db)
{
  int i;

  for( i = 0; i < info_db->number; i++)
    {
      free( info_db->infos[i].ioc_name);
      free_iocenv( info_db->infos[i].env);
    }
  free( info_db->infos);
  free( info_db);
}

static void info_get_init(void *arg, int number)
{
  struct info_get_struct *igs = arg;

  // don't set igs->aids->number here
  if( igs->num_wanted == 0)
    igs->num_wanted = number;
    
  igs->aids->infos = malloc( igs->num_wanted *
                             sizeof( struct access_info_struct));
}

static void info_get_func(void *entry, void *arg)
{
  struct info_get_struct *igs = arg;
  struct iocinfo *ioc = entry;

  struct iocinfo_data *iocdata;
  struct access_info_struct *ais;

  ais = &(igs->aids->infos[igs->counter]);

  iocdata = get_overall_status_timeval( ioc, &(ais->overall_status), 
                                        &(ais->time_value));
  ais->ioc_name = strdup( ioc->ioc_name);
  ais->ping = iocdata->ping;
  ais->env = attach_iocenv(iocdata->env);

  igs->counter++;
}


struct access_info_db_struct *iocdb_info_get_all(void)
{
  struct info_get_struct igs;
  
  igs.counter = 0;
  igs.num_wanted = 0;
  igs.aids = calloc( 1, sizeof( struct access_info_db_struct));
  
  db_walk_init( db.ioc_db, info_get_init, info_get_func, (void *) &igs );
  
  igs.aids->number = igs.counter;

  return igs.aids;
}

struct access_info_db_struct *iocdb_info_get_multi(int number, char **ioc_names)
{
  struct info_get_struct igs;
  
  igs.counter = 0;
  igs.num_wanted = number;
  igs.aids = calloc( 1, sizeof( struct access_info_db_struct));
  
  db_multi_find_init( db.ioc_db, number, (void **) ioc_names, info_get_init,
                      info_get_func, (void *) &igs );

  igs.aids->number = igs.counter;
  if( igs.counter < number)
    {
      igs.aids->infos =
        realloc( igs.aids->infos,
                 igs.counter * sizeof( struct access_info_struct));
    }
  
  return igs.aids;
}

struct access_info_db_struct *iocdb_info_get_single( char *ioc_name)
{
  struct info_get_struct igs;
  
  igs.counter = 0;
  igs.num_wanted = 1;
  igs.aids = calloc( 1, sizeof( struct access_info_db_struct));
  
  db_find_init( db.ioc_db, (void *) ioc_name, info_get_init,
                info_get_func, (void *) &igs );

  igs.aids->number = igs.counter;

  return igs.aids;
}

/////


struct detail_get_struct
{
  int counter;
  int num_wanted; // set to 0 for all, will get overwritten with db_num
  struct access_detail_db_struct *adds;
};

static void detail_release_func( struct access_detail_db_struct *detail_db)
{
  struct access_detail_struct *ads;

  int i, j;

  for( i = 0; i < detail_db->number; i++)
    {
      ads = &(detail_db->details[i]);
      free( ads->ioc_name);
      for( j = 0; j < ads->instance_count; j++)
        free_iocenv(ads->instances[j].env);
      free( ads->instances);
    }
       
  free( detail_db->details);
  free( detail_db);
}

static void detail_get_init(void *arg, int number)
{
  struct detail_get_struct *dgs = arg;

  // don't set dgs->aids->number here
  if( dgs->num_wanted == 0)
    dgs->num_wanted = number;
    
  dgs->adds->details = malloc( dgs->num_wanted *
                               sizeof( struct access_detail_struct));
}


void iocdb_debug_release(struct access_detail_db_struct *adds)
{
  detail_release_func( adds);
}

static void debug_get_func(void *entry, void *arg)
{
  struct detail_get_struct *dgs = arg;
  struct iocinfo *ioc = entry;

  struct iocinfo_data *iocdata;

  struct access_detail_struct *ads;
  struct access_instance_struct *ais;

  int count;

  ads = &(dgs->adds->details[dgs->counter]);
 
  ads->ioc_name = strdup( ioc->ioc_name);
  get_overall_status_timeval( ioc, &(ads->overall_status), &(ads->time_value) );
 
  count = 0;
  iocdata = ioc->data_up;
  while( iocdata != NULL)
    {
      count++;
      iocdata = iocdata->next;
    }
  iocdata = ioc->data_down;
  while( iocdata != NULL)
    {
      count++;
      iocdata = iocdata->next;
    }
  ads->instance_count = count;
  ads->instances = calloc( count, sizeof( struct access_instance_struct));

  ais = ads->instances;

  iocdata = ioc->data_up;
  while( iocdata != NULL)
    {
      ais->status = iocdata->status;
      ais->ping = iocdata->ping;
      ais->env = attach_iocenv(iocdata->env);

      iocdata = iocdata->next;
      ais++;
    }
  iocdata = ioc->data_down;
  while( iocdata != NULL)
    {
      ais->status = iocdata->status;
      ais->ping = iocdata->ping;
      ais->env = attach_iocenv(iocdata->env);

      iocdata = iocdata->next;
      ais++;
    }

  dgs->counter++;
}

struct access_detail_db_struct *iocdb_get_debug(char *ioc_name)
{
  struct detail_get_struct dgs;
  
  dgs.counter = 0;
  dgs.num_wanted = 1;
  dgs.adds = calloc( 1, sizeof( struct access_detail_db_struct));

  db_find_init( db.ioc_db, (void *) ioc_name, detail_get_init,
                debug_get_func, (void *) &dgs );
  dgs.adds->number = dgs.counter;
 
  return dgs.adds;
}





void iocdb_conflict_release(struct access_detail_db_struct *adds)
{
  detail_release_func( adds);
}

static void conflict_get_func(void *entry, void *arg)
{
  struct detail_get_struct *dgs = arg;
  struct iocinfo *ioc = entry;

  struct iocinfo_data *iocdata;

  struct access_detail_struct *ads;
  struct access_instance_struct *ais;

  int count;
  uint32_t boot;

  ads = &(dgs->adds->details[dgs->counter]);

  ads->ioc_name = strdup( ioc->ioc_name);
  get_overall_status_timeval( ioc, &(ads->overall_status), &(ads->time_value) );
 
  if( ioc->data_up == NULL)
    {
      ads->instance_count = 0;
      ads->instances = NULL;
    }
  else
    {
      count = 0;
      boot = ioc->data_up->ping.boottime;

      iocdata = ioc->data_up->next;  // skip
      while( iocdata != NULL)
        {
          if( iocdata->ping.timestamp > boot)
            count++;
          iocdata = iocdata->next;
        }
      if( !count)
        {
          ads->instance_count = 0;
          ads->instances = NULL;
        }
      else
        {
          count++; // add skipped

          ads->instance_count = count;
          ads->instances = calloc( count, 
                                   sizeof( struct access_instance_struct));
          ais = ads->instances;

          iocdata = ioc->data_up;
          ais->status = iocdata->status;
          ais->ping = iocdata->ping;
          ais->env = attach_iocenv(iocdata->env);
          ais++;
          
          iocdata = ioc->data_up->next;
          while( iocdata != NULL)
            {
              if( iocdata->ping.timestamp > boot)
                {
                  ais->status = iocdata->status;
                  ais->ping = iocdata->ping;
                  ais->env = attach_iocenv(iocdata->env);
                  ais++;
                }
              iocdata = iocdata->next;
            }
        }
    }

  dgs->counter++;
}

struct access_detail_db_struct *iocdb_get_conflict(char *ioc_name)
{
  struct detail_get_struct dgs;
  
  dgs.counter = 0;
  dgs.num_wanted = 1;
  dgs.adds = calloc( 1, sizeof( struct access_detail_db_struct));

  db_find_init( db.ioc_db, (void *) ioc_name, detail_get_init,
                conflict_get_func, (void *) &dgs );
  dgs.adds->number = dgs.counter;
 
  return dgs.adds;
}




///////////////////////




/* void test_func( int socket) */
/* { */
/*   void print_data( void *data, void *arg) */
/*   { */
/*     char buffer[1024]; */

/*     struct api_info_struct *ais; */

/*     ais = data; */

/*     sprintf(buffer, "%s\n", ais->ioc_name ); */
/*     send( socket, buffer, strlen(buffer), 0); */
/*   } */


/*   struct llist *list; */

/*   list = iocdb_get_list_all(); */
/*   list_apply( list, print_data, NULL); */
  
/*   iocdb_list_release(list); */
/* } */


void iocdb_debug_print_tree( char *ps_name)
{
  if( db.ioc_db == NULL)
    return;

  db_ps( db.ioc_db, ps_name);
}




