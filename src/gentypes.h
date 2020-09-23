/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef GENTYPES_H
#define GENTYPES_H 1

#include <pthread.h>
#include <stdint.h>

// writer one, reader many; prefer writer
struct worm_mutex_struct
{
  int read_count;
  int write_count;
  pthread_mutex_t m_1;
  pthread_mutex_t m_2;
  pthread_mutex_t m_3;
  pthread_mutex_t m_w;
  pthread_mutex_t m_r;
};


void worm_init( struct worm_mutex_struct *worm);
void worm_clear( struct worm_mutex_struct *worm);
void worm_lock_reader(struct worm_mutex_struct *worm);
void worm_unlock_reader(struct worm_mutex_struct *worm);
void worm_lock_writer(struct worm_mutex_struct *worm);
void worm_unlock_writer(struct worm_mutex_struct *worm);


////////////////////////////////////


struct sharedint_struct
{
  pthread_mutex_t lock;
  int count;
};


void sharedint_init( struct sharedint_struct *shint, int count);
void sharedint_uninit( struct sharedint_struct *shint);
int sharedint_alter( struct sharedint_struct *shint, int count);
int sharedint_value( struct sharedint_struct *shint);

///////////////////////////////////

struct netbuffer_struct
{
  int count;
  int length;
  unsigned char *buffer;
};


void netbuffer_init( struct netbuffer_struct *nbuff, int initlength);
void netbuffer_deinit(struct netbuffer_struct *nbuff);
void netbuffer_clear( struct netbuffer_struct *nbuff);
unsigned char *netbuffer_data( struct netbuffer_struct *nbuff);
int netbuffer_size( struct netbuffer_struct *nbuff);
void netbuffer_add_uint32( struct netbuffer_struct *nbuff, uint32_t value);
void netbuffer_add_uint16( struct netbuffer_struct *nbuff, uint16_t value);
void netbuffer_add_uint8( struct netbuffer_struct *nbuff, uint8_t value);
void netbuffer_add_string( struct netbuffer_struct *nbuff, char *string,
                           int length);
void netbuffer_string_write( int bytes, struct netbuffer_struct *nbuff,
                             char *string);
unsigned char *netbuffer_export( struct netbuffer_struct *nbuff, int *size);

/////////////////////////////////////


struct llink
{
  void *data;
  struct llink *prev, *next;
};

struct llist
{
  int number;
  struct llink head;
  pthread_mutex_t lock; 
};

struct llist_global
{
  int number;
};


struct llist *list_create( void);
// destroy list, func can be NULL
void list_destroy(struct llist *ll, void (* func)( void *, void *), void *arg);
// return all the globals of the lsit (currently just length)
struct llist_global list_globals(struct llist *list);
// add simgle item to list
void list_add( struct llist *list, void *data);
// remove all entries from list, func can be NULL
void list_clear(struct llist *ll, void (* func)( void *, void *), void *arg);

// apply func to all list members
// func set third arg to 1 if should stop, otherwise defaults to 0
void list_apply(struct llist *list, void (* func)( void *, void *, int *),
                void *arg);
// apply func to all list members
// func returns 1 if item was deleted (and stops), otherwise returns 0
void list_apply_delete(struct llist *list, int (* func)( void *, void *, int *),
                       void *arg);
// apply func to all list members until told to stop by func return value
// if it makes it to end, adds arg to list
// func returns 1 if should stop (finds item), otherwise returns 0
void list_apply_modify_add(struct llist *list,
                    void* (* find_func)( void *, void *, int *),
                    void* (* add_func)(void *), void *arg);

/* void list_move( struct llist *src_list, struct llist *dest_list, */
/*                 void* (* func)( void *, void *, int *), void *arg ); */
void list_move_all( struct llist *src_list, struct llist *dest_list,
                    void* (* func)( void *, void *), void *arg );


// process the list, accessing global list data under same lock as list items
// either func can be NULL
void list_process(struct llist *list,
                  void (* global_func)( struct llist_global *, void *),
                  void (* func)( void *, void *), void *arg);



#endif
