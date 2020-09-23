/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <arpa/inet.h>

#include "gentypes.h"

void worm_init( struct worm_mutex_struct *worm)
{
  worm->read_count = 0;
  worm->write_count = 0;
  pthread_mutex_init(&(worm->m_1), NULL);
  pthread_mutex_init(&(worm->m_2), NULL);
  pthread_mutex_init(&(worm->m_3), NULL);
  pthread_mutex_init(&(worm->m_w), NULL);
  pthread_mutex_init(&(worm->m_r), NULL);
}

void worm_clear( struct worm_mutex_struct *worm)
{
  worm->read_count = 0;
  worm->write_count = 0;
  pthread_mutex_destroy(&(worm->m_1));
  pthread_mutex_destroy(&(worm->m_2));
  pthread_mutex_destroy(&(worm->m_3));
  pthread_mutex_destroy(&(worm->m_w));
  pthread_mutex_destroy(&(worm->m_r));
}

// add reader to lock, lock out writer by at most one new reader
void worm_lock_reader(struct worm_mutex_struct *worm)
{
  pthread_mutex_lock(&(worm->m_3));
  pthread_mutex_lock(&(worm->m_r));
  pthread_mutex_lock(&(worm->m_1));
  worm->read_count++;
  if( worm->read_count == 1)
    pthread_mutex_lock(&(worm->m_w));
  pthread_mutex_unlock(&(worm->m_1));
  pthread_mutex_unlock(&(worm->m_r));
  pthread_mutex_unlock(&(worm->m_3));
}

// remove reader from lock, remove lock from writer if no more readers
void worm_unlock_reader(struct worm_mutex_struct *worm)
{
  pthread_mutex_lock(&(worm->m_1));
  worm->read_count--;
  if( worm->read_count == 0)
    pthread_mutex_unlock(&(worm->m_w));
  pthread_mutex_unlock(&(worm->m_1));
}

// set writer lock, but have to wait for previous writer to end
// lock out the readers
void worm_lock_writer(struct worm_mutex_struct *worm)
{
  pthread_mutex_lock(&(worm->m_2));
  worm->write_count++;
  if( worm->write_count == 1)
    pthread_mutex_lock(&(worm->m_r));
  pthread_mutex_unlock(&(worm->m_2));
  pthread_mutex_lock(&(worm->m_w));
}

// remove write lock, remove lock from readers if no more writers
void worm_unlock_writer(struct worm_mutex_struct *worm)
{
  pthread_mutex_unlock(&(worm->m_w));
  pthread_mutex_lock(&(worm->m_2));
  worm->write_count--;
  if( worm->write_count == 0)
    pthread_mutex_unlock(&(worm->m_r));
  pthread_mutex_unlock(&(worm->m_2));
}

///////////////////////////////

void sharedint_init( struct sharedint_struct *shint, int count)
{
  pthread_mutex_init(&(shint->lock), NULL);
  pthread_mutex_lock(&(shint->lock));
  shint->count = count;
  pthread_mutex_unlock(&(shint->lock));
}

void sharedint_uninit( struct sharedint_struct *shint)
{
  pthread_mutex_destroy(&(shint->lock));
}

int sharedint_alter( struct sharedint_struct *shint, int count)
{
  int cnt;

  pthread_mutex_lock(&(shint->lock));
  shint->count += count;
  cnt = shint->count;
  pthread_mutex_unlock(&(shint->lock));

  return cnt;
}

int sharedint_value( struct sharedint_struct *shint)
{
  int cnt;

  pthread_mutex_lock(&(shint->lock));
  cnt = shint->count;
  pthread_mutex_unlock(&(shint->lock));

  return cnt;
}

////////////////////////////////

void netbuffer_init( struct netbuffer_struct *nbuff, int initlength)
{
  nbuff->count = 0;
  nbuff->length = (initlength > 256 ? initlength : 256);
  nbuff->buffer = malloc( nbuff->length * sizeof(char));
}

void netbuffer_deinit(struct netbuffer_struct *nbuff)
{
  nbuff->count = 0;
  nbuff->length = 0;
  if( nbuff->buffer != NULL)
    free(nbuff->buffer);
  nbuff->buffer = NULL;
}

void netbuffer_clear( struct netbuffer_struct *nbuff)
{
  nbuff->count = 0;
}

unsigned char *netbuffer_data( struct netbuffer_struct *nbuff)
{
  return nbuff->buffer;
}

int netbuffer_size( struct netbuffer_struct *nbuff)
{
  return nbuff->count;
}


