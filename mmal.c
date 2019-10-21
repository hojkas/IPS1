/**
 * Implementace My MALloc
 * Demonstracni priklad pro 1. ukol IPS/2019
 * Ales Smrcka
 */

#include "mmal.h"
#include <sys/mman.h> // mmap
#include <stdbool.h> // bool
#include <assert.h> //used for assert

#include<stdio.h> //TODO delete

#ifdef NDEBUG
/**
 * The structure header encapsulates data of a single memory block.
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

#define PAGE_SIZE 128*1024

#endif

Arena *first_arena = NULL;

/**
 * Return size alligned to PAGE_SIZE
 */
static
size_t align_page(size_t size)
{
    size_t new_size = ((size - 1) / PAGE_SIZE + 1) * PAGE_SIZE; //counts wrong for <= 0, but can those numbers get in here?
    return new_size;
}

/**
 * Allocate a new arena using mmap.
 * @param req_size requested size in bytes. Should be alligned to PAGE_SIZE.
 * @return pointer to a new arena, if successfull. NULL if error.
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
    //TODO maybe different size taking into account the space taken by arena header? at least assert
    //deal with MAP_ANONYMOUS stuff

    size_t al_size = req_size+sizeof(Arena);
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
  Header *curr_hdr = hdr;
  while(curr_hdr->next != hdr) curr_hdr = curr_hdr->next;
  return curr_hdr;
}

Header *find_last_header()
{
  Header *first = find_first_header();
  Header *last = find_prev_header(first);
  return last;
}

Header *find_first_in_arena(Arena *arena)
{
  Header *first_in_arena = (void *) arena + sizeof(Arena);
  return first_in_arena;
}

Arena *find_last_arena()
{
  Arena *last = first_arena;
  while(last->next != NULL) last = last->next;
  return last;
}

/**
 * Header structure constructor (alone, not used block).
 * @param hdr       pointer to block metadata.
 * @param size      size of free block
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
 * Splits one block into two.
 * @param hdr       pointer to header of the big block
 * @param req_size  requested size of data in the (left) block.
 * @pre   (req_size % PAGE_SIZE) = 0
 * @pre   (hdr->size >= req_size + 2*sizeof(Header))
 * @return pointer to the new (right) block header.
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
    //assert ((req_size % PAGE_SIZE) != 0); TODO uncomment
    assert (hdr != NULL);
    assert (hdr->size >= req_size + 2*sizeof(Header));
    //could be just 1*size of header, but that would limit the header to size 0 - why do that?

    Header *created_hdr = hdr; //lets him point at the same adress as hdr
    created_hdr = created_hdr + sizeof(Header) + req_size; //lets created_hdr be at the right adress After
    //hdr's data and hdr header
    hdr_ctor(created_hdr, (hdr->size - req_size - sizeof(Header)));

    created_hdr->next = hdr->next;
    hdr->next = created_hdr;
    hdr->size = req_size;

    return created_hdr;
}

/**
 * Detect if two blocks adjacent blocks could be merged.
 * @param left      left block
 * @param right     right block
 * @return true if two block are free and adjacent in the same arena.
 */
static
bool hdr_can_merge(Header *left, Header *right)
{
    assert(left->next == right);
    if(left->asize == 0 && right->asize == 0 && (left + sizeof(Header) + left->size) == right)
      return true; //the last condition should mean the blocks are in the same arena
    return false;
}

/**
 * Merge two adjacent free blocks.
 * @param left      left block
 * @param right     right block
 */
static
void hdr_merge(Header *left, Header *right)
{
    assert(left->next == right);
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
  printf("-----\nHeader debug:\n-----\n");
  for(int i = 0; curr_header != NULL; i++) {
    printf("%d. header - %ld, %ld, %p\n", i, curr_header->size, curr_header->asize, curr_header->next);
    curr_header = curr_header->next;
  }
  printf("------\n");
}

/**
RETURNS NULL if no fit was found
*/
Header *best_fit(size_t req_size)
{
  //TODOO - chybí zarovnání na pages
  assert(first_arena != NULL);
  assert(req_size > 0);

  Header *best_fit = NULL;
  size_t extra;
  Header *curr_hdr = find_first_header();
  Header *first_hdr = curr_hdr;

  while(curr_hdr->next != first_hdr) {
    if(((curr_hdr->size - curr_hdr->asize) > (sizeof(Header) + req_size)) || (curr_hdr->asize == 0 && curr_hdr->size < req_size)) {
      //je-li za aktualnim headerem dost mista na jeho data, dalsi header, pozadovana data
      if(best_fit == NULL) {//prvni nalezeny blok
        if(curr_hdr->asize == 0) extra = curr_hdr->size;
        else extra = curr_hdr->size - curr_hdr->asize - sizeof(Header) - req_size;
        //stores extra space to extra
        best_fit = curr_hdr;
      }
      else if(((curr_hdr->size - curr_hdr->asize - sizeof(Header) - req_size) < extra) || (curr_hdr->asize == 0 && (curr_hdr->size - req_size) < extra)) {
        //already found fit before, but this is better fit
        if(curr_hdr->asize == 0) extra = curr_hdr->size;
        else extra = curr_hdr->size - curr_hdr->asize - sizeof(Header) - req_size;
        //stores extra space to extra
        best_fit = curr_hdr;
      }
    }
    curr_hdr = curr_hdr->next;
  }

  return best_fit;
}

void add_size_to_first(Arena *arena)
{
  Header* first = find_first_in_arena(arena);
  first->size = arena->size - sizeof(Arena) - sizeof(Header);
}

/**
 * Allocate memory. Use best-fit search of available block.
 * @param size      requested size for program
 * @return pointer to allocated data or NULL if error.
 */
void *mmalloc(size_t size)
{
  assert(size > 0);
  size_t al_size = align_page(size);
  if(first_arena == NULL) {
    first_arena = arena_alloc(al_size);
    if(first_arena == NULL) return NULL; //alocation failed
    Header *first_hdr = find_first_header();
    hdr_ctor(first_hdr, al_size);
    add_size_to_first(first_arena);
    first_hdr->next = first_hdr;
  }

  Header *aloc_here = best_fit(size); //tady chyba

  if(aloc_here == NULL) {
    //no space found -> need new arena

    Arena *last_arena = find_last_arena();
    last_arena->next = arena_alloc(size);
    if(last_arena->next == NULL) return NULL; //alocation failed

    last_arena = last_arena->next;
    hdr_ctor(find_first_in_arena(last_arena), size);
    add_size_to_first(last_arena);

    Header *last_header = find_last_header();
    last_header->next = find_first_in_arena(last_arena); //links header in new arena to prev last one
    last_header = last_header->next; //marks last_header as the last
    last_header->next = find_first_header(); //links new header to first one, forming cycle
    aloc_here = last_header; //given that no space was adequate to get here, only place to alloc is new o
  }

  //at this point, we have aloc_here header where we want to aloc (can be new header
  //with asize 0 OR header meant to be split to make that space)

  if(aloc_here->asize != 0) {
    aloc_here = hdr_split(aloc_here, aloc_here->asize);
  }
  aloc_here->asize = size;
  hdr_split(aloc_here, aloc_here->asize);

  return aloc_here;
}

/**
 * Free memory block.
 * @param ptr       pointer to previously allocated data
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
