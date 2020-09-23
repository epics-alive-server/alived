/*************************************************************************\
* Copyright (c) 2020 UChicago Argonne, LLC,
*               as Operator of Argonne National Laboratory.
\*************************************************************************/

/*
  Written by Dohn A. Arms (Advanced Photon Source, ANL)
*/



#ifndef LLRB_DB_H
#define LLRB_DB_H 1

#include <pthread.h>

#include "gentypes.h"


///////////////////////////////////

enum { BLACK, RED };

struct tree_node
{
  char  color;    // 0:black, 1:red
  pthread_mutex_t *rec_mutex; 
  void *key;     // pointer to key
  void *values;     // pointer to data or function
  struct tree_node *left;
  struct tree_node *right;
};

struct tree_db
{
  int record_lock_flag;
  
  void *(* key_copy)( void *);
  void (* key_release)( void *);
  int (* key_compare)( const void *, const void *); // key comparison function
  int number;
  struct tree_node *tree;
  struct worm_mutex_struct worm_mutex; 
};

///////////////////////////

struct tree_db *db_create(void *(* key_copy)( void *), 
                          void (* key_release)( void *),
                          int (* key_compare)( const void *, const void *),
                          int auto_lock_records);
int db_add( struct tree_db *db, void *key, void *(* new_func)( void *), 
            void *(* existing_func)( void *, void *), void *arg);

void db_walk( struct tree_db *db, void (* func)( void *, void *), void *arg );
void db_walk_init( struct tree_db *db, void (* init)( void *, int),
                   void (* func)( void *, void *), void *arg );

int db_find( struct tree_db *db, void *key, void (* func)( void *, void *),
             void *arg );
int db_find_init( struct tree_db *db, void *key, void (* init)( void *, int), 
                  void (* func)( void *, void *), void *arg);

void db_multi_find( struct tree_db *db, int number, void **keys,
                    void (* func)( void *, void *), void *arg);
void db_multi_find_init( struct tree_db *db, int number, void **keys,
                         void (* init)( void *, int),
                         void (* func)( void *, void *), void *arg);

void db_walk_delete( struct tree_db *db, int (* func)( void *, void *),
                     void *arg );
int db_delete( struct tree_db *db, void *key, int (* func)( void *, void *),
               void *arg);
void db_destroy(struct tree_db *db, void (* func)( void *, void *), void *arg );

// arg is pointer to value and stuff, function manipulates tree data
//void db_lock_read(struct tree_db *db, void *arg, void (* func)( void *) );

int db_count( struct tree_db *db);

void db_ps( struct tree_db *db, char *filename);

////////////////////////////

#endif
