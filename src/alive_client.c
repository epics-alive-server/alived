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
#include <stdint.h>

//#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>

#include "alive_client.h"
#include "alive_version.h"


// defined in bytes
#define EVENT_SIZE (4) 


#ifndef DEF_SERVER
  #define DEF_SERVER ""
#endif
#ifndef DEF_DB_PORT
  #define DEF_DB_PORT 5679
#endif

#define CLIENT_PROTOCOL_VERSION (4)


enum BufferType { Buffer_Unknown, Buffer_External, Buffer_Copy, Buffer_Socket };

struct buffer_struct
{
  int type;    
  char *buffer;     
  int amount;  // amount of data actually read into buffer
  int offset;  // offset into buffer of "sent" data

  // next are needed for Buffer_Socket only
  int socket;
  int buffer_size;  // maximum amount of data that can be read at a time
};


static int get_server_addr(char *server, int port, int *sockfd)
{
  char port_str[8];

  int status;
  struct addrinfo hints;
  struct addrinfo *servinfo;  // will point to the results

  struct sockaddr_in *ipv4;

  struct sockaddr_in r_addr;
  int result;
  
  int lsockfd;
  
   
  snprintf( port_str, 10, "%d", port);
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;      
  hints.ai_socktype = SOCK_STREAM;
  
  if( (status = getaddrinfo(server, port_str, &hints, &servinfo)) != 0 )
    {
      printf( "getaddrinfo error: %s\n", gai_strerror(status));
      return 1;
    }
  ipv4 = (struct sockaddr_in *)servinfo->ai_addr;

  bzero( &r_addr, sizeof(r_addr) );
  r_addr.sin_family = AF_INET;
  r_addr.sin_port = htons(port);
  r_addr.sin_addr = ipv4->sin_addr;
  
  freeaddrinfo(servinfo); // free the linked-list

  lsockfd = socket(AF_INET, SOCK_STREAM, 0);

  result = connect(lsockfd, (struct sockaddr *)&r_addr, sizeof(r_addr) );
  if( result == -1)
    {
      printf( "Can't connect to server!\n");
      return 1;
    }

  *sockfd = lsockfd;
  
  return 0;
}




/* static struct buffer_struct *init_buffer_data( char *buffer, int buffer_size, int copy_flag) */
/* { */
/*   struct buffer_struct *bs; */

/*   bs = calloc( 1, sizeof(struct buffer_struct)); */
/*   if( bs == NULL) */
/*     return NULL; */
  
/*   if( copy_flag) */
/*     { */
/*       bs->buffer = malloc( sizeof(char) * buffer_size); */
/*       if( bs->buffer == NULL) */
/*         { */
/*           free( bs); */
/*           return NULL; */
/*         } */
/*       memcpy( bs->buffer, buffer, buffer_size); */
/*       bs->type = Buffer_Copy; */
/*     } */
/*   else */
/*     { */
/*       bs->buffer = buffer; */
/*       bs->type = Buffer_External; */
/*     } */
/*   bs->amount = buffer_size; */
  
/*   // sd->offset set to zero by calloc() */

/*   return bs; */
/* } */

static struct buffer_struct *init_buffer_stream( int socket, int buffer_size)
{
  struct buffer_struct *bs;
  int ns;

  ns = 2;
  while( ns < buffer_size)
    ns <<= 1;
  buffer_size = ns;

  bs = calloc( 1, sizeof(struct buffer_struct));
  if( bs == NULL)
    return NULL;
  bs->buffer = calloc( buffer_size, sizeof( char));
  if( bs->buffer == NULL)
    {
      free(bs);
      return NULL;
    }

  bs->type = Buffer_Socket;
  bs->socket = socket;
  bs->buffer_size = buffer_size;

  // sd->amount and sd->offset set to zero by calloc()

  return bs;
}

