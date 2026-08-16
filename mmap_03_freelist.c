// freelist allocator with coalescing (boundary tag)
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

typedef struct BlockHeader {
  size_t size;
  int isFree;
  struct BlockHeader *next;
} BlockHeader;

#define HEADER_SIZE sizeof(BlockHeader)
#define FOOTER_SIZE sizeof(size_t)
#define MIN_BLOCK_SIZE 32

typedef struct {
  void *base;
  size_t capacity;
  BlockHeader *freelist;
} Allocator;

void print_free_list(Allocator *);
void print_alloc_size(void *);

static BlockHeader *find_first_fit(Allocator *allocator, size_t size,
                                   BlockHeader **prev_out) {
  BlockHeader *prev = NULL;
  BlockHeader *curr = allocator->freelist;

  while (curr != NULL) {
    if (curr->size >= size) {
      *prev_out = prev;
      return curr;
    }

    prev = curr;
    curr = curr->next;
  }

  return NULL;
}

static void write_footer(BlockHeader *block) {
  size_t *footer = (size_t *)((char *)block + HEADER_SIZE + block->size);
  *footer = block->size;
}

static BlockHeader *get_next_physical(Allocator *allocator,
                                      BlockHeader *header) {
  char *candidate = (char *)header + HEADER_SIZE + header->size + FOOTER_SIZE;
  char *arena_end = (char *)allocator->base + allocator->capacity;
  if (candidate >= arena_end)
    return NULL;
  return (BlockHeader *)candidate;
}

static BlockHeader *get_prev_physical(Allocator *allocator,
                                      BlockHeader *header) {

  // first block
  if ((char *)header == (char *)allocator->base)
    return NULL;

  size_t *prev_footer = (size_t *)((char *)header - FOOTER_SIZE);
  size_t prev_size = *prev_footer;
  return (BlockHeader *)((char *)header - FOOTER_SIZE - prev_size -
                         HEADER_SIZE);
}

static void freelist_remove(Allocator *allocator, BlockHeader *target) {
  BlockHeader *prev = NULL;
  BlockHeader *curr = allocator->freelist;
  while (curr != NULL) {
    if (curr == target) {

      if (prev == NULL)
        allocator->freelist = curr->next;
      else
        prev->next = curr->next;
      return;
    }
    prev = curr;
    curr = curr->next;
  }
}

static void freelist_push(Allocator *allocator, BlockHeader *block) {
  block->next = allocator->freelist;
  allocator->freelist = block;
}

int AllocatorInit(Allocator *allocator, size_t size) {
  void *region = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (region == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  allocator->base = region;
  allocator->capacity = size;

  BlockHeader *initial = (BlockHeader *)region;
  initial->size = size - HEADER_SIZE - FOOTER_SIZE;
  initial->isFree = 1;
  initial->next = NULL;
  write_footer(initial);
  allocator->freelist = initial;

  return 0;
}

void *Alloc(Allocator *allocator, size_t size) {
  if (size == 0)
    return NULL;

  BlockHeader *prev;
  BlockHeader *found = find_first_fit(allocator, size, &prev);
  if (found == NULL)
    return NULL;

  if (prev == NULL)
    allocator->freelist = found->next;
  else
    prev->next = found->next;

  size_t leftover = found->size - size;

  if (leftover >= HEADER_SIZE + FOOTER_SIZE + MIN_BLOCK_SIZE) {
    found->size = size;
    write_footer(found);

    BlockHeader *remainder =
        (BlockHeader *)((char *)found + HEADER_SIZE + FOOTER_SIZE + size);
    remainder->size = leftover - HEADER_SIZE - FOOTER_SIZE;
    remainder->isFree = 1;
    write_footer(remainder);
    freelist_push(allocator, remainder);
  }

  found->isFree = 0;
  found->next = NULL;

  return (void *)((char *)found + HEADER_SIZE);
}

void Free(Allocator *allocator, void *ptr) {
  if (ptr == NULL)
    return;

  BlockHeader *header = (BlockHeader *)((char *)ptr - HEADER_SIZE);
  if (header->isFree) {
    printf("double free chief: %p\n", ptr);
    return;
  }
  header->isFree = 1;
  write_footer(header);

  // forward
  BlockHeader *next = get_next_physical(allocator, header);
  if (next != NULL && next->isFree) {
    freelist_remove(allocator, next);
    header->size += HEADER_SIZE + FOOTER_SIZE + next->size;
    write_footer(header);
  }

  // backward
  BlockHeader *prev = get_prev_physical(allocator, header);
  if (prev != NULL && prev->isFree) {
    freelist_remove(allocator, prev);
    prev->size += HEADER_SIZE + FOOTER_SIZE + header->size;
    write_footer(prev);
    header = prev; // merged block is now the prev
  }

  freelist_push(allocator, header);
}

void Destroy(Allocator *allocator) {
  munmap(allocator->base, allocator->capacity);
  allocator->base = NULL;
  allocator->capacity = 0;
  allocator->freelist = NULL;
}

int main(int argc, char *argv[]) {

  Allocator a;
  if (AllocatorInit(&a, 4096) != 0)
    return 1;

  printf("arena: %zu bytes, header size: %zu, footer: %zu\n", a.capacity,
         HEADER_SIZE, FOOTER_SIZE);
  print_free_list(&a);

  ///
  char *p1 = Alloc(&a, 64);
  if (!p1) {
    fprintf(stderr, "p1 alloc failed\n");
    return 1;
  }
  strcpy(p1, "block one");
  printf("p1: %p, \"%s\"\n", (void *)p1, p1);

  ///
  char *p2 = Alloc(&a, 128);
  if (!p2) {
    fprintf(stderr, "p2 alloc failed\n");
    return 1;
  }
  strcpy(p2, "block two");
  printf("p2: %p, \"%s\"\n", (void *)p2, p2);

  ///
  char *p3 = Alloc(&a, 32);
  if (!p3) {
    fprintf(stderr, "p3 alloc failed\n");
    return 1;
  }
  strcpy(p3, "block three");
  printf("p3: %p, \"%s\"\n", (void *)p3, p3);

  print_free_list(&a);

  printf("freeing p1, p2, p3 --\n");
  Free(&a, p1);
  print_free_list(&a);
  Free(&a, p2);
  print_free_list(&a);
  Free(&a, p3);
  print_free_list(&a);

  Destroy(&a);
  return 0;
}

void print_free_list(Allocator *allocator) {
  printf("Free list: ");
  BlockHeader *curr = allocator->freelist;
  if (curr == NULL)
    printf("empty\n");
  while (curr != NULL) {
    printf("[size: %zu @ +%ld] -> ", curr->size,
           (long)((char *)curr - (char *)allocator->base));
    curr = curr->next;
  }
  printf("\n");
}

void print_alloc_size(void *ptr) {
  BlockHeader *header = (BlockHeader *)((char *)ptr - HEADER_SIZE);
  printf("usable size: %zu\n", header->size);
}
