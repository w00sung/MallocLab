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

static char *heap_listp;
static char *next_fitp;
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
        PUT(FTRP(bp), PACK(size, 0));

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

    // 합병 시켰을 때, next_fitp의 행방이 묻히지 않게하자.

    // 묻혔다면? -- bp 뒤에 있고, 다음 block 안에 있다.
    if ((next_fitp > (char *)bp) && (next_fitp < (char *)NEXT_BLKP(bp)))
        next_fitp = bp; //bp는 free 되어있음

    // 합병 후, free 된 총 block의 최초 ptr을 return한다
    return bp;
}

// word 단위를 받는다.
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    /* Alignment 유지시켜주기 위해서 반올림 작업 */
    /* 요청이 왔는데 홀수 words ? : single-word ==> 짝수 words (8의 배수) 로 만들어주기! */
    /* 무조건 Epilogue 만들 수 있음 */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;

    // bp는 size만큼 늘려주고 난 후의 ptr : 이전 heap의 끝
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

// tricky한 변수
int mm_init(void)
{
    /* 초기 empty heap */
    /* 초기 4 워드 만들고, NULL인지 확인한다.) */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return -1;

    // 초기화 할 때, next_fit 시작점 연결
    PUT(heap_listp, 0); /* 첫 WORD에 0을 넣는다. */
    /*  Prologue Header & Footer */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));

    /* Epilogue Header */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));

    /* 초기화 후, 다음 부터 들어올 화살표 옮겨주기 ! */
    heap_listp += (2 * WSIZE);
    next_fitp = heap_listp;

    // CHUNKSIZE만큼 힙 초기 생성하기
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
/* First Fit */
static void *find_fit(size_t asize)
{
    char *strt = heap_listp;
    // Size가 0이면 while 문 멈춘다.
    while (GET_SIZE(HDRP(strt)))
    {
        strt = NEXT_BLKP(strt);
        if (GET_SIZE(HDRP(strt)) >= asize && !GET_ALLOC(HDRP(strt)))
        {
            return strt;
        }
    }
    // printf("I Can't FIND IT");
    return NULL;
}

static void *find_next_fit(size_t asize)
{
    // printf("LETS FIND OUT !\n");
    char *strt = next_fitp;

    // 나를 만나기 직전까지 달린다.
    while (NEXT_BLKP(strt) != next_fitp)
    {
        // strt도 검정하고 넘어간다.
        if (!GET_ALLOC(HDRP(strt)) && GET_SIZE(HDRP(strt)) >= asize)
        {
            // printf(" FIND IT !!!!!! : %p\n", strt);
            next_fitp = strt;
            return strt;
        }

        strt = NEXT_BLKP(strt);
        // printf("Coming NEXT, NOW : %p\n", strt);

        // 끝으로 왔으면?
        if (GET_SIZE(HDRP(strt)) == 0)
        {
            // printf("going heap_listp, NOW L %p\n", strt);
            // 처음으로 돌려놓는다.
            strt = heap_listp;
        }
    }

    printf("I CANT FIND IT\n NOW : %p, next_fitp : %p", strt);
    return NULL;
}

// find_fit 된 bp에 asize 할당 시키기 == size update
static void place(void *bp, size_t asize)
{
    size_t old_size;
    size_t diff;
    old_size = GET_SIZE(HDRP(bp));

    diff = old_size - asize;

    // realloc 추가의 경우 (이미 검증하고 왔음)

    // 최소 사이즈 보다 적게 남으면 잘 넣은 거임 그대로 유지
    if (diff < 2 * DSIZE)
    {
        PUT(HDRP(bp), PACK(old_size, 1));
        PUT(FTRP(bp), PACK(old_size, 1));
    }

    // 최소 사이즈 보다 크거나 같게 남으면, 분리해줘야함
    // 남은 부분 Header & Footer에 명시하기
    else if (diff >= 2 * DSIZE)
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        PUT(HDRP(NEXT_BLKP(bp)), PACK(diff, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(diff, 0));
    }
}

static size_t makeSize(size_t size)
{
    size_t asize; /* 적용될 size */

    /* Header & Footer + Align 조건 충족 */
    if (size <= DSIZE)
        // 최소 4WORD는 필요
        asize = 2 * DSIZE;
    else
        // Header + Footer & 반올림
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    return asize;
}

void *mm_malloc(size_t size)
{

    size_t asize;      /* 적용될 size */
    size_t extendsize; /* 맞는 것을 못 찾았을 때, 필요한 추가적인 heap 크기 */
    char *bp;

    if (size == 0)
        return NULL;

    asize = makeSize(size);

    /* 적절한 fit 장소를 찾았다! */
    if ((bp = find_next_fit(asize)) != NULL)
    {
        place(bp, asize);
        // 할당 후, 다음 block으로 밀어준다.
        // next_fitp = NEXT_BLKP(bp);
        return bp;
    }

    /* 찾지 못했다면 ? */
    /* 현재 힙의 총 사이즈와, 내가 적용해야할 size 중 큰 값을 */
    // 2배할래? 아님 그 이상?
    // -- 요구가 2배보다 크면 그만큼 늘려줌
    extendsize = MAX(asize, CHUNKSIZE);

    // 추가하고 싶은만큼 extend 한다.
    // heap의 과거 brk 주변(brk or brk-DSIZE)을 return 할 것임
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;

    place(bp, asize);
    return bp;
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

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;

    size_t copySize;
    size_t newSize;

    copySize = (size_t)(GET_SIZE(HDRP(oldptr)));
    newSize = makeSize(size);

    if (oldptr == NULL)
        return NULL;

    if (newSize == copySize)
        return oldptr;

    if (newSize > copySize)
    {
        void *nxtblock;
        size_t nxtSize;
        // 현재 다음 block
        nxtblock = NEXT_BLKP(ptr);
        nxtSize = GET_SIZE(HDRP(nxtblock));
        if ((nxtSize - DSIZE) >= (newSize - copySize) &&
            !(GET_ALLOC(HDRP(nxtblock))))
        {
            PUT(HDRP(ptr), PACK(newSize, 1));
            PUT(FTRP(ptr), PACK(newSize, 1));
            // 새로워진 다음 block
            PUT(HDRP(NEXT_BLKP(ptr)), PACK(nxtSize - (newSize - copySize), 0));
            PUT(FTRP(NEXT_BLKP(ptr)), PACK(nxtSize - (newSize - copySize), 0));
            return oldptr;
        }
    }
    newptr = mm_malloc(size);

    // cut
    if (newSize < copySize)
        copySize = size;
    // newPtr에
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
