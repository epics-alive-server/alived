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
#include <limits.h>
#include <errno.h>

#include <sys/time.h>
#include <arpa/inet.h>
//#include <netinet/in.h>

#include <netdb.h>
#include <pthread.h>

#include "alived.h"
#include "notifydb.h"
#include "iocdb.h"
#include "logging.h"
#include "utility.h"
#include "llrb_db.h"
#include "iocdb_access.h"

#include "gentypes.h"

////////////////////

// just some config strings
extern struct alived_config config;


// when entry expires, simply NULL entry and deallocate it
// remove link when references are set to zero


///////////////////////////////

struct event_entry
{
  // used for talking with clients to identify which event was acknowledged
  uint32_t id; 
  // used for telling when to deallocate event
  int references;

  unsigned char expired;

  int msg_len;
  unsigned char *msg;
};

/////////////////////

enum subscription_type { ACCEPTED, WAITING };

struct subscriberinfo_list
{
  int number;
  char **names;
  int *events; // 0: all
};

struct subscriberinfo_net
{
  int number;
  unsigned char *nets;
  int *events;
};

struct subscriberinfo_vars
{
  int number;
  char **keys;
  char **vals;
  int *events;
};

struct subscriberinfo
{
  struct sockaddr_in addr;
  socklen_t addr_len;

  int attempts;  // how many attempts to make before dropping
  // initially used for connecting, then events
  
  uint32_t incarnation;
  uint32_t last_heartbeat_time;

  /* uint32_t first_event;  // temporarily needed when connecting */
  
  int types; // 0: all, &1: ioc_list, &2: subnet, &3: envvars
  struct subscriberinfo_list *iocs;
  struct subscriberinfo_net *nets;
  struct subscriberinfo_vars *vars;
  unsigned char bloom_bits[128]; // used by all three types

  struct llist *event_queue;  // struct event_entry entries
};


struct notifydb
{
  // struct subscriberinfo values
  struct tree_db *subs_waiting;  // subscribers waiting to be verified
  struct tree_db *subs;          // subscribers that are being serviced

  struct llist *events_pending; // not given to subscribers yet
  // struct event_entry values
  struct llist *events;         // active events  

  uint32_t nextevent_id;        // id number for next event
  /* uint32_t nextsentevent_id;    // next id number to se sent out */
};


////////////////////

struct 
{
  struct notifydb *db;
  pthread_t service_thread;
} ndb = { NULL};


/////////////////

// for the LLRB database


struct subscriber_key
{
  struct sockaddr_in addr;
  socklen_t addr_len;
};

void *sub_key_copy( void *r_key)
{
  struct subscriber_key *key;

  key = calloc( 1, sizeof( struct subscriber_key));
  *key = *((struct subscriber_key *) r_key);
  
  return key;
}

int sub_key_compare(const void *key1, const void *key2)
{
  const struct subscriber_key *k1 = key1;
  const struct subscriber_key *k2 = key2;

  if( k1->addr_len == k2->addr_len)
    return memcmp( key1, key2, k1->addr_len);

  if( k1->addr_len < k2->addr_len)
    return -1;
  return 1;
};


//////////////////////////////////


static void lf_free_subscriber( void *data, void *arg)
{
   struct event_entry *event = data;

   event->references--;
}

static void free_subscriber( struct subscriberinfo *info)
{
  int i;

  list_destroy( info->event_queue, lf_free_subscriber, NULL );

  if( info->iocs != NULL)
    {
      for( i = 0; i < info->iocs->number; i++)
        free( info->iocs->names[i]);
      free( info->iocs->names);
      free( info->iocs->events);
      free( info->iocs);
    }
  if( info->nets != NULL)
    {
      free( info->nets->nets);
      free( info->nets->events);
      free( info->nets);
    }
  if( info->vars != NULL)
    {
      for( i = 0; i < info->vars->number; i++)
        {
          free( info->vars->keys[i]);
          free( info->vars->vals[i]);
        }
      free( info->vars->keys);
      free( info->vars->vals);
      free( info->vars);
    }

  //DDD
  //  printf( "Subscriber deleted\n"); fflush(stdout);

  free(info);
}

