#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "uthash.h" //empty
#include <stdio.h>
#include <pthread.h>
#include "tm_verbose.h"
#include "tm_malloc.h"
#include "tm_tree.h"
#include "tm_mt.h"


#define MIN(a,b) ((a)<(b)?(a):(b))

#define EXTRA_BYTE 100

typedef uint8_t  byte;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


typedef struct _hash_t {
  void   *key;            /* we'll use this field as the key */
  size_t size;
  char   *file;
  int    line;
  UT_hash_handle hh; /* makes this structure hashable */
}hash_t;

static hash_t *size_hash = NULL;
static char extra_data[EXTRA_BYTE];

static void save_ptr(void *ptr, size_t size, char *file, int line);
static size_t retreive_size(void *someaddr);
static void init_extra_data(void);

//
char *my_strdup(char* string);

/* Not Called */
char *my_strdup(char* string){
  int size = 1+strlen(string);
  char *res = (char*)malloc(size*sizeof(char));

  if(res)
    memcpy(res, string, size*sizeof(char));

  return res;

}

/* Not Called */
void save_ptr(void *ptr, size_t size, char *file, int line) {
  hash_t *elem;
  elem = (hash_t*) malloc(sizeof(hash_t));
  elem -> key  = ptr;
  elem -> size = size;
  elem -> line = line;
  elem -> file = my_strdup(file);
  tm_verbose_fprintf(DEBUG,verbose_stdout,"Storing (%p,%ld)\n", (void *)ptr,size);
  HASH_ADD_PTR( size_hash, key, elem );
}

/* Not Called */
size_t retreive_size(void *someaddr){
  size_t res;
  hash_t *elem = NULL;
  if (pthread_mutex_lock(&lock) != 0){
        tm_verbose_fprintf(CRITICAL, verbose_stderr, "Cannot lock inread mode!\n");
    exit(-1);
  }
  HASH_FIND_PTR(size_hash, &someaddr, elem);
  pthread_mutex_unlock(&lock);
  if(!elem){
    tm_verbose_fprintf(CRITICAL,verbose_stderr,"Cannot find ptr %p to free!\n", (void *)someaddr);
    abort();
    return 0;
  }

  res  = elem->size;

  tm_verbose_fprintf(DEBUG,verbose_stdout,"Retreiving (%p,%ld)\n",(void *)someaddr, res);

  free(elem->file);
  if (pthread_mutex_lock(&lock) != 0){
    tm_verbose_fprintf(CRITICAL, verbose_stderr, "Cannot lock in write mode!\n");
    exit(-1);
  }
  HASH_DEL( size_hash, elem);
  pthread_mutex_unlock(&lock);
  return res;
}

void tm_mem_check(void){
#ifdef __DEBUG_TM_MALLOC__
  hash_t  *s;
  int nb_errors = 0;
  for(s=size_hash; s != NULL; s=s->hh.next) {

    tm_verbose_fprintf(ERROR,verbose_stderr,"pointer %p of size %ld (%s: %d) has not been freed!\n", (void *)s->key + EXTRA_BYTE, s->size, s->file, s->line);
    nb_errors ++;
  }

  tm_verbose_fprintf(INFO,verbose_stdout,"Number of errors in managing memory: %d\n",nb_errors);
#endif
}

/* Not Called */
void init_extra_data(void){
  static int done = 0;
  int i;

  if(done)
    return;
  
  init_genrand(0);

  for( i = 0 ; i < EXTRA_BYTE; i++)
    extra_data[i] = (char) genrand_int32() % 256;

  done = 1;
}

/* Not Called */
void *tm_malloc(size_t size, char *file, int line){
  byte *ptr;
  init_extra_data();

  size+=2*EXTRA_BYTE;
  ptr = malloc(size);

  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_malloc of size %ld: %p (%s: %d)\n",size-2*EXTRA_BYTE, (void *)ptr,file,line);

  save_ptr(ptr, size, file, line);

  memcpy(ptr, extra_data, EXTRA_BYTE);
  memcpy(ptr + size - EXTRA_BYTE, extra_data, EXTRA_BYTE);

  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_malloc returning: %p\n",(void *)(ptr+EXTRA_BYTE));

  return (void *)(ptr + EXTRA_BYTE);
}

