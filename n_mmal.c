/**
 * Implementace My MALloc
 * Demonstracni priklad pro 1. ukol IPS/2019
 * Ales Smrcka
 */

#include "mmal.h"
#include <sys/mman.h> // mmap
#include <stdbool.h> // bool
#include <assert.h> // assert
#include <string.h> // memcpy

//TODO delete
#include<stdio.h>

#ifdef NDEBUG
/**
 * The structure header encapsulates data of a single memory block.
 *   ---+------+----------------------------+---
 *      |Header|DDD not_free DDDDD...free...|
 *   ---+------+-----------------+----------+---
 *             |-- Header.asize -|
 *             |-- Header.size -------------|
 */
typedef struct header Header;
struct header {

    /**
     * Pointer to the next header. Cyclic list. If there is no other block,
     * points to itself.
     */
    Header *next;

    /// size of the block
    size_t size;

    /**
     * Size of block in bytes allocated for program. asize=0 means the block
     * is not used by a program.
     */
    size_t asize;
};

/**
 * The arena structure.
 *   /--- arena metadata
 *   |     /---- header of the first block
 *   v     v
 *   +-----+------+-----------------------------+
 *   |Arena|Header|.............................|
 *   +-----+------+-----------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
typedef struct arena Arena;
struct arena {

    /**
     * Pointer to the next arena. Single-linked list.
     */
    Arena *next;

    /// Arena size.
    size_t size;
};

#define PAGE_SIZE (128*1024)

#endif // NDEBUG

Arena *first_arena = NULL;

/**
 * Return size alligned to PAGE_SIZE
 */
static
size_t allign_page(size_t size)
{
    if(size == 0) return 0;
    size_t new_size = ((size - 1) / PAGE_SIZE + 1) * PAGE_SIZE;
    return new_size;
}

/**
 * Allocate a new arena using mmap.
 * @param req_size requested size in bytes. Should be alligned to PAGE_SIZE.
 * @return pointer to a new arena, if successfull. NULL if error.
 * @pre req_size > sizeof(Arena) + sizeof(Header)
 */

/**
 *   +-----+------------------------------------+
 *   |Arena|....................................|
 *   +-----+------------------------------------+
 *
 *   |--------------- Arena.size ---------------|
 */