//////////------------///////


static void db_find_get_incarnation( void *data, void *arg)
{
  struct subscriberinfo *linfo = data;
  uint32_t *incarnation = arg;

  *incarnation = linfo->incarnation;
}


static void *db_subscriber_add_adder( void *arg)
{
  struct subscriberinfo *pinfo;

  pinfo = malloc( sizeof(struct subscriberinfo) );
  *pinfo = *((struct subscriberinfo *) arg);
  
  pinfo->event_queue = list_create();

  return pinfo;
}

// returns 1 if record added
static int subscriber_add( struct sockaddr_in *r_addr, socklen_t r_len,
                           uint32_t incarnation)
{
  struct subscriberinfo info;
  struct subscriber_key sks;

  uint32_t db_incarnation;
  
  sks.addr = *r_addr;
  sks.addr_len = r_len;

  if( db_find( ndb.db->subs_waiting, &sks, db_find_get_incarnation,
               &db_incarnation) )
    return (db_incarnation == incarnation) ? 1 : 0;
  
  // don't do anything if it is already in the accepted database
  // if this is a legitimate attempt, it has to wait for the timeout to happen










  
  // NEED TO CHECK THE INCARNATION!
  if( db_find( ndb.db->subs, &sks, NULL, NULL) )
    return 0;
  
  memcpy( &(info.addr), r_addr, r_len);
  info.addr_len = r_len;
  info.incarnation = incarnation;
  info.attempts = 10;
  info.last_heartbeat_time = 0; // subscription is not used as a heartbeat
  info.types = 0;
  info.iocs = NULL;
  info.nets = NULL;
  info.vars = NULL;

  // no function in case it finds it, as we then throw it out
  db_add( ndb.db->subs_waiting, &sks, db_subscriber_add_adder, NULL, &info);

  return 1;
}

////////-----------///////


static int db_subscriber_del( void *data, void *arg)
{
  struct subscriberinfo *linfo = data;
  uint32_t *pincarnation = arg;

  if( linfo->incarnation != *pincarnation)
    return 0;

  free_subscriber( linfo);

  // return of 1 deletes link
  return 1;
}

// returns 1 if record deleted, 0 if it doesn't exist
static int subscriber_del( struct sockaddr_in *addr, socklen_t len,
                           uint32_t incarnation)
{
  struct subscriber_key sks;
  
  sks.addr = *addr;
  sks.addr_len = len;
  
  return db_delete( ndb.db->subs, &sks, db_subscriber_del, &incarnation);
}

////////-----------///////


struct st_subscriber_accept
{
  uint32_t incarnation;
  // return
  struct subscriberinfo *subinfo;
};

static int db_subscriber_accept_grab( void *data, void *arg)
{
  struct subscriberinfo *dsub = data;
  struct st_subscriber_accept *ssa = arg;

  // if the incarnations don't match, throw it out
  if( dsub->incarnation != ssa->incarnation)
    return 0;
    
  ssa->subinfo = dsub;

  return 1;
}

static void *db_subscriber_accept_replace( void *data, void *arg)
{
  struct subscriberinfo *dsub = data;
  struct st_subscriber_accept *ssa = arg;

  free_subscriber( dsub);

  return ssa->subinfo;
}

static void *db_subscriber_accept_add( void *arg)
{
  return ((struct st_subscriber_accept *) arg)->subinfo;
}

static int subscriber_accept( struct sockaddr_in *addr, socklen_t len,
                              uint32_t incarnation)
