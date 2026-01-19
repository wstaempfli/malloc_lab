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
#include <stdbool.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* single word (4) or double word (8) alignment */
//adapt minsize for explicit list
#define ALIGNMENT 8
#define WSIZE 4             /* word size */
#define DSIZE 8             /* doubleword size */
#define CHUNKSIZE (1<<12)   /* Extend heap by this amount (bytes) */
#define MINSIZE 24

//------------EXPLICIT-LIST MACROS/vars-------------------------
#define GET_NEXT(ptr) (*(char **)(ptr))
#define GET_PREV(ptr) (*(char **)(ptr + 8))
#define SET_NEXT(ptr, nxt) (GET_NEXT(ptr) = nxt)
#define SET_PREV(ptr, prev) (GET_PREV(ptr) = prev)

//------------SEGREGATED-LIST MACROS/vars-------------------------
#define LIST_LIMT 20
void *seg_lists[LIST_LIMT];

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

static int get_idx(size_t size){
    int list = 0;
    size_t threshold = 32;
    while(list < LIST_LIMT -1){
        if(size <= threshold){
            return list;
        }
        list++;
        threshold <<= 1;
    }
    return list;
}
static void insert_node_seg(void *bp)
{
    int idx = get_idx(SIZE(HDRP(bp)));
    if(seg_lists[idx] == NULL){
        seg_lists[idx] = bp;
        SET_NEXT(bp, NULL);
        SET_PREV(bp, NULL);
    }else {
        SET_PREV(seg_lists[idx], bp); 
        SET_NEXT(bp, seg_lists[idx]); 
        SET_PREV(bp, NULL);       

        seg_lists[idx] = bp;
    }
}


//this function removes a node from the free list (watch out for edge cases)
static void delete_node_seg(void *bp){
    char *prev, *nxt;
    int idx = get_idx(SIZE(HDRP(bp)));
    //delete root (what if only root exists?)
    if(bp == seg_lists[idx]){
        seg_lists[idx] = GET_NEXT(bp);
        if(seg_lists[idx] != NULL){
            SET_PREV(seg_lists[idx], NULL);
        }
    } 
    //delete last Node
    else if(GET_NEXT(bp) == NULL) {
        prev = GET_PREV(bp);
        SET_NEXT(prev, NULL);
    }
    //delete middle node
    else {
        prev = GET_PREV(bp);
        nxt = GET_NEXT(bp);
        SET_NEXT(prev, nxt);
        SET_PREV(nxt, prev);
    }
}
//merges free blocks laying next to each other
static void *coalesce(void * ptr)
{
    size_t last_a = GET_ALLOC(HDRP(LAST_BLKP(ptr)));
    size_t next_a = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));

    if(last_a == 0){
        //remove from free list
        delete_node_seg(LAST_BLKP(ptr));
        size_t cur_size = SIZE(HDRP(ptr));
        size_t prev_size = SIZE(HDRP(LAST_BLKP(ptr)));
        ptr = LAST_BLKP(ptr);
        PUT(HDRP(ptr), PACK(cur_size + prev_size, 0));
        PUT(FTRP(ptr), PACK(cur_size + prev_size, 0));
    }
    if(next_a == 0){
        //remove from free list
        delete_node_seg(NEXT_BLKP(ptr));
        size_t cur_size = SIZE(HDRP(ptr));
        size_t next_size = SIZE(HDRP(NEXT_BLKP(ptr)));
        PUT(HDRP(ptr), PACK(cur_size + next_size, 0));
        PUT(FTRP(ptr), PACK(cur_size + next_size, 0));
    }
    insert_node_seg(ptr);
    return ptr;
}

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
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    for(int i = 0; i < LIST_LIMT; i++){
        seg_lists[i] = NULL;
    }
    
    //mem_sbrk return a pointer to -1 if something went wrong
    if((heap_listp = mem_sbrk(4 * WSIZE)) == (void *) -1) return -1;

    PUT(heap_listp, 0); //initial 4 bytes padding
    PUT(heap_listp + (WSIZE), PACK(DSIZE, 1)); //prologue hdr
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); //prologue ftr
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); //epilogue hdr

    heap_listp += (2*WSIZE);
    if(extend_heap(CHUNKSIZE/WSIZE) == NULL) return -1;
    return 0;
}
//splits and allocates blocks
static void place(void *bp, size_t asize){
    size_t bsize = SIZE(HDRP(bp));
    // unused part of block is large enough to be one on its own -> split it 
    if(bsize - asize >= MINSIZE){
        size_t remainder = bsize - asize;
        delete_node_seg(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(remainder, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(remainder, 0));
        insert_node_seg(NEXT_BLKP(bp));
    } else {
        delete_node_seg(bp);
        PUT(HDRP(bp), PACK(bsize, 1));
        PUT(FTRP(bp), PACK(bsize, 1));
    }
}
//traverses List and returns pointer to first free fitting block
static void *find_fit(size_t size)
{
    int idx = get_idx(size);

    //start at the correct index, but keep going up if empty
    for(int i = idx;i < LIST_LIMT; i++) {
        void *bp = seg_lists[i];

        while(bp != NULL){
            if(SIZE(HDRP(bp)) >= size){
                return bp;
            }
            bp = GET_NEXT(bp);
        }
    }
    return NULL;
}
/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize, esize;
    char * bp;
    if(size == 0) return NULL;
    //Adjust block size to include overhead (+ DSIZE) and alignment reqs (mult of DSIZE).
    if(size <= 2 * DSIZE){
        asize = MINSIZE;
    } else {
        asize = ALIGN(size) + DSIZE;
    }
    
    //find fit 
    if((bp = find_fit(asize)) != NULL){
        place(bp, asize);
    } else {
        //extend heap
        esize = (CHUNKSIZE > asize) ? CHUNKSIZE : asize;
        if((bp = extend_heap(esize/WSIZE)) == NULL) return NULL;
        place(bp, asize);
    }
    return bp;
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
    bool next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr))); //is next block allocated?
    size_t next_size = SIZE(HDRP(NEXT_BLKP(ptr))); //size of next block
    size_t old_size = SIZE(HDRP(ptr)); //size of existing block
    size_t new_size = ALIGN(size) + (2*WSIZE); 
    size_t combined_size = old_size + next_size; //size of merged block
    char *new_ptr;


    if(size == 0) return NULL;
    if(ptr == NULL) return mm_malloc(size);

    if(new_size <= old_size) return ptr;

    if(!next_alloc && (combined_size >= new_size)) {
        delete_node_seg(NEXT_BLKP(ptr));
        PUT(HDRP(ptr), PACK(combined_size, 1));
        PUT(FTRP(ptr), PACK(combined_size, 1));
        return ptr;
    } else {
        new_ptr = mm_malloc(size);
        if(new_ptr == NULL) { //check if alloc failed
            return NULL;
        }
        memcpy(new_ptr, ptr, old_size - DSIZE); //copy old data to new block
        mm_free(ptr);
        return new_ptr;
    }
}