/* Not Called */
void *tm_calloc(size_t count, size_t size, char *file, int line){
  byte *ptr;
  size_t full_size;

  init_extra_data();

  full_size = count * size + 2 * EXTRA_BYTE;

  ptr = malloc(full_size);
  bzero(ptr,full_size);
  save_ptr(ptr, full_size, file, line);

  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_calloc of size %ld: %p (%s: %d)\n",full_size-2*EXTRA_BYTE,(void *)ptr, file, line);

  memcpy(ptr, extra_data, EXTRA_BYTE);
  memcpy(ptr + full_size - EXTRA_BYTE, extra_data, EXTRA_BYTE);

  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_calloc returning: %p\n",(void *)(ptr+EXTRA_BYTE));

  return (void *)(ptr+EXTRA_BYTE);
}

/* Not Called */
void *tm_realloc(void *old_ptr, size_t size, char *file, int line){
  byte *ptr;
  size_t full_size;

  init_extra_data();

  full_size = size + 2 * EXTRA_BYTE;

  ptr = malloc(full_size);
  save_ptr(ptr, full_size, file, line);

  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_realloc of size %ld: %p (%s: %d)\n",full_size-2*EXTRA_BYTE, (void *)ptr, file, line);

  memcpy(ptr, extra_data, EXTRA_BYTE);
  memcpy(ptr + full_size - EXTRA_BYTE, extra_data, EXTRA_BYTE);

  if(old_ptr){
    byte *original_ptr = ((byte *)old_ptr) - EXTRA_BYTE;
    size_t old_ptr_size = retreive_size(original_ptr);

    memcpy(ptr + EXTRA_BYTE, old_ptr, MIN(old_ptr_size - 2 * EXTRA_BYTE, size));

    if((bcmp(original_ptr ,extra_data, EXTRA_BYTE))){
      tm_verbose_fprintf(ERROR,verbose_stderr,"Realloc: cannot find special string ***before*** %p!\nmemory is probably corrupted here!\n", (void *)original_ptr);
    }

    if((bcmp(original_ptr + old_ptr_size -EXTRA_BYTE ,extra_data, EXTRA_BYTE))){
      tm_verbose_fprintf(ERROR,verbose_stderr,"Realloc: cannot find special string ***after*** %p!\nmemory is probably corrupted here!\n", (void *)original_ptr);
    }

    tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_free freeing: %p\n",(void *)original_ptr);

    free(original_ptr);
  }


  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_realloc returning: %p (----- %p)\n", (void *)(ptr+EXTRA_BYTE), (void *)(ptr - EXTRA_BYTE));


  return (void *)(ptr+EXTRA_BYTE);
}

/* Not Called */
void tm_free(void *ptr){
  byte *original_ptr = ((byte *)ptr) - EXTRA_BYTE;
  size_t size;

  if(!ptr)
    return;

  size = retreive_size(original_ptr);

  if((bcmp(original_ptr ,extra_data, EXTRA_BYTE))){
    tm_verbose_fprintf(ERROR,verbose_stderr,"Free: cannot find special string ***before*** %p!\nmemory is probably corrupted here!\n", (void *)original_ptr);
  }

  if((bcmp(original_ptr + size -EXTRA_BYTE ,extra_data, EXTRA_BYTE))){
    tm_verbose_fprintf(ERROR,verbose_stderr,"Free: cannot find special string ***after*** %p!\nmemory is probably corrupted here!\n", (void *)original_ptr);
  }

  tm_verbose_fprintf(DEBUG,verbose_stdout,"tm_free freeing: %p\n", (void *)original_ptr);

  free(original_ptr);
}