/* , uint32_t *first_event) */
{
  struct st_subscriber_accept ssa;
  struct subscriber_key sks;
  
  sks.addr = *addr;
  sks.addr_len = len;

  ssa.incarnation = incarnation;
  ssa.subinfo = NULL;

  db_delete( ndb.db->subs_waiting, &sks, db_subscriber_accept_grab, &ssa);
  // not found, could use return value of db_delete to check too
  if( ssa.subinfo == NULL)
    {
      uint32_t db_incarnation;
      
      if( db_find( ndb.db->subs, &sks, db_find_get_incarnation,
                   &db_incarnation) )
        return (db_incarnation == incarnation) ? 1 : 0;
      return 0;
    }

  // reset attempts as have been heard from
  ssa.subinfo->attempts = 10;

  // connection counts as a heartbeat
  ssa.subinfo->last_heartbeat_time = time(NULL);

  db_add( ndb.db->subs, &sks, db_subscriber_accept_add,
          db_subscriber_accept_replace, (void *) &ssa);

  return 1;
}

////////-----------///////

// return 1 if connection is refused
static int send_event_ack( int socket, struct sockaddr_in *addr, socklen_t len,
                           uint32_t incarnation, uint32_t event_id)
{
  uint8_t packet_buffer[16];
  uint8_t *p;  

  int ret;
  
  // make global for magic number below
  p = packet_buffer;
  *((uint32_t *) p) = htonl(0x8675309);
  p += 4;
  *((uint16_t *) p) = htons(SUBSCRIPTION_PROCOTOL_VERSION);
  p += 2;
  *((uint16_t *) p) = htons(8);
  p += 2;
  *((uint32_t *) p) = htonl(incarnation);
  p += 4;
  *((uint32_t *) p) = htonl(event_id);

  ret = sendto( socket, packet_buffer, 16, 0, (struct sockaddr *) addr, len);
  if( (ret < 0) && (errno == ECONNREFUSED) )
    return 1;
    
  if( ret != 16)
    // FIXME: use error logging
    perror( "send_event_ack sendto");

  return 0;
}



/* static void send_sub_first_event( int socket, struct sockaddr_in *addr, */
/*                                   socklen_t len, uint32_t incarnation, */
/*                                   uint32_t first_event) */
/* { */
/*   uint8_t packet_buffer[16]; */
/*   uint8_t *p;   */

/*   // make global for magic number below */
/*   p = packet_buffer; */
/*   *((uint32_t *) p) = htonl(0x8675309); */
/*   p += 4; */
/*   *((uint16_t *) p) = htons(SUBSCRIPTION_PROCOTOL_VERSION); */
/*   p += 2; */
/*   *((uint16_t *) p) = htons(2); */
/*   p += 2; */
/*   *((uint32_t *) p) = htonl(incarnation); */
/*   /\* p += 4; *\/ */
/*   /\* *((uint32_t *) p) = htonl(); *\/ */

  
/*   if( sendto( socket, packet_buffer, 16, 0, (struct sockaddr *) addr, len) */
/*       != 12) */
/*     // FIXME: use error logging */
/*     perror( "send_sub_first_event sendto"); */
/* } */


static void send_sub_ack( int socket, struct sockaddr_in *addr, socklen_t len,
                          uint32_t incarnation)
{
  uint8_t packet_buffer[12];
  uint8_t *p;  

  // make global for magic number below
  p = packet_buffer;
  *((uint32_t *) p) = htonl(0x8675309);
  p += 4;
  *((uint16_t *) p) = htons(SUBSCRIPTION_PROCOTOL_VERSION);
  p += 2;
  *((uint16_t *) p) = htons(2);
  p += 2;
  *((uint32_t *) p) = htonl(incarnation);

  
  if( sendto( socket, packet_buffer, 12, 0, (struct sockaddr *) addr, len)
      != 12)
    // FIXME: use error logging
    perror( "send_sub_ack sendto");
}


static int db_send_mass_sub_acks( void *data, void *arg)
{
  struct subscriberinfo *linfo = data;
  int *psocket = arg;

  // if out of attempts, delete this one (return 1)
  if( !linfo->attempts)
    {
      free_subscriber( linfo);
      return 1;
    }

  send_sub_ack( *psocket, &(linfo->addr), linfo->addr_len,
                linfo->incarnation);
  
  linfo->attempts--;

  return 0;
}

