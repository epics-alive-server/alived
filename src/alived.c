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
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#include <netdb.h>
#include <pthread.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <sys/un.h>

#include <unistd.h>
//#include <dirent.h>

#include <arpa/inet.h>

#include <time.h>
//#include <fcntl.h>


#include "alived.h"
#include "alive_version.h"
#include "iocdb.h"
#include "iocdb_access.h"
#include "notifydb.h"
#include "utility.h"
#include "logging.h"
#include "config_parse.h"




/////////////////////////////////////////////

struct alived_config config;

volatile sig_atomic_t signal_exit = 0;


////////////////////////////
// Local global


static struct config_dictionary *dict;

//////////////////////////

struct client_reply_struct
{
  int socket;
  time_t starttime;
};

void *client_reply( void *data)
{
  int socket;
  time_t starttime;


  int client_sockfd;
  struct sockaddr_in r_addr;
  socklen_t r_len;

  char **names;
  char *name;

  uint16_t type;
  uint16_t number;
  uint32_t now;
  
  uint16_t o16;
  uint32_t o32;

  int i;

  socket = ( (struct client_reply_struct *) data)->socket;
  starttime = ( (struct client_reply_struct *) data)->starttime;
  free( data);

  while(1)
    {
      r_len = sizeof(r_addr);
      client_sockfd =
        accept(socket, (struct sockaddr *)&r_addr, &r_len);
      if( client_sockfd == -1)
        {
          log_error_write(errno, "TCP accept");
          continue;
        }

      //      set to non-blocking
      fcntl(client_sockfd, F_SETFL, fcntl(client_sockfd, F_GETFL) | O_NONBLOCK);

      if( socket_reader( client_sockfd, sizeof(uint16_t), (void *) &o16) )
        goto error;
      type = ntohs( o16);

      now = time(NULL);

      o16 = htons( API_PROCOTOL_VERSION); //version
      write( client_sockfd, &o16, sizeof(o16));
      o32 = htonl( now);
      write( client_sockfd, &o32, sizeof(o32));
      o32 = htonl( starttime);
      write( client_sockfd, &o32, sizeof(o32));
      switch( type)
        {
        case 1:
          iocdb_socket_send_all( client_sockfd);
          break;
        case 2:
          names = net_string_array_grab( 2, 1, client_sockfd, &number);
          if( names == NULL)
            {
              log_write( "client_reply: (2) bad ioc names.\n");
              break;
            }
          iocdb_socket_send_multi( client_sockfd, number, names);
          for( i = 0; i < number; i++)
            free( names[i]);
          free( names);
          break;
        case 3:
          name = net_string_grab( 1, client_sockfd);
          if( name == NULL)
            {
              log_write( "client_reply: (3) bad ioc name.\n");
              break;
            }
          iocdb_socket_send_single( client_sockfd, name);
          free(name);
          break;

        case 15:
          name = net_string_grab( 1, client_sockfd);
          if( name == NULL)
            {
              log_write( "client_reply: (15) bad ioc name.\n");
              break;
            }
          event_file_send( name, client_sockfd);
          free(name);
          break;
        case 21:
          name = net_string_grab( 1, client_sockfd);
          if( name == NULL)
            {
              log_write( "client_reply: (21) bad ioc name.\n");
              break;
            }
          iocdb_socket_send_debug( client_sockfd, name);
          free(name);
          break;
        case 22:
          name = net_string_grab( 1, client_sockfd);
          if( name == NULL)
            {
              log_write( "client_reply: (22) bad ioc name.\n");
              break;
            }
          iocdb_socket_send_conflicts( client_sockfd, name);
          free(name);
          break;
        default:
          log_write( "client_reply: bad type.\n");
        }

    error:

      shutdown( client_sockfd, SHUT_RDWR);
      close( client_sockfd);
    }
  
  return NULL;
}



/////////////////////////////


//void test_func( int socket);

////////////////////////////////////////

#define BUFSIZE (4096)
#define MAXTEXT (64) // reserve room for replying with original text