static void free_buffer( struct buffer_struct *bs)
{
  if( (bs->type == Buffer_Copy) || (bs->type == Buffer_Socket) )
    free( bs->buffer);
  free( bs);
}

static int load_buffer( struct buffer_struct *bs, int number)
{
  int left;
  int addlen;

  left = bs->amount - bs->offset;

  if( (left < number) && (bs->type == Buffer_Socket))
    {
      // can do realloc instead
      if( number > bs->buffer_size)
        {
          char *p;
          int ns;

          ns = bs->buffer_size;
          do
            ns <<= 1;
          while( ns < number);

          p = realloc( bs->buffer, ns);
          if( p == NULL)
            // can't add more, but can at least fill the buffer
            number = bs->buffer_size;
          else
            {
              bs->buffer_size = ns;
              bs->buffer = p;
            }
        }

      // move remaining data to front of buffer
      if( left) 
        memmove( bs->buffer, &(bs->buffer[bs->offset]), left);
      bs->amount = left;
      bs->offset = 0;
      
      while( bs->amount < number) 
        {
          addlen = read( bs->socket, &(bs->buffer[bs->amount]), 
                         bs->buffer_size - bs->amount);
          bs->amount += addlen;
          if( !addlen)
            break;
        }

      return bs->amount;
    }

  return left;
}

// this just tests whether a length of number can be read
static int load_buffer_test( struct buffer_struct *bs, int number)
{
  return load_buffer(bs, number) < number;
}

// get access to databuffer and it's current length of unread data
static int get_buffer_dataptr( struct buffer_struct *bs, int number, 
                               char **dptr, int *retnumber)
{
  *retnumber = load_buffer( bs, number);
  *dptr = &(bs->buffer[bs->offset]);

  bs->offset += *retnumber;

  return *retnumber != number;
}

static int get_buffer_uint8( struct buffer_struct *bs, uint8_t *val)
{
  if( load_buffer_test( bs, sizeof(uint8_t)))
    return 1;

  *val = *((uint8_t *) &(bs->buffer[bs->offset]) );
  bs->offset += sizeof(uint8_t);

  return 0;
}

static int get_buffer_uint16( struct buffer_struct *bs, uint16_t *val)
{
  if( load_buffer_test( bs, sizeof(uint16_t)))
    return 1;

  *val = ntohs( *((uint16_t *) &(bs->buffer[bs->offset]) ));
  bs->offset += sizeof(uint16_t);

  return 0;
}

static int get_buffer_uint32( struct buffer_struct *bs, uint32_t *val)
{
  if( load_buffer_test( bs, sizeof(uint32_t)))
    return 1;

  *val = ntohl( *((uint32_t *) &(bs->buffer[bs->offset]) ));
  bs->offset += sizeof(uint32_t);

  return 0;
}



// can only do 1, 2, or 4 bytes
static char *get_buffer_string( struct buffer_struct *bs, int bytes)
{
  int len;

  char *p;

  if( load_buffer_test( bs, bytes))
    return NULL;

  switch( bytes)
    {
    case 1:
      len = *((uint8_t *) &(bs->buffer[bs->offset]) );
      bs->offset += sizeof(uint8_t);
      break;
    case 2:
      len =  ntohs( *((uint16_t *) &(bs->buffer[bs->offset]) ));
      bs->offset += sizeof(uint16_t);
      break;
    case 4:
      len = ntohl( *((uint32_t *) &(bs->buffer[bs->offset]) ));
      bs->offset += sizeof(uint32_t);
      break;
    default:
      return NULL;
    }

  if( load_buffer_test( bs, len))
    return NULL;

  // if returns NULL, that gets returned below
  p = strndup( &(bs->buffer[bs->offset]), len);
  bs->offset += len;

  return p;
}


static struct alive_env *get_environment( struct buffer_struct *bs)
{
  struct alive_env *env;

  int i;

  uint8_t data_exists;

  if( get_buffer_uint8( bs, &data_exists) || (data_exists == 0) )
    return NULL;