static void send_mass_sub_acks( int sockfd)
{
  db_walk_delete( ndb.db->subs_waiting, db_send_mass_sub_acks, &sockfd);
}


/////////-------------////////////

struct sub_list_db_struct
{
  int number;
  struct sockaddr_in *addr;
  socklen_t *addr_len;
};


void sub_list_release(struct sub_list_db_struct *sub_db)
{
  free( sub_db->addr);
  free( sub_db->addr_len);
  free( sub_db);
}

static void sub_list_get_init(void *arg, int number)
{
  struct sub_list_db_struct *slds = arg;

  slds->addr = calloc( number, sizeof( struct sockaddr_in));
  slds->addr_len = calloc( number, sizeof( socklen_t));
}

static void sub_list_get_func(void *data, void *arg)
{
  struct subscriberinfo *linfo = data;
  struct sub_list_db_struct *slds = arg;

  slds->addr[slds->number] = linfo->addr;
  slds->addr_len[slds->number] = linfo->addr_len;
  slds->number++;
}


struct sub_list_db_struct *sub_list_get(void)
{
  struct sub_list_db_struct *slds;

  slds = calloc( 1, sizeof( struct sub_list_db_struct));
  
  slds->number = 0;
  slds->addr = NULL;
  slds->addr_len = NULL;
  db_walk_init( ndb.db->subs, sub_list_get_init, sub_list_get_func,
                (void *) slds );

  return slds;
}



////////-----------///////


struct st_clear_sub_event
{
  uint32_t incarnation;
  uint32_t event_id;
  // return
  int found_flag;
};

static int lf_in_clear_sub_event( void *data, void *arg, int *stop_flag)
{
  struct event_entry *event = data;
  struct st_clear_sub_event *scse = arg;

  if( event->id == scse->event_id)
    {
      // just decrement references, drop from list, don't delete event
      event->references--;

      scse->found_flag = 1;
      
      *stop_flag = 1;
      return 1;
    }

return 0;  
}
  
static void db_clear_sub_event( void *data, void *arg)
{
  struct subscriberinfo *linfo = data;
  struct st_clear_sub_event *scse = arg;

  if( linfo->incarnation != scse->incarnation)
    return;
  

  scse->found_flag = 0;
  list_apply_delete( linfo->event_queue, lf_in_clear_sub_event, scse);
  // only reset attempts if this is actually releasing an event
  // otherwise client spam for an old event keeps it alive
  if( scse->found_flag)
    linfo->attempts = 10;

}

// returns whether the key was found, NOT the event
static int clear_sub_event( struct sockaddr_in *addr, socklen_t len,
                            uint32_t incarnation, uint32_t event_id)
{
  struct subscriber_key key;
  struct st_clear_sub_event scse;
  
  key.addr = *addr;
  key.addr_len = len;

  scse.event_id = event_id;
  scse.incarnation = incarnation;
  
  return db_find( ndb.db->subs, &key, db_clear_sub_event, &scse);
}


////////-----------///////


static void send_heartbeat_ack( int socket, struct sockaddr_in *addr,
                                socklen_t len, uint32_t incarnation)
{
  uint8_t packet_buffer[12];
  uint8_t *p;  

  // make global for magic number below
  p = packet_buffer;
  *((uint32_t *) p) = htonl(0x8675309);
  p += 4;
  *((uint16_t *) p) = htons(SUBSCRIPTION_PROCOTOL_VERSION);
  p += 2;
  *((uint16_t *) p) = htons(12);
  p += 2;
  *((uint32_t *) p) = htonl(incarnation);
      
  if( sendto( socket, packet_buffer, 12, 0, (struct sockaddr *) addr, len)
      != 12)
    // FIXME: use error logging
    perror( "send_heartbeat_ack sendto");
}


struct st_acknowledge_heartbeat
{
  int socket;
  uint32_t incarnation;
};