static void netbuffer_check_size( struct netbuffer_struct *nbuff, int bytes)
{
  if( (nbuff->length - nbuff->count) < bytes)
    {
      do
        {
          nbuff->length *= 2;
          // double until big enough
        } 
      while( (nbuff->length - nbuff->count) < bytes);
      nbuff->buffer = realloc( nbuff->buffer, nbuff->length);
    }
}

void netbuffer_add_uint32( struct netbuffer_struct *nbuff, uint32_t value)
{
  netbuffer_check_size( nbuff, 4);
  *((uint32_t *) &(nbuff->buffer[nbuff->count])) = htonl(value);
  nbuff->count += 4;
}

void netbuffer_add_uint16( struct netbuffer_struct *nbuff, uint16_t value)
{
  netbuffer_check_size( nbuff, 2);
  *((uint16_t *) &(nbuff->buffer[nbuff->count])) = htons(value);
  nbuff->count += 2;
}

void netbuffer_add_uint8( struct netbuffer_struct *nbuff, uint8_t value)
{
  netbuffer_check_size( nbuff, 1);
  *((uint8_t *) &(nbuff->buffer[nbuff->count])) = value;
  nbuff->count += 1;
}

void netbuffer_add_string( struct netbuffer_struct *nbuff, char *string,
                              int length)
{
  netbuffer_check_size( nbuff, length);
  memcpy( &(nbuff->buffer[nbuff->count]), string, length);
  nbuff->count += length;
}

void netbuffer_string_write( int bytes, struct netbuffer_struct *nbuff,
                             char *string)
{
  uint16_t len;

  if( string == NULL)
    return;
  len = strlen( string);

  if( bytes == 1)
    netbuffer_add_uint8( nbuff, len);
  else
    netbuffer_add_uint16( nbuff, len);

  if( len)
    netbuffer_add_string( nbuff, string, len);
}

unsigned char *netbuffer_export( struct netbuffer_struct *nbuff, int *size)
{
  unsigned char *b;

  b = malloc( nbuff->count * sizeof(char));
  memcpy( b, nbuff->buffer, nbuff->count);
  *size = nbuff->count;

  return b;
}


///////////////////////////////

struct llist *list_create( void)
{
  struct llist *list;

  list = malloc( sizeof(struct llist));
  list->number = 0;
  list->head.data = NULL;
  list->head.prev = &(list->head);
  list->head.next = &(list->head);
  pthread_mutex_init(&(list->lock), NULL);

  return list;
}

void list_clear(struct llist *list, void (* func)( void *, void *), void *arg)
{
  struct llink *ptr, *head, *temp;
    
  pthread_mutex_lock(&(list->lock));
  head = &(list->head);
  ptr = head->next;
  while( ptr != head)
    {
      if( func != NULL)
        func( ptr->data, arg);

      temp = ptr;
      ptr = ptr->next;
  
      free(temp);
    }
  list->number = 0;
  pthread_mutex_unlock(&(list->lock));
}

void list_destroy(struct llist *list, void (* func)( void *, void *), void *arg)
{
  list_clear(list, func, arg);
  pthread_mutex_destroy(&(list->lock));
  free( list);
}


// this can only be used locally
static void local_remove( struct llist *list, struct llink *link)
{
  struct llink *ptr;
  
  ptr = link;

  ptr->next->prev = ptr->prev;
  ptr->prev->next = ptr->next;
          
  free(link);

  list->number--;
}


// this can only be used locally
static void local_add( struct llist *list, void *data)
{
  struct llink *ptr;

  ptr = malloc( sizeof( struct llink));
  ptr->data = data;

  ptr->next = &(list->head);
  ptr->prev = list->head.prev;
  ptr->prev->next = ptr;
  ptr->next->prev = ptr;

  list->number++;
}


void list_add( struct llist *list, void *data)
{
  pthread_mutex_lock(&(list->lock));
  local_add( list, data);
  pthread_mutex_unlock(&(list->lock));
}


// apply func to all list members
// func set third arg to 1 if should stop, otherwise defaults to 0
// INTENDED PURPOSE:  This lets you apply some function to all the
// list members, or just the first that matches
void list_apply(struct llist *list, void (* func)( void *, void *, int *),
                void *arg)
{
  struct llink *ptr, *head;
  int stop_flag;
  
  if( func == NULL || !list->number)
    return;

  pthread_mutex_lock(&(list->lock));
  head = &(list->head);
  ptr = head->next;
  stop_flag = 0;
  while( ptr != head)
    {
      func( ptr->data, arg, &stop_flag);
      if( stop_flag)
        break;
      ptr = ptr->next;
    }

  pthread_mutex_unlock(&(list->lock));
}