  env = calloc( 1, sizeof( struct alive_env));
  if( env == NULL)
    return NULL;

  if( get_buffer_uint16( bs, &env->number_envvar) )
    return NULL;
  env->envvar_key = calloc( env->number_envvar, sizeof( char *));
  env->envvar_value = calloc( env->number_envvar, sizeof( char *));
  for( i = 0; i < env->number_envvar; i++)
    {
      env->envvar_key[i] = get_buffer_string( bs, 1);
      env->envvar_value[i] = get_buffer_string( bs, 2);
    }

  if( get_buffer_uint16( bs, &env->extra_type) )
    return NULL;
  switch( env->extra_type)
    {
    case VXWORKS:
      {
        struct alive_iocinfo_extra_vxworks *vw;

        vw = env->extra = calloc( 1, sizeof(struct alive_iocinfo_extra_vxworks) );
        if( vw == NULL)
          goto Error1;
          
        vw->bootdev = get_buffer_string( bs, 1);
        if( get_buffer_uint32( bs, &vw->unitnum) )
          return NULL;
        if( get_buffer_uint32( bs, &vw->procnum) )
          return NULL;
        vw->boothost_name = get_buffer_string( bs, 1);
        vw->bootfile = get_buffer_string( bs, 1);
        vw->address = get_buffer_string( bs, 1);
        vw->backplane_address = get_buffer_string( bs, 1);
        vw->boothost_address = get_buffer_string( bs, 1);
        vw->gateway_address = get_buffer_string( bs, 1);
        if( get_buffer_uint32( bs, &vw->flags) )
          return NULL;
        vw->target_name = get_buffer_string( bs, 1);
        vw->startup_script = get_buffer_string( bs, 1);
        vw->other = get_buffer_string( bs, 1);
      }
      break;
    case LINUX:
      {
        struct alive_iocinfo_extra_linux *lnx;
                
        lnx = env->extra = calloc( 1, sizeof(struct alive_iocinfo_extra_linux) );
        if( lnx == NULL)
          goto Error1;
          
        lnx->user = get_buffer_string( bs, 1);
        lnx->group = get_buffer_string( bs, 1);
        lnx->hostname = get_buffer_string( bs, 1);
      }
      break;
    case DARWIN:
      {
        struct alive_iocinfo_extra_darwin *dar;

        dar = env->extra = calloc( 1, sizeof(struct alive_iocinfo_extra_darwin) );
        if( dar == NULL)
          goto Error1;
          
        dar->user = get_buffer_string( bs, 1);
        dar->group = get_buffer_string( bs, 1);
        dar->hostname = get_buffer_string( bs, 1);
      }
      break;
    case WINDOWS:
      {
        struct alive_iocinfo_extra_windows *win;
                
        win = env->extra = calloc( 1, sizeof(struct alive_iocinfo_extra_windows) );
        if( win == NULL)
          goto Error1;
                
        win->user = get_buffer_string( bs, 1);
        win->machine = get_buffer_string( bs, 1);
      }
      break;
    }

  return env;

 Error1:
  free( env);

  return NULL;
}


