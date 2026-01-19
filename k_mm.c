/*
 * I use the Segregated-free-list approach, with coalescing after every call to free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>


#include "mm.h"
#include "memlib.h"

/* 8-byte aligntment */
#define ALIGNMENT 8
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/*My own macros for implementation*/
#define WSIZE 8 //word size in bytes
#define DSIZE 16 //double word size in bytes
#define CHUNKSIZE (1<<11) //extend heap by this amount in bytes, also size of initial free block


/*Pack a size and allocated  bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/*Read and write a word at address p*/
#define GET(p) (*(unsigned int *)(p)) //unsgined int has size 4 Bytes
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size (in bytes) and allocated fields from address p*/
#define GET_SIZE(p) (GET(p) & ~0x7) //mask out last 3 bits, since those are needed for meta data
#define GET_ALLOC(p) (GET(p) & 0x1) //mask out all but last bit, since that is the allocated bit

/*Given block-pointer bp compute adress of its header and footer*/
#define HDRP(bp) ((char *)(bp) - WSIZE) //header is 4 bytes before bp
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP((char *)(bp))) - (2*WSIZE)) //footer follows directly after  payload, since size includes hdr/ftr we subtract DSIZE

/* Given block-pointer bp, compute adress of next and previous blocks, bp should be block pointer, not header/footer*/
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP((bp)))) //next block starts at end of ftr
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - (2*WSIZE))))

/* Macros for Explicit/Segregated free-list */

#define NEXT_FREE(bp) (*(char **)(bp))//returns pointer to next free block in free list
#define PREV_FREE(bp) (*(char **)(bp + WSIZE)) //returns pointer to prev free block in free list
#define NUMSEGS 12



#define MAX(x, y) ((x) > (y)? (x) : (y)) //max macro
#define MIN(x, y) ((x) > (y)? (y) : (x)) //max macro

#define SEGS(i) (*(char **)(segs+((WSIZE)*(i))))

static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void place(void *bp, size_t asize);

static unsigned int getList(size_t size);
static void seg_list_add(void *bp);
static void seg_list_delete(void *bp);
static void *find_fit_seg(size_t asize);

//private variables for the full program
static char *heap_listp; //pointer to the prologue block in the heap
static void *segs;

/* 
 * mm_init - initialize the malloc package.
 */

int mm_init(void)
{
   if ((heap_listp = mem_sbrk(19*WSIZE)) == (void *)-1) {
        return -1; //initial allocation failed
    }
    //heap_listp points to alignment padding block
    segs = heap_listp;
    PUT(heap_listp, PACK((14*WSIZE), 1)); //allocate space for the segregated lists
    PUT(heap_listp+(13*WSIZE),  PACK((14*WSIZE), 1)); //allocate space for the segregated lists

    heap_listp += (14*WSIZE); //heap_listp now points to "real" header.
    PUT(heap_listp, PACK((4*WSIZE), 1)); //Prologue Header
    PUT(heap_listp + (3*WSIZE), PACK((4*WSIZE), 1)); //Prologue Footer
    PUT(heap_listp + (4*WSIZE), PACK(0, 1)); //Epilogue Header
    heap_listp += (1*WSIZE); //heap_listp now points to payload of prologue block
    

    PREV_FREE(heap_listp) = NULL; //prev pointer of first free block is NULL
    NEXT_FREE(heap_listp) = NULL; //next pointer of first free block is NULL


    for (int i = 0; i < NUMSEGS; i++) {
        SEGS(i) = heap_listp;
    }

    //extend empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {
        return -1; //extend_heap failed
    }

    return 0; //everything worked

}





/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize; //adjusted block size
    char *bp;

    if (size == 0) {
        return NULL;
    }

    asize = ALIGN(size) + (2*WSIZE); //adjust block size to include overhead and alignment reqs

    if ((bp = find_fit_seg(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    bp = extend_heap(MAX(asize, CHUNKSIZE)/WSIZE); //no fit found, extend heap

    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }

    return NULL; //no fit found
    
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp)); //get size of block
    PUT(HDRP(bp), PACK(size, 0)); //set header to free
    PUT(FTRP(bp), PACK(size, 0)); //set footer to free
    coalesce(bp); //coalesce if possible
    

}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
     bool next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); //is next block allocated?
     size_t nextSize = GET_SIZE(HDRP(NEXT_BLKP(ptr))); //size of next block
     size_t oldSize = GET_SIZE(HDRP(ptr));
     size_t newSize = ALIGN(size) + (2*WSIZE); //size of new block
     size_t combineSize = oldSize+nextSize; //size of combined block
     char *newPtr;

     if (size == 0) {
        mm_free(ptr);
        return NULL;
     }

     if (ptr == NULL) {
        return mm_malloc(size);
     }
    
        
     if (newSize <= oldSize) {
        return ptr;
     } else {
        
        if (!next_alloc && (combineSize >= newSize)) {
                seg_list_delete(NEXT_BLKP(ptr));
                PUT(HDRP(ptr), PACK(combineSize, 1));
                PUT(FTRP(ptr), PACK(combineSize, 1));
                return ptr;
        }else{
            newPtr = mm_malloc(size);
            if (newPtr == NULL) { //check if malloc failed
                return NULL;
            }
            memcpy(newPtr, ptr, oldSize-DSIZE);
            mm_free(ptr);
            return newPtr;
        }
        



     }


}




