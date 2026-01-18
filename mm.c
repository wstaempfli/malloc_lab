/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
#define WSIZE 4             /* word size */
#define DSIZE 8             /* doubleword size */
#define CHUNKSIZE (1<<12)   /* Extend heap by this amount (bytes) */

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)
#define PACK(size, alloc) ((size) | (alloc))
#define PUT(p, val) (*(unsigned int *)(p) = (val)) //write 4 bytes
#define GET(p) (*(unsigned int *)(p)) //Read content of word 
#define GET_ALLOC(p) (*(unsigned int *)(p) & 0x1)
#define SIZE(p) ((GET(p)) & ~0x7) //Get word value and 0 out last three bits

#define HDRP(p) ((char *)(p) - WSIZE)
#define FTRP(p) ((char *)(p) + SIZE(HDRP(p)) - 8) //retreive block size from header (-4 bytes) then add that to the ptr - 4
#define NEXT_BLKP(p) ((char *)(p) + SIZE((char *)(p) - 4))
#define LAST_BLKP(p) ((char *)(p) - SIZE((char *)(p) - 8)) //read footer of prev move ptr back by its size
static char *heap_listp;

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))


void *coalesce(void * ptr)
{
    size_t last_a = GET_ALLOC(HDRP(LAST_BLKP(ptr)));
    size_t next_a = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    if(last_a == 0){
        size_t cur_size = SIZE(HDRP(ptr));
        size_t prev_size = SIZE(HDRP(LAST_BLKP(ptr)));
        ptr = LAST_BLKP(ptr);
        PUT(HDRP(ptr), PACK(cur_size + prev_size, 0));
        PUT(FTRP(ptr), PACK(cur_size + prev_size, 0));
    }
    if(next_a == 0){
        size_t cur_size = SIZE(HDRP(ptr));
        size_t next_size = SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(cur_size + next_size, 0));
        PUT(FTRP(ptr), PACK(cur_size + next_size, 0));
    }

    return ptr;
}
/* 
 * mm_init - initialize the malloc package.
 */
//extend heap by words many words called (1) in init phase and (2) when no block is free
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    //allocate an even num of words to keep 8 byte alignment
    //size in bytes
    //bp points to first byte outside of old heap
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if(((bp = mem_sbrk(size)) == (void *)-1)) return NULL;
    //note: bp points to payload area of new free block, thus new header is overwriting old epilogue header
    PUT(HDRP(bp), PACK(size, 0)); //free block hdr
    PUT(FTRP(bp), PACK(size, 0)); //free block ftr
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); //new epilogue header 
    
    return coalesce(bp);

}
int mm_init(void)
{
    //mem_sbrk return a pointer to -1 if something went wrong
    if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) -1) return -1;

    PUT(heap_listp, 0); //initial 4 bytes padding
    PUT(heap_listp + (WSIZE), PACK(DSIZE, 1)); //prologue hdr
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); //prologue ftr
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); //epilogue hdr

    heap_listp += (2*WSIZE);
    if(extend_heap(CHUNKSIZE/WSIZE) != 0) return -1;
    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    int newsize = ALIGN(size + SIZE_T_SIZE);
    void *p = mem_sbrk(newsize);
    if (p == (void *)-1)
	return NULL;
    else {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