struct alive_db *alive_get_iocs( char *server, int port, int number, 
                                 char **names)
{
  struct alive_db *db;
  struct alive_ioc *ioc;

  struct buffer_struct *bs;
  int sockfd;

  uint16_t version;

  uint32_t o32;
  uint16_t o16;
  uint8_t o8;

  int i;

  if( get_server_addr( server, port, &sockfd) )
    return NULL;


 
  // only works for INCOMING stream
  bs = init_buffer_stream( sockfd, 1024);
  if( bs == NULL)
    {
      printf("Can't create socket buffer!\n");
      return NULL;
    }

  db = malloc( sizeof( struct alive_db) );

  if( !number) // all of them
    {
      o16 = htons(1);
      write( sockfd, &o16, sizeof(o16));
    }
  else if( number != 1) // some of them
    {
      o16 = htons(2);
      write( sockfd, &o16, sizeof(o16));

      o16 = htons( number);
      write( sockfd, &o16, sizeof(o16));

      for( i = 0; i < number; i ++)
        {
          o8 = strlen( names[i]);
          write( sockfd, &o8, sizeof(o8));
          write( sockfd, names[i], o8);
        }
    }
  else // one of them
    {
      o16 = htons(3);
      write( sockfd, &o16, sizeof(o16));

      o8 = strlen( *names);
      write( sockfd, &o8, sizeof(o8));
      write( sockfd, *names, o8);
    }

  shutdown( sockfd, SHUT_WR);

 
  if( load_buffer_test(bs, 12) )
    goto Error;
  get_buffer_uint16( bs, &version);
  if( version != CLIENT_PROTOCOL_VERSION)
    {
      printf("Unable to handle this protocol version.\n");
      goto Error;
    }
  get_buffer_uint32( bs, &o32);
  db->current_time = o32;
  get_buffer_uint32( bs, &o32);
  db->start_time = o32;
  get_buffer_uint16( bs, &db->number_ioc);

  db->ioc = calloc( db->number_ioc, sizeof( struct alive_ioc) );
  for( i = 0; i < db->number_ioc; i++)
    {
      ioc = &(db->ioc[i]);

      if((ioc->ioc_name = get_buffer_string( bs, 1)) == NULL)
        {
          printf("Missing data.\n");
          goto Error;
        }

      if( load_buffer_test(bs, 13) )
        {
          printf("Missing data.\n");
          goto Error;
        }
      get_buffer_uint8( bs, &ioc->status);
      get_buffer_uint32( bs, &o32);
      ioc->time_value = o32;
      get_buffer_uint32( bs, &ioc->raw_ip_address);
      get_buffer_uint32( bs, &ioc->user_msg);

      ioc->environment = get_environment( bs);
    }

  shutdown( sockfd, SHUT_RD);
  close(sockfd);

  free_buffer( bs);

  return db;

 Error:

  shutdown( sockfd, SHUT_RD);
  close(sockfd);

  return NULL;
}

struct alive_db *alive_get_db( char *server, int port)
{
  return alive_get_iocs( server, port, 0, NULL);
}

struct alive_db *alive_get_ioc( char *server, int port, char *name)
{
  return alive_get_iocs( server, port, 1, &name);
}

void alive_free_env(  struct alive_env *env )
{
  int j;

  if( env == NULL)
    return;

  for( j = 0; j < env->number_envvar; j++)
    {
      free( env->envvar_key[j]);
      free( env->envvar_value[j]);
    }
  free( env->envvar_key);
  free( env->envvar_value);
  
  switch(env->extra_type)
    {
    case VXWORKS:
      {
        struct alive_iocinfo_extra_vxworks *vw;

        vw = env->extra;
        free(vw->bootdev);
        free(vw->boothost_name);
        free(vw->bootfile);
        free(vw->address);
        free(vw->backplane_address);
        free(vw->boothost_address);
        free(vw->gateway_address);
        free(vw->target_name);
        free(vw->startup_script);
        free(vw->other);
        free(vw);
      }
      break;
    case LINUX:
      {
        struct alive_iocinfo_extra_linux *lnx;

        lnx = env->extra;
        free(lnx->user);
        free(lnx->group);
        free(lnx->hostname);
        free(lnx);
      }
      break;
    case DARWIN:
      {
        struct alive_iocinfo_extra_darwin *dar;

        dar = env->extra;
        free(dar->user);
        free(dar->group);
        free(dar->hostname);
        free(dar);
      }
      break;
    case WINDOWS:
      {
        struct alive_iocinfo_extra_windows *win;
                
        win = env->extra;
        free(win->user);
        free(win->machine);
        free(win);
      }
      break;
    }
  free( env);
}