static
Arena *arena_alloc(size_t req_size)
{
    assert(req_size > sizeof(Arena) + sizeof(Header));

    size_t al_size = allign_page(req_size+sizeof(Arena)+sizeof(Header));
    Arena *arena = mmap(NULL, al_size, PROT_WRITE | PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if(arena == MAP_FAILED) return NULL;

    arena->next = NULL;
    arena->size = al_size;
    return arena;
}

static
Header *find_first_header()
{
  Header *first = (void *) first_arena + sizeof(Arena);
  return first;
}

static
Header *find_prev_header(Header *hdr)
{
  assert(first_arena != NULL);

  Header *curr_hdr = hdr;
  while(curr_hdr->next != hdr) curr_hdr = curr_hdr->next;
  return curr_hdr;
}

static
Header *find_last_header()
{
  Header *first = find_first_header();
  if(first->next == NULL) return NULL; //for first header not yet inicialized
  Header *last = find_prev_header(first);
  return last;
}

static
Header *find_first_in_arena(Arena *arena)
{
  Header *first_in_arena = (void *) arena + sizeof(Arena);
  return first_in_arena;
}

static
Arena *find_last_arena()
{
  Arena *last = first_arena;
  while(last->next != NULL) last = last->next;
  return last;
}

/**
 * Appends a new arena to the end of the arena list.
 * @param a     already allocated arena
 */
static
void arena_append(Arena *a)
{
    if(first_arena == NULL) {
      first_arena = a;
    }
    else {
      Arena *last_arena = find_last_arena();
      last_arena->next = a;
    }
}

/**
 * Header structure constructor (alone, not used block).
 * @param hdr       pointer to block metadata.
 * @param size      size of free block
 * @pre size > 0
 */
/**
 *   +-----+------+------------------------+----+
 *   | ... |Header|........................| ...|
 *   +-----+------+------------------------+----+
 *
 *                |-- Header.size ---------|
 */
static
void hdr_ctor(Header *hdr, size_t size)
{
    assert (size > 0);
    hdr->size = size;
    hdr->asize = 0;
    hdr->next = NULL;
}

/**
 * Checks if the given free block should be split in two separate blocks.
 * @param hdr       header of the free block
 * @param size      requested size of data
 * @return true if the block should be split
 * @pre hdr->asize == 0
 * @pre size > 0
 */
static
bool hdr_should_split(Header *hdr, size_t size)
{
    assert(hdr->asize == 0);
    assert(size > 0);

    if(hdr->size >= size + 2*sizeof(Header)) return true;
    return false;
}

/**
 * Splits one block in two.
 * @param hdr       pointer to header of the big block
 * @param req_size  requested size of data in the (left) block.
 * @return pointer to the new (right) block header.
 * @pre   (hdr->size >= req_size + 2*sizeof(Header))
 */
/**
 * Before:        |---- hdr->size ---------|
 *
 *    -----+------+------------------------+----
 *         |Header|........................|
 *    -----+------+------------------------+----
 *            \----hdr->next---------------^
 */
/**
 * After:         |- req_size -|
 *
 *    -----+------+------------+------+----+----
 *     ... |Header|............|Header|....|
 *    -----+------+------------+------+----+----
 *             \---next--------^  \--next--^
 */
static
Header *hdr_split(Header *hdr, size_t req_size)
{
    assert (hdr->size >= req_size + 2*sizeof(Header));

    Header *new_hdr = hdr + req_size + sizeof(Header);
    hdr_ctor(new_hdr, (hdr->size - req_size - sizeof(Header)));
    new_hdr->next = hdr->next;
    hdr->next = new_hdr;
    hdr->size = req_size;

    return new_hdr;
}

/**
 * Detect if two adjacent blocks could be merged.
 * @param left      left block
 * @param right     right block
 * @return true if two block are free and adjacent in the same arena.
 * @pre left->next == right
 * @pre left != right
 */
static
bool hdr_can_merge(Header *left, Header *right)
{
    assert(left->next == right);
    assert(left != right);

    if(left->asize == 0 && right->asize == 0 && (left + sizeof(Header) + left->size) == right)
      return true; //the last condition should mean the blocks are in the same arena
    return false;
}

/**
 * Merge two adjacent free blocks.
 * @param left      left block
 * @param right     right block
 * @pre left->next == right
 * @pre left != right
 */
static
void hdr_merge(Header *left, Header *right)
{
    assert(left->next == right);
    assert(left != right);
    left->next = right->next;
    left->size = left->size + right->size + sizeof(Header);
}

//TODO delete
void debug_arenas()
{
  Header *curr_header = find_first_header();
  Arena *curr_arena = first_arena;
  printf("------\n");
  printf("Arena debug:\n------\n");
  for(int i = 0; curr_arena != NULL; i++) {
    printf("%d. arena - %ld\n", i, curr_arena->size);
    curr_arena = curr_arena->next;
  }
  printf("-----\nHeader debug: (size, asize, it, next)\n-----\n");
  for(int i = 0; (curr_header != find_first_header()) || i == 0; i++) {
    printf("%d. header - %ld, %ld, %p, %p\n", i, curr_header->size, curr_header->asize, curr_header, curr_header->next);
    curr_header = curr_header->next;
  }
  printf("------\n");
}

/**
 * Finds the free block that fits best to the requested size.
 * @param size      requested size
 * @return pointer to the header of the block or NULL if no block is available.
 * @pre size > 0
 */
static
Header *best_fit(size_t size)
{
  assert(size > 0);
  if(first_arena == NULL) return NULL;

  Header *best_fit_hdr = NULL;
  Header *first_hdr = find_first_header();
  Header *curr_hdr = first_hdr;
  size_t extra;

  do {
    if(curr_hdr->asize == 0) {
      //block is free, can be used for alocation
      if(best_fit_hdr == NULL) {
        best_fit_hdr = curr_hdr;
        extra = curr_hdr->size - size;
      }
      else if(curr_hdr->size - size < extra) {
        best_fit_hdr = curr_hdr;
        extra = curr_hdr->size - size;
      }
    }
    curr_hdr = curr_hdr->next;
  } while(curr_hdr != first_hdr);

  return best_fit_hdr;
}

/**
 * Search the header which is the predecessor to the hdr. Note that if
 * @param hdr       successor of the search header
 * @return pointer to predecessor, hdr if there is just one header.
 * @pre first_arena != NULL
 * @post predecessor->next == hdr
 */
 /* NOT USED, ALREADY HAVE find_prev_header(Header *hdr);
static
Header *hdr_get_prev(Header *hdr)
{
    // FIXME
    (void)hdr;
    return NULL;
}*/

/**
 * Allocate memory. Use best-fit search of available block.
 * @param size      requested size for program
 * @return pointer to allocated data or NULL if error or size = 0.
 */
void *mmalloc(size_t size)
{
    assert(size > 0);
    Header *best_fit_hdr = best_fit(size);

    if(best_fit_hdr == NULL) {
      //no fit found, new arena needed
      Arena *new_arena = arena_alloc(size);
      if(new_arena == NULL) return NULL; //alocation failed

      arena_append(new_arena);
      Header *first_in_new = (void *) new_arena + sizeof(Arena);
      Header *last_hdr = find_last_header();
      hdr_ctor(first_in_new, new_arena->size - sizeof(Arena) - sizeof(Header));

      //linking new header to the rest
      if(last_hdr == NULL) {
        //no linked headers yet
        first_in_new->next = first_in_new;
      }
      else {
        Header *first_hdr = find_first_header();
        last_hdr->next = first_in_new;
        first_in_new->next = first_hdr;
      }
      //marks new one as best fit
      best_fit_hdr = first_in_new;
    }

    if(hdr_should_split(best_fit_hdr, size)) hdr_split(best_fit_hdr, size);
    best_fit_hdr->asize = size;

    return best_fit_hdr;
}

/**
 * Free memory block.
 * @param ptr       pointer to previously allocated data
 * @pre ptr != NULL
 */
void mfree(void *ptr)
{
    (void)ptr;
    // FIXME
}

/**
 * Reallocate previously allocated block.
 * @param ptr       pointer to previously allocated data
 * @param size      a new requested size. Size can be greater, equal, or less
 * then size of previously allocated block.
 * @return pointer to reallocated space.
 */
void *mrealloc(void *ptr, size_t size)
{
    // FIXME
    (void)ptr;
    (void)size;
    return NULL;
}
