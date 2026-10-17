/*
 * Copyright (C) 2026 TheCRatTime
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef DBG_ALLOC_DBG_ALLOC_H_
#define DBG_ALLOC_DBG_ALLOC_H_

#include <stddef.h>

#define CANARY_VALUE 0x43 /* Canary value */
#define CANARY_SIZE  8    /* Canary size */

#define POISON_VALUE 0xCC /* Poison value */
#define POISON_SIZE  8    /* Poison size */

#define SH_MEM_VALID 0x00 /* Shadow memory valid. */
#define SH_MEM_FREED 0xFD /* Shadow memory freed. */
#define SH_MEM_BAD   0xBD /* Shadow memory bad. */

#define DBG_MAGIC_1 0x01 /* Magic 1 */
#define DBG_MAGIC_2 0x02 /* Magic 2 */
#define DBG_MAGIC_3 0x03 /* Magic 3 */
#define DBG_MAGIC_4 0x04 /* Magic 4 */

typedef unsigned char dbg_uchar;

/* Internal header of memory data */
typedef struct DbgSymbol {
  dbg_uchar magic[4];   /* Magic characters */

  dbg_uchar freed;      /* Was block freed */
  dbg_uchar padding[3]; /* Padding from 1 to 4 */
  
  size_t real_size;     /* Size of allocated data */
} DbgSymbol;

/* Pointer list. */
typedef struct PtrLinkedList {
  void* ptr;                  /* Real pointer */
  struct PtrLinkedList* next; /* Next pointer */
} PtrLinkedList;

/* Chunk of memory */
typedef struct MemChunk {
  char* memory;       /* Pointer to memory */
  size_t memory_size; /* Total memory size */
  size_t left_bytes;  /* Left bytes of memory */
} MemChunk;

/* Allocator context. */
typedef struct AllocatorCtx {
  void* self_ptr;             /* For free */
  size_t total_allocated;     /* Total allocated pointers */
  size_t total_freed;         /* Total freed pointers */

  PtrLinkedList* head_of_pll; /* Head of all pointers */
  char* pll_memory;           /* PtrLinkedList memory */
  size_t pll_memory_size;     /* PtrLinkedList memory size */
  size_t pll_left;            /* PtrLinkedList left bytes */
  PtrLinkedList* cur_pll;     /* Current PtrLinkedList* node */
  dbg_uchar have_overflow;    /* Overflow check. */

  char* sh_memory;            /* Shadow memory */
  size_t sh_memory_size;      /* Shadow memory size */
  
  MemChunk* memory;           /* Memory for allocate */
} AllocatorCtx;

/* Initial function for allocator
   Returns 0 on success, -1 at error. */
int InitAllocator(size_t);

/* Allocate memory.
   Returns pointer to data on success, NULL at error */
void* DbgAlloc(size_t);

/* Free memory, that allocated by DbgAlloc */
void DbgFree(void*);

#ifdef WRAP_REAL_MALLOC
# define DEFER_WRAP_MALLOC
#endif

/* Check pointer for corruption. */
void CheckPtr(void*);

/* Print log about all information */
void AllocatorLog(void);

/* Clear debug allocator */
void KillAllocator(void);

/* Check all pointers in allocator. */
void AllocatorFullCheck(void);

/* Check pointer for use now. */
void CheckForUse(void*);

/* Get shadow memory offset for PTR */
size_t GetShMemOffset(void*);

/* Get count bytes of shadow memory. */
size_t GetShMemCountBytes(size_t);

/* Check all bytes in PTR with size SIZE */
#define SH_MEM_CHECK(ptr, size) do { \
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL); \
  size_t offset = GetShMemOffset(ptr); \
  size_t count = GetShMemCountBytes(size); \
  size_t i = 0; \
  \
  if (ptr == NULL) { \
    break; \
  } \
  \
  for (; i < count; i++) { \
    if (ctx->sh_memory[offset+i] != SH_MEM_VALID) { \
      printf("SH_MEM_CHECK: *** pointer to a canary/debug info/freed block " \
             "***\n=== aborted (core dumped) ===\n"); \
      abort(); \
    } \
  } \
} while(0)