// apply func to all list members
// func returns 1 if item was deleted, otherwise returns 0
// func set third arg to 1 if should stop, otherwise defaults to 0
// INTENDED PURPOSE: this lets you delete all the links that match some
// condition, or simply delete the first that matches
void list_apply_delete(struct llist *list, int (* func)( void *, void *, int *),
                       void *arg)
{
  struct llink *ptr, *head, *temp;
  int stop_flag;

  if( func == NULL || !list->number)
    return;

  pthread_mutex_lock(&(list->lock));
  head = &(list->head);
  ptr = head->next;
  stop_flag = 0;
  while( ptr != head)
    {
      if( func( ptr->data, arg, &stop_flag) )
        {
          temp = ptr;
          ptr = ptr->next;

          local_remove( list, temp);
        }
      else
        ptr = ptr->next;

      if( stop_flag)
        break;
    }

  pthread_mutex_unlock(&(list->lock));
}


// func returns 1 if item was deleted, otherwise returns 0
// func returns non-NULL is it wants to replace data
// func set third arg to 1 if should stop, otherwise defaults to 0
// if it makes it to end, adds arg to list
// INTENDED PURPOSE: this function lets you find a member in the list
// and replace it, or add a new one if it doesn't exist
void list_apply_modify_add(struct llist *list,
                           void* (* find_func)( void *, void *, int *),
                           void* (* add_func)(void *), void *arg)
{
  struct llink *ptr, *head;
  int stop_flag;

  void *data;
  
  if( (find_func == NULL ) || ( add_func == NULL ))
    return;

  pthread_mutex_lock(&(list->lock));
  stop_flag = 0;
  if( list->number)
    {
      head = &(list->head);
      ptr = head->next;
      while( ptr != head)
        {
          // successfully did what it wanted
          data = find_func( ptr->data, arg, &stop_flag);
          if( data != NULL )
            ptr->data = data;

          if( stop_flag)
            break;
          
          ptr = ptr->next;
        }
    }

  // at this point, add link is stop_flag still zero
  if( !stop_flag)
    local_add( list, add_func(arg) );
  
  pthread_mutex_unlock(&(list->lock));
}


// process the list, accessing global list data under same lock as list items
// either func can be NULL
void list_process(struct llist *list,
                  void (* global_func)( struct llist_global *, void *),
                  void (* func)( void *, void *), void *arg)
{
  struct llink *ptr, *head;
  
  pthread_mutex_lock(&(list->lock));

  if( global_func != NULL)
    {
      struct llist_global gl;

      gl.number = list->number;
      global_func( &gl, arg);
    }

  if(( func != NULL) && list->number)
    {
      head = &(list->head);
      ptr = head->next;
      while( ptr != head)
        {
          func( ptr->data, arg);

          ptr = ptr->next;
        }
    }

  pthread_mutex_unlock(&(list->lock));
}


// func returns data for new list, can't return NULL
void list_move_all( struct llist *src_list, struct llist *dest_list,
                    void* (* func)( void *, void *), void *arg )
{
  struct llink transfers;
  int transfer_cnt;
  
  struct llink *ptr, *head;

  if( func == NULL || !src_list->number)
    return;

  // remove list from incoming to completely to free up lock
  // don't want two locks active at same time if you can help it
  
  pthread_mutex_lock(&(src_list->lock));

  head = &(src_list->head);

  transfer_cnt = src_list->number;
  transfers.next = head->next;
  transfers.prev = head->prev;
  transfers.next->prev = &transfers;
  transfers.prev->next = &transfers;

  head->next = head;
  head->prev = head;
  src_list->number = 0;

  pthread_mutex_unlock(&(src_list->lock));

  /////////////////
  
  // run func here, and let it possibly change data
  // no locking needed (or available)
  
  head = &transfers;
  ptr = head->next;
  while( ptr != head)
    {
      ptr->data = func( ptr->data, arg);

      ptr = ptr->next;
    }

  ////////////////////  
  
  pthread_mutex_lock(&(dest_list->lock));

  dest_list->number += transfer_cnt;
  head = &(dest_list->head);

  transfers.prev->next = head;
  transfers.next->prev = head->prev;
  head->prev->next = transfers.next;
  head->prev = transfers.prev;

  /* transfers.next->prev = head; */
  /* transfers.prev->next = head->next; */
  /* head->next->prev = transfers.prev; */
  /* head->next = transfers.next; */

  pthread_mutex_unlock(&(dest_list->lock));
}


struct llist_global list_globals(struct llist *list)
{
  struct llist_global gl;
  
  pthread_mutex_lock(&(list->lock));
  gl.number = list->number;
  pthread_mutex_unlock(&(list->lock));

  return gl;
}



