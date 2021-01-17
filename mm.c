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

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team-5",
    /* First member's full name */
    "Woosung Jeong",
    /* First member's email address */
    "jws0324@uos.ac.kr",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/**** 기본 상수 & 매크로 ****/
#define WSIZE 4             // Word Size
#define DSIZE 8             // Double-Word
#define CHUNKSIZE (1 << 12) // Extended heap 최대 확장 가능 크기

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc)) // 헤더에 (size | alloc) : 비트 연산 하여 정보 저장

/* Read and Write a word at address p */
#define GET(p) (*(unsigned int *)(p))              // ptr에 저장된 워드 리턴
#define PUT(p, val) (*(unsigned int *)(p) = (val)) // ptr에 저장된 워드에 val 넣기

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7) // 사이즈 게산
#define GET_ALLOC(p) (GET(p) & 0x1) // 할당 정보

/* 블락 포인터 bp에 대해서, Header와 Footer의 주소를 던진다. */
#define HDRP(bp) ((char *)(bp)-WSIZE)                        // Header Pointer : payload ptr - 1 WORD
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // Footer Pointer : payload ptr + block SIZE - DSIZE(==2*WORD : 뒤로 2칸)

/* 블락 포인터 bp에 대해서, 다음 block 과 이전 block 얻어오기 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE))) // Next Block ptr : payload ptr + block Size (get from my Header)
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))   // Prev Block ptr : payload ptr - prev block size (get from prev Footer)

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Alignment 유지시켜주기 위해서 반올림 작업 */
    /* 요청이 왔는데 홀수 words ? : single-word ==> 짝수 words (8의 배수) 로 만들어주기! */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* free block의 Header와 Footer & epilogue block의 Footer 를 초기화 시킨다. */

    /* 새로 들어온 block의 Header와 Footer 초기화 */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* 다음 block을 Epilogue block으로 만들어주기 */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    /* 연장시키기 전 마지막 block이 free였으면? */
    return coalesce(bp);
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* 초기 empty heap */
    /* 초기 4 워드 만들고, NULL인지 확인한다.) */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0); /* 첫 WORD에 0을 넣는다. */
    /*  Prologue Header & Footer */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));

    /* Epilogue Header */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));

    /* 초기화 후, 다음 부터 들어올 화살표 옮겨주기 ! */
    heap_listp += (2 * WSIZE);

    if (exted_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

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
    else
    {
        *(size_t *)p = size;
        return (void *)((char *)p + SIZE_T_SIZE);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    /* 해당 block의 size를 얻어온다. */
    size_t size = GET_SIZE(HDRP(bp));

    /* Header와 Footer에 할당상태 0으로 변경 */
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    /* immediately 합치기 */
    coalesce(bp);
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* 모두 할당되어 있는 경우 */
    if (prev_alloc && next_alloc)
        return bp;

    /* prev block만 free인 경우 */
    else if (!prev_alloc && next_alloc)
    {
        // prev block의 size를 얻어온다. (from its Footer)
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp); // bp는 free 된 총 block의 pointer로 유지
    }

    /* next block만 free인 경우 */
    else if (prev_alloc && !next_alloc)
    {
        // next block의 size를 얻어온다. (from its Header)
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));

        // FTRP는 Header에 입력된 size를 GET_SIZE하여 확인한다.
        // 따라서, Header에 size를 update해줬으면, Footer가 자동으로 변경된다.
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));

        // bp는 그대로 둔다. why? free된 block의 시작은 그대로다.
    }

    /* 모두 free인 경우 */
    else
    {
        size += (GET_SIZE(HDRP(PREV_BLKP(bp))) +
                 GET_SIZE(FTRP(NEXT_BLKP(bp))));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    // 합병 후, free 된 총 block의 최초 ptr을 return한다
    return bp;
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
