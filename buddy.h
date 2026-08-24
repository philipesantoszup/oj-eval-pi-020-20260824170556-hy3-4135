#ifndef OS_MM_H
#define OS_MM_H

/* The provided test driver (main.c) intentionally passes the int returned
 * by return_pages() to PTR_ERR() (which expects a pointer). That triggers a
 * -Wint-conversion diagnostic that some toolchains treat as a hard error.
 * It does not affect program behaviour or output, so we silence it here.
 * This pragma is output-neutral. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wint-conversion"
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wint-conversion"
#endif
#define MAX_ERRNO 4095

#define OK          0
#define EINVAL      22  /* Invalid argument */    
#define ENOSPC      28  /* No page left */  


#define IS_ERR_VALUE(x) ((x) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long error) { return (void *)error; }
static inline long PTR_ERR(const void *ptr) { return (long)ptr; }
static inline long IS_ERR(const void *ptr) { return IS_ERR_VALUE((unsigned long)ptr); }


int init_page(void *p, int pgcount);
void *alloc_pages(int rank);
int return_pages(void *p);
int query_ranks(void *p);
int query_page_counts(int rank);

#endif