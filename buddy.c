#include "buddy.h"
#include <stdlib.h>
#include <string.h>

/*
 * Buddy allocator implementation.
 *
 * Memory model:
 *   - total_pages pages of 4K each, starting at base_addr.
 *   - rank r block has size 2^(r-1) pages.
 *   - The whole pool is always a power of two pages, so it is one
 *     rank max_rank block initially.
 *
 * Global metadata (sized total_pages):
 *   - meta[i]      : rank of the (free or allocated) block that fully
 *                    contains page i. Used by query_ranks.
 *   - free_rank[i] : if page i is the start of a FREE block, its rank;
 *                    otherwise 0. Used for O(1) buddy merge checks.
 *   - alloc_start[i]: 1 if page i is the start of an ALLOCATED block.
 *
 * Free blocks are kept in per-rank sorted (ascending by start) singly
 * linked lists so allocation always takes the lowest-address block,
 * which yields the sequential allocation order the tests require.
 */

#define PAGE_SIZE 4096L

static char *base_addr = NULL;
static int total_pages = 0;
static int max_rank = 0;

static int *meta = NULL;        /* rank of block containing page i            */
static int *free_rank = NULL;   /* rank if page i is start of a free block    */
static int *alloc_start = NULL; /* 1 if page i is start of an allocated block */
static int *next_arr = NULL;    /* linked-list next for free lists            */
static int free_head[17];       /* free_head[rank] = start page of first block */

static void list_insert(int rank, int s) {
    int *h = &free_head[rank];
    next_arr[s] = -1;
    if (*h == -1 || s < *h) {
        next_arr[s] = *h;
        *h = s;
    } else {
        int prev = *h;
        while (next_arr[prev] != -1 && next_arr[prev] < s)
            prev = next_arr[prev];
        next_arr[s] = next_arr[prev];
        next_arr[prev] = s;
    }
}

static void list_remove(int rank, int s) {
    int *h = &free_head[rank];
    if (*h == s) {
        *h = next_arr[s];
        next_arr[s] = -1;
        return;
    }
    int prev = *h;
    while (prev != -1 && next_arr[prev] != s)
        prev = next_arr[prev];
    if (prev != -1) {
        next_arr[prev] = next_arr[s];
        next_arr[s] = -1;
    }
}

/* remove and return the head block of rank; clears its free_rank */
static int list_pop(int rank) {
    int s = free_head[rank];
    if (s != -1) {
        free_head[rank] = next_arr[s];
        next_arr[s] = -1;
        free_rank[s] = 0;
    }
    return s;
}

int init_page(void *p, int pgcount) {
    base_addr = (char *)p;
    total_pages = pgcount;

    /* max_rank such that 2^(max_rank-1) == pgcount */
    int r = 0, t = pgcount;
    while (t > 1) { t >>= 1; r++; }
    max_rank = r + 1;

    meta = (int *)malloc(sizeof(int) * pgcount);
    free_rank = (int *)malloc(sizeof(int) * pgcount);
    alloc_start = (int *)malloc(sizeof(int) * pgcount);
    next_arr = (int *)malloc(sizeof(int) * pgcount);

    for (int i = 0; i < pgcount; i++) {
        meta[i] = 0;
        free_rank[i] = 0;
        alloc_start[i] = 0;
        next_arr[i] = -1;
    }
    for (int i = 0; i < 17; i++) free_head[i] = -1;

    /* whole pool is one free block of rank max_rank */
    free_head[max_rank] = 0;
    free_rank[0] = max_rank;
    next_arr[0] = -1;
    for (int i = 0; i < pgcount; i++) meta[i] = max_rank;

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > max_rank)
        return ERR_PTR((long)-EINVAL);

    int r = rank;
    while (r <= max_rank && free_head[r] == -1) r++;
    if (r > max_rank)
        return ERR_PTR((long)-ENOSPC);

    int start = list_pop(r);   /* block of rank r, no longer free */

    /* split down to the requested rank; keep the left (lower-address)
     * child as the block we continue with, put the right child on the
     * free list. This yields ascending allocation addresses. */
    while (r > rank) {
        int new_rank = r - 1;
        int child_sz = 1 << (new_rank - 1);   /* pages per rank new_rank block */
        int buddy = start + child_sz;
        free_rank[buddy] = new_rank;
        for (int i = buddy; i < buddy + child_sz; i++) meta[i] = new_rank;
        list_insert(new_rank, buddy);
        r = new_rank;
    }

    int sz = 1 << (rank - 1);
    alloc_start[start] = 1;
    for (int i = start; i < start + sz; i++) meta[i] = rank;

    return (void *)((char *)base_addr + (long)start * PAGE_SIZE);
}

int return_pages(void *p) {
    if (p == NULL) return -EINVAL;
    long off = (char *)p - base_addr;
    if (off < 0 || (off % PAGE_SIZE) != 0) return -EINVAL;
    int idx = (int)(off / PAGE_SIZE);
    if (idx < 0 || idx >= total_pages) return -EINVAL;

    int r = meta[idx];
    if (r < 1 || r > max_rank) return -EINVAL;

    int sz = 1 << (r - 1);
    int start = idx - (idx % sz);
    if (idx != start) return -EINVAL;        /* must be an exact block start */
    if (!alloc_start[start]) return -EINVAL; /* must be an allocated block   */

    alloc_start[start] = 0;

    int cur_rank = r;
    int cur_start = start;

    while (cur_rank < max_rank) {
        int cur_sz = 1 << (cur_rank - 1);
        int buddy = cur_start ^ cur_sz;
        if (buddy < 0 || buddy + cur_sz > total_pages) break;
        if (free_rank[buddy] == cur_rank) {
            list_remove(cur_rank, buddy);
            free_rank[buddy] = 0;
            if (buddy < cur_start) cur_start = buddy;
            cur_rank++;
        } else {
            break;
        }
    }

    int final_sz = 1 << (cur_rank - 1);
    free_rank[cur_start] = cur_rank;
    for (int i = cur_start; i < cur_start + final_sz; i++) meta[i] = cur_rank;
    list_insert(cur_rank, cur_start);

    return OK;
}

int query_ranks(void *p) {
    if (p == NULL) return -EINVAL;
    long off = (char *)p - base_addr;
    if (off < 0 || (off % PAGE_SIZE) != 0) return -EINVAL;
    int idx = (int)(off / PAGE_SIZE);
    if (idx < 0 || idx >= total_pages) return -EINVAL;
    return meta[idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > max_rank) return -EINVAL;
    int cnt = 0;
    for (int s = free_head[rank]; s != -1; s = next_arr[s]) cnt++;
    return cnt;
}