void alive_free_ioc( struct alive_ioc *ioc)
{
  free( ioc->ioc_name);
  alive_free_env( ioc->environment);
}

void alive_free_db( struct alive_db *iocs)
{
  int i;
  
  for( i = 0; i < iocs->number_ioc; i++)
    {
      /* free( iocs->ioc[i].ioc_name); */
      /* free_alive_env( iocs->ioc[i].environment); */
      alive_free_ioc( &(iocs->ioc[i]) );
    }
  free(iocs->ioc);
  free(iocs);
}



/* // TEMPORARY FUNCTION! */
/* // Will disappear when events added to API */
/* static struct alive_env *unpack_alive_env( char *data, int length) */
/* { */
/*   struct buffer_struct *bs; */
/*   struct alive_env *env; */

/*   bs = init_buffer_data( data, length, 0); */
/*   env = get_environment( bs); */
/*   free_buffer( bs); */

/*   return env; */
/* } */


///////////////////////////////////////////////////////////////////

struct alive_detailed_ioc *alive_get_detailed( char *server, int port, 
                                               char *name, int type)
{
  struct alive_detailed_ioc *dioc;
  struct alive_instance *inst;

  struct buffer_struct *bs;
  int sockfd;

  uint16_t version;

  uint32_t o32;
  uint16_t o16;
  uint8_t o8;

  int i;


  if( get_server_addr( server, port, &sockfd) )
    return NULL;

  // only works for INCOMING stream
  bs = init_buffer_stream( sockfd, 1024);
  if( bs == NULL)
    {
      printf("Can't create socket buffer!\n");
      return NULL;
    }

  if( !type)
    o16 = htons(21);
  else
    o16 = htons(22);
  write( sockfd, &o16, sizeof(o16));

  o8 = strlen( name);
  write( sockfd, &o8, sizeof(o8));
  write( sockfd, name, o8);

  shutdown( sockfd, SHUT_WR);
 
  dioc = calloc( 1, sizeof( struct alive_detailed_ioc) );


  if( load_buffer_test(bs, 10) )
    return NULL;
  get_buffer_uint16( bs, &version);
  if( version != CLIENT_PROTOCOL_VERSION)
    {
      printf("Unable to handle this protocol version.\n");
      return NULL;
    }
  get_buffer_uint32( bs, &o32);
  dioc->current_time = o32;
  get_buffer_uint32( bs, &o32);
  dioc->start_time = o32;


  if( (dioc->ioc_name = get_buffer_string( bs, 1)) == NULL)
    return NULL;
  if( load_buffer_test(bs, 9) )
    return NULL;
  get_buffer_uint8( bs, &dioc->overall_status);
  get_buffer_uint32( bs, &o32);
  dioc->overall_time_value = o32;
  get_buffer_uint32( bs, &dioc->number_instances);


  dioc->instances = calloc( dioc->number_instances,
                            sizeof(struct alive_instance) );
  inst = dioc->instances;
  for( i = 0; i < dioc->number_instances; i++)
    {
      if( load_buffer_test(bs, 31) )
        return NULL;
      get_buffer_uint8( bs, &inst->status);
      get_buffer_uint32( bs, &inst->raw_ip_address);
      get_buffer_uint16( bs, &inst->origin_port);
      get_buffer_uint32( bs, &inst->heartbeat);
      get_buffer_uint16( bs, &inst->period);
      get_buffer_uint32( bs, &inst->incarnation);
      get_buffer_uint32( bs, &o32);
      inst->boottime = o32;
      get_buffer_uint32( bs, &o32);
      inst->timestamp = o32;
      get_buffer_uint16( bs, &inst->reply_port);
      get_buffer_uint32( bs, &inst->user_msg);
      

      inst->environment = get_environment( bs);

      inst++;
    }

  shutdown( sockfd, SHUT_RD);
  close(sockfd);
  
  free_buffer( bs);

  return dioc;
}