static void db_acknowledge_heartbeat( void *data, void *arg)
{
  struct subscriberinfo *linfo = data;
  struct st_acknowledge_heartbeat *sah = arg;
 
  // if the incarnations don't match, throw it out
  if( linfo->incarnation != sah->incarnation)
    return;

  linfo->last_heartbeat_time = time(NULL);

  send_heartbeat_ack( sah->socket , &(linfo->addr), linfo->addr_len,
                      linfo->incarnation);
}


static void acknowledge_heartbeat( int socket, struct sockaddr_in *addr,
                                   socklen_t len, uint32_t incarnation)
{
  struct subscriber_key key;

  struct st_acknowledge_heartbeat sah;
  
  key.addr = *addr;
  key.addr_len = len;

  sah.socket = socket;
  sah.incarnation = incarnation;
    
  db_find( ndb.db->subs, &key, db_acknowledge_heartbeat, &sah);
}


/////////// event stuff ////////////////////

static struct event_entry *event_builder(  char *ioc_name,
                                           struct iocinfo_ping *ping,
                                           struct iocinfo_env *env,
                                           uint8_t event_type,
                                           uint32_t event_id, 
                                           uint32_t currtime)
{
  struct event_entry *entry;

  struct netbuffer_struct nbuff;
 
  entry = malloc( sizeof( struct event_entry));
  entry->id = event_id;
  entry->references = 0;
  
  entry->expired = 0;  // IS THIS BEING USED?

  netbuffer_init( &nbuff, 1024);
  netbuffer_add_uint32( &nbuff, 0x8675309);
  netbuffer_add_uint16( &nbuff, SUBSCRIPTION_PROCOTOL_VERSION);
  netbuffer_add_uint16( &nbuff, 6);
  netbuffer_add_uint32( &nbuff, 0);  // leaving room for incarnation
  netbuffer_add_uint32( &nbuff, event_id);

  netbuffer_add_uint8( &nbuff, event_type);
  netbuffer_string_write( 1, &nbuff, ioc_name);
  netbuffer_add_uint32( &nbuff, ping->ip_address.s_addr);
  netbuffer_add_uint32( &nbuff, ping->user_msg);
  netbuffer_add_uint32( &nbuff, currtime);

  iocdb_make_netbuffer_env( &nbuff, env);

  entry->msg = netbuffer_export( &nbuff, &entry->msg_len);

  netbuffer_deinit( &nbuff);
   
  return entry;
}

static void event_remover( struct event_entry *event)
{
  free( event->msg);
  free( event);
}


////////-----------///////


static void db_in_process_events_move( void *data, void *arg)
{
  struct subscriberinfo *sinfo = data;
  struct event_entry *entry = arg;

  list_add( sinfo->event_queue, arg);
  entry->references++;
}

static void *lf_process_events_move( void *data, void *arg)
{
  // data is an event
  db_walk( ndb.db->subs, db_in_process_events_move, data);

  return data;
}


struct st_process_events_messages
{
  int socket;
  struct subscriberinfo *subinfo;
};

static void lf_in_process_events_messages_global( struct llist_global *global,
                                           void *arg)
{
  struct st_process_events_messages *spem = arg;

  // only decrement if there are events in queue
  if( global->number > 0)
    spem->subinfo->attempts--;
}

static void lf_in_process_events_messages( void *data, void *arg)
{
  struct st_process_events_messages *spem = arg;
  struct event_entry *entry = data;

  *((uint32_t *) (entry->msg + 8)) = htonl(spem->subinfo->incarnation);
  if( sendto( spem->socket, entry->msg, entry->msg_len, 0, 
              (struct sockaddr *) &(spem->subinfo->addr),
              spem->subinfo->addr_len) != entry->msg_len)
    // FIXME: use error logging
    perror( "lf_in_process_eventes_messages sendto");
}

