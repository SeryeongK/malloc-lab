/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* Basic constants and macros */
#define WSIZE 4             /* Word and header/footer size (bytes). 32비트 아키텍처에서 데이터를 처리할 때 더 쉽고 효율적으로 처리하기 위해서 */
#define DSIZE 8             /* Double word size (bytes) */
#define CHUNKSIZE (1 << 12) /* Extend heap by this amount (bytes) */

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) /* Pack a size and allocated bit into a word */

/* Read and wrtie a word at address p */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7) /* ~0x7 = 0xFFFFFFF8 = 하위 3비트를 제외한 비트들이 모두 1인 값. & 연산을 하면 8이하의 값은 나올 수 없다. */
#define GET_ALLOC(p) (GET(p) & 0x1) /* 하위 1비트를 추출하여 해당 값이 0이면 할당되어 있지 않은 상태이고, 1이면 할당되어 있는 상태 */

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)                        /* payload 시작주소에서 1워드 돌아감 => header의 시작주소 */
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) /* payload 시작주소에서 블록의 크기-2워드 더함 => footer의 시작주소 */

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE))) /* 다음 블록 payload 시작주소 */
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))   /* 이전 블록 payload 시작주소 */

#define NEXT_FIT /* Next-fit 사용할 때 */

//***************************mm******************************
static char *heap_listp;
#ifdef NEXT_FIT
static char *pointp; /* 블록을 탐색하는 포인터 */
#endif

extern int mm_init(void);
extern void *mm_malloc(size_t size);
extern void mm_free(void *ptr);

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); /* bp 이전 블록의 푸터에서 가져온 할당 정보 */
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); /* bp 다음 블록의 헤더에서 가져온 할당 정보 */
    size_t size = GET_SIZE(HDRP(bp));                   /* 현재 블록의 사이즈 */

    /* Case 1 앞, 뒤 모두 할당되어 있을 때 */
    if (prev_alloc && next_alloc)
    {
        return bp;
    }
    /* Case 2 앞은 할당되어 있고, 뒤는 가용블록일 때 */
    else if (prev_alloc && !next_alloc)
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); /* 현재 블록의 크기에 다음 블록의 크기를 더한 크기 */
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    /* Case 3 앞은 가용블록이고, 뒤는 할당되어 있을 때 */
    else if (!prev_alloc && next_alloc)
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); /* 현재 블록의 크기에 이전 블록의 크기를 더한 크기 */
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    /* Case 4 앞, 뒤 모두 가용블록일 때 */
    else
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); /* 현재 블록의 크기에 앞, 뒤 블록의 크기를 더한 크기 */
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
#ifdef NEXT_FIT
    pointp = bp;
#endif
    return bp;
}

/*
두 가지 경우에 호출됨
1. 힙이 초기화될 때
2. mm_malloc이 적당한 맞춤 fit을 찾지 못했을 때
*/
static void *extend_heap(size_t words) /* 워드 단위 */
{
    char *bp;
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; /* 데이터 크기 words가 홀수이면 짝수로 만들어 줌(padding) */
    if ((long)(bp = mem_sbrk(size)) == -1)                    /* 사이즈를 해당 크기만큼 늘릴 수 없으면 */
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */
#ifdef NEXT_FIT
    pointp = NEXT_BLKP(bp);
#endif

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1) /* 메모리 할당 오류가 발생했으면 */
        return -1;
    PUT(heap_listp, 0);                            /* Alignment padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1)); /* Prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2 * WSIZE);                     /* Prologue 헤더 건너뛰기. payload를 변수로 사용하기 때문 */
#ifdef NEXT_FIT
    /* 탐색 포인터를 힙의 처음으로 초기화 */
    pointp = heap_listp;
#endif
    /*  Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    return 0;
}

static void *next_fit(size_t asize)
{
    void *bp;
    void *old_pointp = pointp;
    /* 이전 탐색의 종료시점부터 / 크기가 0이 아닐 동안, 즉 에필로그 헤더가 나오기 전까지 / 다음 블록으로 이동하면서 */
    for (bp = pointp; GET_SIZE(HDRP(bp)); bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && GET_SIZE(HDRP(bp)) >= asize)
        {
            /* 탐색 포인터를 현재 선택된 가용 블록 다음으로 설정 */
            pointp = NEXT_BLKP(bp);
            return bp;
        }
    }
    /* 이전 탐색부터 찾았는데 사용할 수 있는 블록이 없다면, 다시 앞에서부터 탐색 */
    for (bp = heap_listp; bp < old_pointp; bp = NEXT_BLKP(bp))
    {
        if ((!GET_ALLOC(HDRP(bp))) && GET_SIZE(HDRP(bp)) >= asize)
        {
            /* 탐색 포인터를 현재 선택된 가용 블록 다음으로 설정 */
            pointp = NEXT_BLKP(bp);
            return bp;
        }
    }
    return NULL; /* No fit */
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));  /* 가용 블록의 크기 */
    if ((csize - asize) >= (2 * DSIZE)) /* 분할의 가치가 있음 */
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1)); /* 헤더가 있기 때문에 푸터 찾을 수 있음 */
        bp = NEXT_BLKP(bp);
#ifdef NEXT_FIT
        /* 탐색 포인터를 분할되고 남은 가용블록으로 설정 */
        pointp = bp;
#endif
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }
    else /* 남은 가용 블록의 크기가 2*DSIZE보다 작으면 */
    {
        /* 다 쓰기(패딩) */
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
#ifdef NEXT_FIT
        /* 탐색 포인터를 다음 블록으로 설정 */
        pointp = NEXT_BLKP(bp);
#endif
    }
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjsted block size */
    size_t extendsize; /* Amount to extend heap if no fit */
    char *bp;

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs */
    if (size <= DSIZE)
        asize = 2 * DSIZE; /* 왜 16바이트? 정렬조건인 더블 워드를 충족하기 위한 8바이트, 헤더와 푸터 오버헤드를 위한 8바이트 */
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE); /* 값으로 나눴을 때 나머지가 될 수 있는 수의 최댓값은 값-1 */

    /* Search the free list for a fit */
    if ((bp = next_fit(asize)) != NULL)
    {
        place(bp, asize); /* 배치하기 */
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) /* 바이트 단위 -> 워드 단위 */
        return NULL;
    place(bp, asize);

    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));
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
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr)) - DSIZE; /* 기존에 할당된 블록의 사이즈. */
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}