struct alive_detailed_ioc *alive_get_debug( char *server, int port, 
                                            char *name)
{
  return alive_get_detailed( server, port, name, 0);
}

struct alive_detailed_ioc *alive_get_conflicts( char *server, int port, 
                                                char *name)
{
  return alive_get_detailed( server, port, name, 1);
}


void alive_free_detailed( struct alive_detailed_ioc *ioc)
{
  int i;

  free( ioc->ioc_name);

  for( i = 0; i < ioc->number_instances; i++)
    alive_free_env( ioc->instances[i].environment);
  free( ioc->instances);
  free( ioc);
}


///////////////////////////////////////////////////////////////////


struct alive_ioc_event_db *alive_get_ioc_event_db( char *server, int port, char *name)
{
  struct alive_ioc_event_db *events;
  struct alive_ioc_event_item *items;

  struct buffer_struct *bs;
  int sockfd;

  uint32_t *data;
  char *dptr;
  int data_count;
  int chunk_size;
  int data_max;
  int length;
  int ret;

  uint16_t version;

  uint32_t o32;
  uint16_t o16;
  uint8_t o8;

  int i;


  if( get_server_addr( server, port, &sockfd) )
    return NULL;
 
  // only works for INCOMING stream
  bs = init_buffer_stream( sockfd, 1024);
  if( bs == NULL)
    {
      printf("Can't create socket buffer!\n");
      return NULL;
    }

  events = malloc( sizeof( struct alive_ioc_event_db) );

  o16 = htons(15);
  write( sockfd, &o16, sizeof(o16));

  o8 = strlen( name);
  write( sockfd, &o8, sizeof(o8));
  write( sockfd, name, o8);

  shutdown( sockfd, SHUT_WR);

  
  // COULD switch this to BUFFER MODE

  if( load_buffer_test(bs, 10) )
    return NULL;
  get_buffer_uint16( bs, &version);
  if( version != CLIENT_PROTOCOL_VERSION)
    {
      printf("Unable to handle this protocol version.\n");
      return NULL;
    }
  get_buffer_uint32( bs, &o32);
  events->current_time = o32;
  get_buffer_uint32( bs, &o32);
  events->start_time = o32;


  data_count = 0;
  data_max = 256;
  chunk_size = EVENT_SIZE*sizeof(uint32_t);

  length = load_buffer( bs, data_max*chunk_size);
  data_count = length/chunk_size;
  while( data_count == data_max )
    {
      data_max *= 2;
      length = load_buffer( bs, data_max*chunk_size);
      data_count = length/chunk_size;
    }

  get_buffer_dataptr( bs, length, &dptr, &ret);

  shutdown( sockfd, SHUT_RD);
  close(sockfd);

  data = (uint32_t *) dptr;
  events->number = ret/chunk_size;
  events->instances = malloc( events->number * sizeof( struct alive_ioc_event_item) );
  items = events->instances;


  for( i = 0; i < events->number; i++)
    {
      items[i].time = *(data++);
      items[i].raw_ip_address = *(data++);
      items[i].user_msg = *(data++);
      items[i].event = *(data++);

      /* if( (items[i].event > 6) || (items[i].event < 0) ) */
      /*   printf("WARNING! %d\n", items[i].event); */
    }

  fflush(stdout);

  free_buffer( bs);

  return events;
}


void alive_free_ioc_event_db( struct alive_ioc_event_db *events)
{
  if (events!= NULL)
    {
      free( events->instances);
      free( events);
    }
}


char *alive_default_database( int *port)
{
  char *lhost;
  int lport;

  //  char *envstr;
  
  lhost = DEF_SERVER;
  lport = DEF_DB_PORT;

  //  envstr = getenv("ALIVE_DEFAULT");
  // fill this out for parsing this environment variable

  *port = lport;
  return strdup( lhost);
}


char *alive_client_api_version( void)
{
  return strdup( ALIVE_SYSTEM_VERSION);
}