#endif /* DBG_ALLOC_DBG_ALLOC_H_ */

#ifdef DBG_ALLOC_SOURCE

#include <stdio.h>
#include <stdlib.h>

static AllocatorCtx* AllocatorCtxManager(int mode, AllocatorCtx* ctx) {
  static AllocatorCtx* internal_ctx = NULL;
  if (mode == 1) {
    internal_ctx = ctx;
  }

  return internal_ctx;
}

static PtrLinkedList* PLLAllocate(AllocatorCtx* ctx) {
  size_t aligned = (sizeof(PtrLinkedList) + 7) & (size_t)~7;
  PtrLinkedList* pll = NULL;
  
  if (ctx == NULL) {
    return NULL;
  }

  if (ctx->pll_left < aligned) {
    return NULL;
  }

  pll = (PtrLinkedList*)(void*)&ctx->
    pll_memory[ctx->pll_memory_size - ctx->pll_left];

  ctx->pll_left -= aligned;

  pll->ptr = NULL;
  pll->next = NULL;

  return pll;
}

static void FillShadowMem(char* sh_mem, size_t size) {
  size_t i = 0;
  
  for (; i < size; i++) {
    sh_mem[i] = SH_MEM_VALID;
  }
}

int InitAllocator(size_t mem_size) {
  size_t aligned1 = 0;
  size_t aligned2 = 0;
  size_t aligned3 = 0;
  size_t aligned4 = 0;
  size_t aligned5 = 0;
  size_t total = 0;
  size_t for_ptrs = 0;
  char* slice = NULL;
  AllocatorCtx* allocator_ctx = NULL;
  MemChunk* chunk = NULL;
  
  if (mem_size == 0) {
    return -1;
  }

  for_ptrs = (mem_size >> 3) + (mem_size & 7 ? 1 : 0);

  aligned1 = (sizeof(AllocatorCtx) + 7) & (size_t)~7;
  aligned2 = (sizeof(MemChunk) + 7) & (size_t)~7;
  aligned3 = (mem_size + 7) & (size_t)~7;
  aligned4 = (for_ptrs + 7) & (size_t)~7;
  aligned5 = ((aligned3 >> 3) + 7) & (size_t)~7;
  total = aligned1 + aligned2 + aligned3 + aligned4 + aligned5;

  slice = (char*)malloc(total);

  if (slice == NULL) {
    return -1;
  }

  allocator_ctx = (AllocatorCtx*)(void*)slice;
  
  chunk = (MemChunk*)(void*)(slice + aligned1);
  chunk->memory = (slice + aligned1 + aligned2);
  chunk->memory_size = aligned3;
  chunk->left_bytes = aligned3;

  allocator_ctx->memory = chunk;
  allocator_ctx->pll_memory = (slice + aligned1 + aligned2 + aligned3);
  allocator_ctx->pll_memory_size = aligned4;
  allocator_ctx->pll_left = aligned4;
  allocator_ctx->total_allocated = 0;
  allocator_ctx->total_freed = 0;
  allocator_ctx->self_ptr = (void*)slice;
  allocator_ctx->head_of_pll = PLLAllocate(allocator_ctx);
  allocator_ctx->cur_pll = allocator_ctx->head_of_pll;
  allocator_ctx->have_overflow = 0;

  allocator_ctx->sh_memory = slice + aligned1 + aligned2 + aligned3 + aligned4;
  allocator_ctx->sh_memory_size = aligned5;

  FillShadowMem(allocator_ctx->sh_memory, aligned5);

  (void)AllocatorCtxManager(1, allocator_ctx);
  
  return 0;
}

static void* ChunkAlloc(MemChunk* chunk, size_t bytes) {
  if (chunk == NULL) {
    return NULL;
  }

  if (chunk->left_bytes < bytes) {
    return NULL;
  }

  chunk->left_bytes -= bytes;

  return (void*)&chunk->memory[chunk->memory_size - (chunk->left_bytes+bytes)];
}