static int db_process_events_messages( void *data, void *arg)
{
  struct subscriberinfo *sinfo = data;
  struct st_process_events_messages *spem = arg;

  // if event retries zero out, or if it's been a long time since
  // last contact (like a heartbeat), drop subscriber
  if( !sinfo->attempts || ((time(NULL) - sinfo->last_heartbeat_time) > 300) )
    {
      free_subscriber( sinfo);

      return 1;
    }

  // leave spem->socket alone
  spem->subinfo = sinfo;
  list_process( sinfo->event_queue, lf_in_process_events_messages_global,
                lf_in_process_events_messages, spem);
  
  return 0;
}

static int lf_process_events_clean( void *data, void *arg, int *stop_flag)
{
  struct event_entry *entry = data;

  if( !entry->references)
    {
      event_remover( entry);

      return 1;
    }
  
  return 0;
}

static void process_periodic( int sockfd)
{
  struct st_process_events_messages spem;

  // send out acknowledgements from subscriber attempts
  send_mass_sub_acks( sockfd);
  
  // move events from pending to active, while adding to active subscriptions
  // embedded in here is reference to global ndb.db->subs_accepted
  list_move_all( ndb.db->events_pending, ndb.db->events,
                 lf_process_events_move, NULL);
  /* ndb.db->nextsentevent_id = ndb.db->nextevent_id; */

  // send messages to subscriber list
  spem.socket = sockfd;
  db_walk_delete( ndb.db->subs, db_process_events_messages, &spem);

  // check outgoing list and remove zero referenced events
  list_apply_delete( ndb.db->events, lf_process_events_clean, NULL);
}

///////////////////////////////


// this is a bit of overkill, but I'll leave for now
struct st_service_subscribers
{
  int socket;
};


// loop thread

static void *th_service_subscribers( void *data)
{
  struct st_service_subscribers *sss;

  uint32_t incarnation, /* request_incarnation, */ event_id;

  int sockfd;

  fd_set rset;
  int retval;
  struct timeval tv;

  int request;

  /* uint32_t first_event; */
  
  //  int data_length;
  char data_buffer[4096];
  ssize_t buff_len;
  
  struct sockaddr_in r_addr;
  socklen_t r_len;

  char *p;

  struct timeval now;
  struct timeval timer;


  sss = data;
  sockfd = sss->socket;

  gettimeofday( &timer, NULL);
  while(1)
    {
      FD_ZERO( &rset);
      FD_SET( sockfd, &rset);
      // can change timer to change granularity
      tv.tv_sec = 0;
      tv.tv_usec = 100000;
      retval = select( sockfd + 1, &rset, NULL, NULL, &tv);
      if( retval < 0)
        continue;
      gettimeofday( &now, NULL);

      if( timediff_msec( timer, now) > 200)
        {
          process_periodic( sockfd);
          timer = now;
        }

      if( retval && FD_ISSET( sockfd, &rset) )
        {
          r_len = sizeof( struct sockaddr_in);
          // should really read recvfrom until empty
          //          data_length =
          buff_len = recvfrom( sockfd, (void *) data_buffer,
                               4096 * sizeof( char), 0,
                               (struct sockaddr *) &r_addr, &r_len);
          if( buff_len < 12) // minimum size
            continue;
          
          p = data_buffer;
          if( ntohl( *((uint32_t *) p)) != 0x8675309)
            {
              printf("Bad magic number: %d.\n", ntohl( *((uint32_t *) p)));
              continue;
            }
          p += 4;
          if( ntohs( *((uint16_t *) p)) != SUBSCRIPTION_PROCOTOL_VERSION )
            continue;
          p += 2;
          request = ntohs( *((uint16_t *) p));
          p += 2;
          incarnation = ntohl( *((uint32_t *) p));
          p += 4;
          switch( request )
            {
            case 1:
              // if an entry exists and if the incarnation desn't match,
              // it gets thrown out.  If previous dies, it will time out.

              if( subscriber_add( &r_addr, r_len, incarnation) )
                send_sub_ack( sockfd, &r_addr, r_len, incarnation);
              break;
              
            case 3:
              subscriber_accept( &r_addr, r_len, incarnation);
              /* if( subscriber_accept( &r_addr, r_len, incarnation, */
              /*                        &first_event) ) */
              /*   send_sub_first_event( sockfd, &r_addr, r_len, incarnation, */
              /*                         first_event); */
              break;

            case 7: // event ack
              if( buff_len < 16) // minimum size
                continue;

              event_id = ntohl( *((uint32_t *) p));
              p += 4;

//              printf("Received event ack\n");
              
              // an ack will be sent if the subscription is valid,
              // but ignoring if the event is still found
              // as packets could have been lost
              if( clear_sub_event( &r_addr, r_len, incarnation, event_id) )
                {
//                  printf("event cleared\n");
                  if( send_event_ack( sockfd, &r_addr, r_len, incarnation,
                                      event_id) )
                    {
//                      printf("event ack: connection refused\n");
                      subscriber_del( &r_addr, r_len, incarnation);
                    }
                }
              break;

            case 11:

              acknowledge_heartbeat( sockfd, &r_addr, r_len, incarnation);
              break;   
            }
        }
    }

  free(data);

  return NULL;
}