//private helper functions

static void *extend_heap(size_t words) //extends the heap by 4*words bytes, returns address of first new free block, or NULL if extend_heap failed
{ 
    char *bp;
    size_t size;

    size = (words % 2) ? (words+1)*WSIZE : words*WSIZE; //make sure size is a multiple of 8 bytes

    if ((bp = mem_sbrk(size)) == (char *)-1) {
        return NULL; //mem_sbrk failed
    }

    //initialize free block header/footer and the epilogue header
    PUT(HDRP(bp), PACK(size, 0)); //free block header
    PUT(FTRP(bp), PACK(size, 0)); //free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    seg_list_add(bp);
    return bp;
}





//checks if coalescing is possible and returns pointer to coalesced block
static void *coalesce(void *bp) 
{
    bool prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); //is previous block allocated?
    bool next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); //is next block allocated?
    size_t size = GET_SIZE(HDRP(bp)); //size of current block

    if (prev_alloc && next_alloc) {
        seg_list_add(bp); //add block to free list
        return bp;
    } else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); //add size of next block to current block
        seg_list_delete(NEXT_BLKP(bp)); //remove next block from free list
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        seg_list_add(bp);
        return bp;
    } else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(FTRP(PREV_BLKP(bp)));
        seg_list_delete(PREV_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
        seg_list_add(bp);
        return bp;
    } else {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(FTRP(PREV_BLKP(bp)));
        seg_list_delete(PREV_BLKP(bp));
        seg_list_delete(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        seg_list_add(bp);
        return bp;
    }
}



static void place(void *bp, size_t asize) 
{
    size_t csize = GET_SIZE(HDRP(bp));

    if (csize-asize >= (4*WSIZE)) { //splitting possible
        seg_list_delete(bp); //remove block from free list
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp); //move bp to next block
        PUT(HDRP(bp), PACK(csize-asize, 0));
        PUT(FTRP(bp), PACK(csize-asize, 0));
        seg_list_add(bp); //coalesce if possible
    } else{
        seg_list_delete(bp); //remove block from free list
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }

} 

/*
static void *find_fit(size_t asize) 
{
    char *bp;
    
    for (bp = free_listp; GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp)) {
        if (GET_SIZE(HDRP(bp)) >= asize && !GET_ALLOC(HDRP(bp))) {
            return bp;
        }
    }
    
    return NULL; //no fit found
}

static void free_list_add(void *bp) 
{
    NEXT_FREE(bp) = free_listp;
    PREV_FREE(free_listp) = bp;
    PREV_FREE(bp) = NULL;
    free_listp = bp;
}

static void free_list_delete(void *bp) 
{
    if (PREV_FREE(bp) == NULL) { //bp was start of list
        free_listp = NEXT_FREE(bp);
    } else {
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    }
    PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);

}*/




//Methods for segregated lists:

static unsigned int getList(size_t size) 
{
    

    if (size <= (8*WSIZE)) {
        return 0;
    }

    if (size <= (16*WSIZE)) {
        return 1;
    }

    if (size <= (32*WSIZE)) {
        return 3;
    }

    if (size <= (64*WSIZE)) {
        return 4;
    }

    if (size <= (128*WSIZE)) {
        return 5;
    }
    

    if (size <= (256*WSIZE)) {
        return 6;
    }

    if (size <= (1024*WSIZE)) {
        return 7;
    }


    if (size <= (4096*WSIZE)) {
        return 8;
    }

    
    if (size <= (8192*WSIZE)) {
        return 9;
    }




    

    return 10;
}

static void seg_list_add(void *bp) 
{
    unsigned int list = getList(GET_SIZE(HDRP(bp)));
    NEXT_FREE(bp) = SEGS(list);
    PREV_FREE(SEGS(list)) = bp;
    PREV_FREE(bp) = NULL;
    SEGS(list) = bp;
}

static void seg_list_delete(void *bp) 
{
    unsigned int list = getList(GET_SIZE(HDRP(bp)));

    if (PREV_FREE(bp) == NULL) {
        SEGS(list) = NEXT_FREE(bp);
    }else{
        NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
    }
    
    PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
}

static void *find_fit_seg(size_t asize) 
{
    char *bp;

    for (int i = getList(asize); i < NUMSEGS; i++) {
        for (bp = SEGS(i); GET_ALLOC(HDRP(bp)) == 0; bp = NEXT_FREE(bp)) {
            if (GET_SIZE(HDRP(bp)) >= asize && !GET_ALLOC(HDRP(bp))) {
                return bp;
            }
        }
    }

    return NULL; //no fit found
}