void* DbgAlloc(size_t bytes) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  
  size_t aligned = (bytes + 7) & (size_t)~7;
  size_t canary = ((CANARY_SIZE << 2) + 7) & (size_t)~7;
  size_t symbol = (sizeof(DbgSymbol) + 7) & (size_t)~7;
  
  size_t total = (symbol + aligned + canary + 7) & (size_t)~7;
  
  char* mem = NULL;
  size_t i = 0;
  DbgSymbol* header = NULL;
  void* ret = NULL;
  char* tmp = NULL;
  size_t sh_mem_offset = 0;
  size_t sh_mem_count = 0;
  
  if (bytes == 0) {
    return NULL;
  }

  if (ctx == NULL) {
    printf("DbgAlloc: *** context not initialized ***\n"
           "=== aborted ===\n");
    abort();
  }

  mem = (char*)ChunkAlloc(ctx->memory, total);
  if (mem == NULL) {
    return NULL;
  }

  header = (DbgSymbol*)(void*)mem;
  header->magic[0] = DBG_MAGIC_1;
  header->magic[1] = DBG_MAGIC_2;
  header->magic[2] = DBG_MAGIC_3;
  header->magic[3] = DBG_MAGIC_4;
  header->freed = 0;
  header->real_size = aligned;
  
  sh_mem_offset = (size_t)(((char*)header - ctx->memory->memory) >> 3);
  sh_mem_count = ((sizeof(DbgSymbol) + 7) & (size_t)~7) >> 3;

  for (; i < sh_mem_count; i++) {
    ctx->sh_memory[sh_mem_offset+i] = SH_MEM_BAD;
  }

  tmp = mem + symbol;
  
  sh_mem_offset = (size_t)((tmp - ctx->memory->memory) >> 3);
  sh_mem_count = ((CANARY_SIZE + 7) & (size_t)~7) >> 3;
  
  for (i = 0; i < sh_mem_count; i++) {
    ctx->sh_memory[sh_mem_offset+i] = SH_MEM_BAD;
  }

  for (i = 0; i < CANARY_SIZE; i++) {
    *(tmp++) = CANARY_VALUE;
  }

  tmp = mem + symbol + CANARY_SIZE + aligned;

  sh_mem_offset = (size_t)((tmp - ctx->memory->memory) >> 3);
  sh_mem_count = ((CANARY_SIZE + 7) & (size_t)~7) >> 3;
  
  for (i = 0; i < sh_mem_count; i++) {
    ctx->sh_memory[sh_mem_offset+i] = SH_MEM_BAD;
  }

  for (i = 0; i < CANARY_SIZE; i++) {
    *(tmp++) = CANARY_VALUE;
  }

  ret = (void*)(mem + symbol + CANARY_SIZE);

  ctx->total_allocated++;

  if (!ctx->have_overflow) {
    ctx->cur_pll->ptr = ret;
    ctx->cur_pll->next = PLLAllocate(ctx);
    if (ctx->cur_pll->next == NULL) {
      ctx->have_overflow = 1;
    } else {
      ctx->cur_pll = ctx->cur_pll->next;
    }
  }

  return ret;
}