/* 
   alived monitors a UNIX socket for traffic from a controlling client.
   This client by nature of the UNIX socket has to be local and be allowed
   to write to the socket.
*/
int control_loop(void )
{
  int ctl_sockfd;
  struct sockaddr_un ctl_addr;

  int c_sockfd;
  struct sockaddr_in c_addr;
  socklen_t c_len;
  char buffer[BUFSIZE];
  char tbuffer[BUFSIZE];

  int len, addlen;

  int exit_flag = 0;

  int cnt;
  
  ctl_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  //  bzero( &ctl_addr, sizeof(ctl_addr) );
  ctl_addr.sun_family = AF_UNIX;
  strcpy( ctl_addr.sun_path, config.control_socket);
  unlink( ctl_addr.sun_path);
  len = strlen( ctl_addr.sun_path) + sizeof( ctl_addr.sun_family);
  if( bind(ctl_sockfd, (struct sockaddr *) &ctl_addr, len) )
    {
      log_error_write(errno, "UNIX domain bind");
      return 1;
    }
  if( listen(ctl_sockfd, 5) )
    {
      log_error_write(errno, "UNIX domain listen");
      return 1;
    }


  while(!exit_flag)
    {
      c_len = sizeof(c_addr);
      if( signal_exit)
        return 0;
      c_sockfd = accept(ctl_sockfd, (struct sockaddr *)&c_addr, &c_len);
      if( c_sockfd == -1)
        {
          if( errno == EINTR)
            continue;  // signal_exit will be caught at top of loop

          log_error_write(errno, "UNIX domain accept");
          return 1;
        }

      len = 0;
      while( (addlen = read( c_sockfd, buffer + len,
                             (BUFSIZE - MAXTEXT) - len)) > 0)
        len += addlen;
      if( len == (BUFSIZE - MAXTEXT) )  // this should never happen
        {
          cnt = snprintf(tbuffer, BUFSIZE, "Command too long: \"%s\".\n",
                         buffer);
          send(c_sockfd, tbuffer, cnt + 1, 0);

          shutdown( c_sockfd, SHUT_RDWR);
          close( c_sockfd);
          continue;
        }
      buffer[len] = '\0';

      if( !strcmp( buffer, "quit") )
        {
          cnt = sprintf(buffer, "shutting down\n" );
          send(c_sockfd, buffer, cnt + 1, 0);

          exit_flag = 1;
        }
      else if( !strcmp( buffer, "version") )
        {
          cnt = sprintf(buffer, "version %s\n", ALIVE_SYSTEM_VERSION);
          send(c_sockfd, buffer, cnt + 1, 0);
        }
      else if( !strcmp( buffer, "ping") )
        {
          cnt = sprintf(buffer, "hello\n" );
          send(c_sockfd, buffer, cnt + 1, 0);
        }
      else if( !strcmp( buffer, "stats") )
        {
          cnt = sprintf(buffer, "%d IOCs\n", iocdb_number_iocs() );
          send(c_sockfd, buffer, cnt + 1, 0);
        }
      else if( !strcmp( buffer, "configuration") )
        {
          int i;
          char *b;
          
          cnt = strlen(dict->filename) + 1;
          for( i = 0; i < dict->count; i++)
            cnt += (strlen(dict->key[i]) + strlen(dict->value[i]) + 6);
          if( cnt > BUFSIZE)
            b = malloc( cnt*sizeof(char) );
          else
            b = buffer;
          if( b != NULL)
            {
              cnt = sprintf(b, "%s\n", dict->filename);
              for( i = 0; i < dict->count; i++)
                cnt += sprintf(&b[cnt], "  %s = %s\n",
                               dict->key[i], dict->value[i] );
              send(c_sockfd, b, cnt + 1, 0);
              if( b != buffer)
                free( b);
            }
        }
      else if( !strcmp( buffer, "list") )
        {
          iocdb_socket_send_control_list( c_sockfd);
        }
      else if( !strcmp( buffer, "subscribers") )
        {
          notifyds_send_subscribers_control( c_sockfd);
        }
      else if( sscanf( buffer, "info %s", tbuffer) )
        {
          /* sprintf(buffer, "%d IOCs", iocdb_number_iocs() ); */
          /* send(c_sockfd, buffer, strlen(buffer)+1, 0); */
          if( !iocdb_socket_send_control_ioc( c_sockfd, tbuffer) )
            {
              cnt = snprintf(buffer, BUFSIZE, "\'%s\' not found\n", tbuffer );
              send(c_sockfd, buffer, cnt + 1, 0);
            }            
        }
      else if( sscanf( buffer, "delete %s", tbuffer) )
        {
          /* char *filename; */
          char *p;
          int safe = 1;

          // check to make sure ioc name is safe, as used for unlink()
          p = tbuffer;
          while( *p != '\0')
            {
              if( !isalnum(*p) && (*p != '_') && (*p != '-') && (*p != ':') )
                {
                  safe = 0;
                  break;
                }
              p++;
            }
          if( safe)
            {
              if( !iocdb_remove( tbuffer, 1) )
                cnt = snprintf(buffer, BUFSIZE, "\'%s\' not found\n", tbuffer );
              else
                cnt = snprintf(buffer, BUFSIZE, "\'%s\' deleted\n", tbuffer );
              send(c_sockfd, buffer, cnt + 1, 0);
            }
        }
      else if( sscanf( buffer, "archive %s", tbuffer) )
        {
          iocdb_snapshot_tree( tbuffer);
        }
      else if( sscanf( buffer, "tree_dump %s", tbuffer) )
        {
          iocdb_debug_print_tree( tbuffer);
        }
      else if( sscanf( buffer, "experimental %s", tbuffer) )
        {
          //          test_func( c_sockfd);
        }
      else
        {
          cnt = snprintf(tbuffer, BUFSIZE, "Unknown command: \"%s\".\n",
                         buffer);
          send(c_sockfd, tbuffer, cnt + 1, 0);
        }
      
      shutdown( c_sockfd, SHUT_RDWR);
      close( c_sockfd);
    }

  return 0;
}      