int notifydb_start(void)
{
  int sockfd;
  int flag;

  struct sockaddr_in ip_addr;
  struct st_service_subscribers *sss;

  pthread_attr_t attr;

  ////////////////////////////////////////

  // Initialize the local global database structure

  ndb.db = malloc( sizeof( struct notifydb));
  
  ndb.db->subs = db_create( sub_key_copy, free, sub_key_compare, 1);
  // llrb tree overkill for _waiting, but want to use same interface
  ndb.db->subs_waiting = db_create( sub_key_copy, free, sub_key_compare, 1);;

  ndb.db->events_pending = list_create( );
  ndb.db->events = list_create( );

  // don't start with zero
  ndb.db->nextevent_id = 1;
  /* ndb.db->nextsentevent_id = 1; */

  ///////////////////////////////////////////////


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
  ip_addr.sin_port = htons(config.subscription_udp_port);
  if( bind(sockfd, (struct sockaddr *) &ip_addr, sizeof( ip_addr) ))
    {
      log_error_write(errno, "UDP bind");
      return 1;
    }

  sss = malloc( sizeof( struct st_service_subscribers) );
  sss->socket = sockfd;
         
  if( pthread_attr_init(&attr) )
    return 1;
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&ndb.service_thread, &attr, th_service_subscribers,
                 (void *) sss );
  pthread_attr_destroy( &attr);
  
  return 0;
}

int notifydb_stop(void)
{
  int returner;
  
  returner = pthread_cancel( ndb.service_thread);

  // At this point, I can free up all the memory.
  // Not really necessary, as the daemon shuts down.
  
  /* if( !returner) */
  /*   printf("shut down notifier thread!\n"); */
  
  return returner;
}


void notifydb_report_event( char *ioc_name, struct iocinfo_ping ping,
                            struct iocinfo_env *env, int event_type, 
                            uint32_t currtime)
{
  struct event_entry *entry;

  entry = event_builder( ioc_name, &ping, env, event_type,
                         ndb.db->nextevent_id, currtime);
  list_add( ndb.db->events_pending, (void *) entry);

  // increment the number for the next event   
  if( ndb.db->nextevent_id == UINT_MAX)  // this would happen in 13 years
    ndb.db->nextevent_id = 1;  // skip zero
  else
    ndb.db->nextevent_id++;
}


// sends to control program the list of event subscribers
void notifyds_send_subscribers_control( int socket)
{
  struct sub_list_db_struct *slds;

  char addr_string[16];
  char buffer[32];
  
  int i, num;

  slds = sub_list_get();
  for( i = 0; i < slds->number; i++)
    {
      address_to_string( addr_string,
                         (uint32_t) slds->addr[i].sin_addr.s_addr);
      send( socket, addr_string, strlen(addr_string), 0);
      num = sprintf( buffer, " - %d\n", slds->addr[i].sin_port);
      if( num > 0)
        send( socket, buffer, num, 0);
    }
  sub_list_release( slds);
}