void DbgFree(void* ptr) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  DbgSymbol* debuginfo = NULL;
  char* canary1 = NULL;
  char* canary2 = NULL;
  size_t i = 0;
  size_t sh_mem_offset = 0;
  size_t sh_mem_count = 0;

  if (ptr == NULL) {
    return;
  }

  if (ctx == NULL) {
    printf("DbgFree: *** not set context ***\n=== abort ===\n");
    abort();
  }

  if ((char*)ptr < (ctx->memory->memory + ((sizeof(DbgSymbol) + 7)
                                           & (size_t)~7)) ||
      (char*)ptr >= (ctx->memory->memory + ctx->memory->memory_size - 1)) {
    printf("DbgFree: *** dangling pointer detected ***\n"
           "(pointer: %p)\n"
           "=== aborted (core dumped) ===\n", ptr);
    abort();
  }

  debuginfo = (DbgSymbol*)(void*)((char*)ptr - ((sizeof(DbgSymbol)+7) &
                                                 (size_t)~7) - CANARY_SIZE);

  if (debuginfo->magic[0] != DBG_MAGIC_1 ||
      debuginfo->magic[1] != DBG_MAGIC_2 ||
      debuginfo->magic[2] != DBG_MAGIC_3 ||
      debuginfo->magic[3] != DBG_MAGIC_4) {
    printf("DbgFree: *** heap corruption (invalid magic) ***\n"
           "=== aborted (core dumped) ===\n");
    abort();
  }

  if (debuginfo->freed) {
    printf("DbgFree: *** double free detected ***\n"
           "(pointer: %p)\n"
           "=== aborted (core dumped) ===\n", ptr);
    abort();
  }

  sh_mem_offset = (size_t)(((char*)ptr - ctx->memory->memory) >> 3);
  sh_mem_count = (debuginfo->real_size) >> 3;

  for (; i < sh_mem_count; i++) {
    if (ctx->sh_memory[sh_mem_offset+i] != SH_MEM_VALID) {
      printf("DbgFree: *** pointer to a canary/debug info/freed block ***\n"
             "(pointer: %p)\n"
             "=== aborted (core dumped) ===\n", ptr);
      abort();
    }
  }

  canary1 = (char*)((char*)ptr - CANARY_SIZE);
  canary2 = (char*)((char*)ptr + debuginfo->real_size);

  for (i = 0; i < CANARY_SIZE; i++) {
    if (canary1[i] != CANARY_VALUE || canary2[i] != CANARY_VALUE) {
      printf("DbgFree: *** stack smashing detected ***\n"
             "(pointer: %p)\n"
             "=== aborted (core dumped) ===\n", ptr);
      abort();
    }
  }
  
  debuginfo->freed = 1;

  for (i = 0; i < POISON_SIZE; i++) {
    ((char*)ptr)[i] = POISON_VALUE;
  }
  
  for (i = 0; i < sh_mem_count; i++) {
    ctx->sh_memory[sh_mem_offset+i] = SH_MEM_FREED; 
  }

  ctx->total_freed++;
}

void CheckPtr(void* ptr) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  DbgSymbol* debuginfo = NULL;
  char* canary1 = NULL;
  char* canary2 = NULL;
  int i = 0;

  if (ptr == NULL) {
    return;
  }

  if (ctx == NULL) {
    printf("CheckPtr: *** not set context ***\n=== abort ===\n");
    abort();
  }

  if ((char*)ptr < (ctx->memory->memory + ((sizeof(DbgSymbol) + 7)
                                           & (size_t)~7)) ||
      (char*)ptr >= (ctx->memory->memory + ctx->memory->memory_size - 1)) {
    printf("CheckPtr: *** dangling pointer detected ***\n"
           "(pointer: %p)\n"
           "=== aborted (core dumped) ===\n", ptr);
    abort();
  }

  debuginfo = (DbgSymbol*)(void*)((char*)ptr - ((sizeof(DbgSymbol) + 7) &
                                                (size_t)~7) - CANARY_SIZE);

  if (debuginfo->magic[0] != DBG_MAGIC_1 ||
      debuginfo->magic[1] != DBG_MAGIC_2 ||
      debuginfo->magic[2] != DBG_MAGIC_3 ||
      debuginfo->magic[3] != DBG_MAGIC_4) {
    printf("CheckPtr: *** heap corruption (invalid magic) ***\n"
           "=== aborted (core dumped) ===\n");
    abort();
  }

  canary1 = (char*)((char*)ptr - CANARY_SIZE);
  canary2 = (char*)((char*)ptr + debuginfo->real_size);

  for (; i < CANARY_SIZE; i++) {
    if (canary1[i] != CANARY_VALUE || canary2[i] != CANARY_VALUE) {
      printf("CheckPtr: *** stack smashing detected ***\n"
           "(pointer: %p)\n"
             "=== aborted (core dumped) ===\n", ptr);
      abort();
    }
  }
}