////////////////////////////////////////


int config_dict_reader(struct config_dictionary *dict)
{
  enum Settings { HeartbeatUdpPort, DatabaseTcpPort, SubscriptionUdpPort,
                  FailNumberHeartbeats, FailCheckPeriod, InstanceRetainTime,
                  LogFile, EventFile, InfoFile, ControlSocket, EventDir,
                  StateDir, SettingsNumber };

  char *setting_str[] = { "heartbeat_udp_port", "database_tcp_port",
                          "subscription_udp_port", "fail_number_heartbeats",
                          "fail_check_period", "instance_retain_time",
                          "log_file", "event_file", "info_file",
                          "control_socket", "event_dir", "state_dir" };

  

  char *flags;
  
  char *token1, *token2;
  int current;

  int val;

  int i;
  enum Settings s;

  /* config_name = config_file; */
  /* dict = config_get_dictionary(&config_name); */
  /* if( dict == NULL) */
  /*   return 1; */
  

  flags = calloc( SettingsNumber, sizeof(char) );
  if( flags == NULL)
    {
      printf("Can't allocate memory.\n");
      return 1;
    }
  
  for( i = 0; i < dict->count; i++)
    {
      token1 = dict->key[i];
      token2 = dict->value[i];
        
      current = -1;
      for( s = 0; s < SettingsNumber; s++)
        {
          if( !strcasecmp( token1, setting_str[s]) )
            {
              current = s;
              break;
            }
        }
      if( current == -1)
        {
          printf("Configuration file error in \"%s\" :\n"
                 "Unknown option \"%s\".\n", dict->filename, token1);
          return 1;
        }

      switch( current)
        {
        case FailNumberHeartbeats:
          val = atoi( token2);
          if( (val <= 0) || (val > 255) )
            {
              printf("Configuration file error in \"%s\" :\n"
                     "Bad value of \"%s\" for \"%s\".\n", 
                     dict->filename, token2, token1);
              return 1;
            }
          config.fail_number_heartbeats = val;
          break;
        case InstanceRetainTime:
          val = atoi( token2);
          if( (val <= 0) || (val > 10000000) )
            {
              printf("Configuration file error in \"%s\" :\n"
                     "Bad value of \"%s\" for \"%s\".\n",
                     dict->filename, token2, token1);
              return 1;
            }
          config.instance_retain_time = val;
          break;
        case FailCheckPeriod:
          val = atoi( token2);
          if( (val <= 0) || (val > 65535) )
            {
              printf("Configuration file error in \"%s\" :\n"
                     "Bad value of \"%s\" for \"%s\".\n",
                     dict->filename, token2, token1);
              return 1;
            }
          config.fail_check_period = val;
          break;
        case HeartbeatUdpPort:
        case DatabaseTcpPort:
        case SubscriptionUdpPort:
          val = atoi( token2);
          if( (val <= 0) || (val > 65535) )
            {
              printf("Configuration file error in \"%s\" :\n"
                     "Bad value of \"%s\" for \"%s\".\n",
                     dict->filename, token2, token1);
              return 1;
            }
          if( current == HeartbeatUdpPort)
            config.heartbeat_udp_port = val;
          else if( current == DatabaseTcpPort)
            config.database_tcp_port = val;
          else
            config.subscription_udp_port = val;
          break;
        case LogFile:
          config.log_file = strdup( token2);
          break;
        case EventFile:
          config.event_file = strdup( token2);
          break;
        case InfoFile:
          config.info_file = strdup( token2);
          break;
        case ControlSocket:
          config.control_socket = strdup( token2);
          break;
        case EventDir:
          config.event_dir = strdup( token2);
          break;
        case StateDir:
          config.state_dir = strdup( token2);
          break;
        }
      if( flags[current])
        {
          printf("Configuration file error in \"%s\" :\n"
                 "Multiple values for \"%s\".\n", 
                 dict->filename, token1);
          return 1;
        }
      flags[current] = 1;
    }

  for( i = 0; i < SettingsNumber; i++)
    {
      if( !flags[i] )
        {
          printf("Configuration file error in \"%s\" :\n"
                 "Missing value for \"%s\".\n",
                 dict->filename, setting_str[i]);
          return 1;
        }
    }
  
  free( flags);
                       
  return 0;
}



void signal_term(int signum)
{
  signal_exit = 1;
}


void helper(void)
{
  printf("alived [-ht] [configuration_file]\n"
         "   -h for this help\n"
         "   -t means run in terminal, not as a daemon\n");
}

//int main( int argc, char *argv[], char *envp[])
int main( int argc, char *argv[])
{
  char *config_name;

  int sockfd;
  struct sockaddr_in ip_addr;

  struct sigaction action;

  pthread_t thread;
  pthread_attr_t attr;


  struct client_reply_struct *crs;

  time_t starttime;

  int flag;
  

  int daemon_mode = 1;


  while((flag = getopt( argc, argv, "ht")) != -1)
    {
      switch(flag)
        {
        case 'h':
          helper();
          return 0;
        case 't':
          daemon_mode = 0;
          break;
        }
    }
  if( (argc - optind) > 1)
    {
      helper();
      return 0;
    }

  config_name = (argc - optind) == 1 ? argv[optind] : NULL ;
  dict = config_get_dictionary( config_name);
  if( dict == NULL)
    return 1;
  if( config_dict_reader(dict) )
    return 1;

  
  if( daemon_mode )
    daemon(0,0);



  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, NULL);
  sigaction(SIGCHLD, &action, NULL);
  sigaction(SIGHUP, &action, NULL);
  action.sa_handler = signal_term;
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);


  log_init(config.log_file);

  debug_init("/tmp/alived-debug.log");


  log_write("Start\n");

  /*
    This section starts the thread that processes heartbeats from the IOCs
    and periodically checks the database for timeouts
   */
  if( iocdb_start())
    return 1;


  /*
    This section starts the thread that processes client db requests
   */

  starttime = time(NULL);

  // bind TCP socket for programs that want data, then start thread
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  flag = 1;
  if( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, 
                 sizeof(flag)) == -1) 
    {
      log_error_write(errno, "TCP setsockopt");
      return 1;
    }
  bzero( &ip_addr, sizeof(ip_addr) );
  ip_addr.sin_family = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ip_addr.sin_port = htons(config.database_tcp_port);
  if( bind(sockfd, (struct sockaddr *) &ip_addr, sizeof( ip_addr) ) )
    {
      log_error_write(errno, "TCP bind");
      return 1;
    }
  if( listen(sockfd, 5) )
    {
      log_error_write(errno, "TCP listen");
      return 1;
    }

  crs = malloc( sizeof( struct client_reply_struct) );
  crs->socket = sockfd;
  crs->starttime = starttime;

  pthread_attr_init(&attr);
  //  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_create(&thread, &attr, client_reply, (void *) crs );
  pthread_attr_destroy( &attr);


  /*
    This section starts the thread that srvices subscribers that
    want event notifications.
  */

  if( notifydb_start() )
    return 1;


  /*
    This command starts the loop for processing control requests
   */
  // don't really need to see if exited on error
  control_loop();

  
  notifydb_stop();
  
  iocdb_stop();

  // supposed to need this here
  //  pthread_exit(NULL);

  log_write("Halt\n");

  return 0;
}