void AllocatorLog(void) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  DbgSymbol* debuginfo = NULL;
  PtrLinkedList* cur_pll = NULL;

  if (ctx == NULL) {
    return;
  }

  cur_pll = ctx->head_of_pll;

  printf("=== ALLOCATOR DUMP ===\n");

  while (cur_pll != NULL && cur_pll->ptr != NULL) {
    printf("--- pointer: %p ---\n", cur_pll->ptr);
    debuginfo = (DbgSymbol*)(void*)((char*)cur_pll->ptr -
                                    ((sizeof(DbgSymbol) + 7) & (size_t)~7)
                                      - CANARY_SIZE);
    printf("size: %lu\n", (unsigned long)debuginfo->real_size);
    printf("freed: %s\n", (debuginfo->freed ? "yes" : "no"));
    printf("magic: %x%x%x%x\n",
           debuginfo->magic[0], debuginfo->magic[1],
           debuginfo->magic[2], debuginfo->magic[3]);
    
    cur_pll = cur_pll->next;
  }

  if (ctx->have_overflow) {
    printf("... (There isn't all pointers) ...\n");
  }

  printf("----------------------\n");

  printf("Allocated blocks: %lu\n"
         "Freed blocks:     %lu\n",
         (unsigned long)ctx->total_allocated,
         (unsigned long)ctx->total_freed);

  printf("======================\n");
}

void KillAllocator(void) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  
  if (ctx == NULL) {
    return;
  }

  AllocatorFullCheck();

  AllocatorCtxManager(1, NULL);

  free(ctx->self_ptr);
}

void AllocatorFullCheck(void) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  PtrLinkedList* pll = NULL;
  
  if (ctx == NULL) {
    return;
  }

  pll = ctx->head_of_pll;

  while (pll != NULL && pll->ptr != NULL) {
    int i = 0;
    
    CheckPtr(pll->ptr);

    if (((DbgSymbol*)(void*)((char*)pll->ptr - CANARY_SIZE -
        ((sizeof(DbgSymbol) + 7) & (size_t)~7)))->freed == 0) {
      printf("CheckPtr: *** memory leak detected ***\n"
             "(pointer: %p)\n"
             "=== aborted (core dumped) ===\n", pll->ptr);
      abort();
    }

    for (; i < POISON_SIZE; i++) {
      if (((char*)(pll->ptr))[i] != POISON_VALUE) {
        printf("CheckPtr: *** Use-After-Free detected (poison corrupted) ***\n"
               "(pointer: %p)\n"
               "=== aborted (core dumped) ===\n", pll->ptr);
        abort();
      }
    }
    
    pll = pll->next;
  }

  if (ctx->total_allocated > ctx->total_freed) {
    printf("CheckPtr: *** memory leak detected ***\n"
           "=== aborted (core dumped) ===\n");
    abort();
  }
}

void CheckForUse(void* ptr) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  DbgSymbol* debuginfo = NULL;
  size_t i = 0;
  size_t sh_mem_offset = 0;
  size_t sh_mem_count = 0;

  if (ptr == NULL) {
    return;
  }

  if (ctx == NULL) {
    printf("CheckForUse: *** not set context ***\n=== abort ===\n");
    abort();
  }

  CheckPtr(ptr);

  debuginfo = (DbgSymbol*)(void*)((char*)ptr - ((sizeof(DbgSymbol) + 7) &
                                                (size_t)~7) - CANARY_SIZE);

  sh_mem_offset = (size_t)(((char*)ptr - ctx->memory->memory) >> 3);
  sh_mem_count = (debuginfo->real_size) >> 3;

  for (; i < sh_mem_count; i++) {
    if (ctx->sh_memory[sh_mem_offset+i] != SH_MEM_VALID) {
      printf("CheckForUse: *** pointer to a canary/debug info/freed block ***\n"
             "(pointer: %p)\n"
             "=== aborted (core dumped) ===\n", ptr);
      abort();
    }
  }
}

size_t GetShMemOffset(void* ptr) {
  AllocatorCtx* ctx = AllocatorCtxManager(0, NULL);
  
  if (ctx == NULL) {
    printf("GetShMemOffset: *** not set context ***\n=== abort ===\n");
    abort();
  }
  
  CheckPtr(ptr);

  return (size_t)(((char*)ptr - ctx->memory->memory) >> 3);
}

size_t GetShMemCountBytes(size_t bytes) {
  return ((bytes + 7) & (size_t)~7) >> 3;
}

#endif /* DBG_ALLOC_SOURCE */

#ifdef DEFER_WRAP_MALLOC
# define malloc DbgAlloc
# define free   DbgFree
#endif

#undef DEFER_WRAP_MALLOC
