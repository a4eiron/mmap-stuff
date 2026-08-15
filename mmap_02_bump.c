// bump allocator
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

typedef struct {
  void *base;
  size_t offset;
  size_t capacity;
} Arena;

int ArenaInit(Arena *arena, size_t size) {
  void *region = mmap(NULL, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (region == MAP_FAILED) {
    perror("mmap");
    return -1;
  }

  arena->base = region;
  arena->offset = 0;
  arena->capacity = size;
  return 0;
}

void *ArenaAllocate(Arena *arena, size_t size, size_t alignment) {
  uintptr_t current = (uintptr_t)(arena->base + arena->offset);
  uintptr_t aligned = (current + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
  size_t padding = aligned - current;

  if (arena->offset + padding + size > arena->capacity) {
    return NULL;
  }

  arena->offset += padding + size;
  return (void *)aligned;
}

void ArenaDestroy(Arena *arena) {
  munmap(arena->base, arena->capacity);
  arena->base = NULL;
  arena->capacity = 0;
  arena->offset = 0;
}

int main(int argc, char *argv[]) {
  Arena arena;
  if (ArenaInit(&arena, 4096 * 4) != 0) {
    return 1;
  }

  printf("Arena ready: %zu bytes at %p\n", arena.capacity, arena.base);

  ///
  char *s1 = ArenaAllocate(&arena, 13, 1);
  strcpy(s1, "hello, bump!");
  printf("s1: %s at offset %zu\n", s1, (size_t)((void *)s1 - arena.base));

  ///
  double *d1 = ArenaAllocate(&arena, sizeof(double), sizeof(double));
  *d1 = 3.14159;
  printf("d1: %f at offset %zu\n", *d1, (size_t)((void *)d1 - arena.base));

  ///
  int *nums = ArenaAllocate(&arena, sizeof(int) * 10, sizeof(int));
  for (int i = 0; i < 10; i++) {
    nums[i] = i + 1;
  }

  for (int i = 0; i < 10; i++) {
    printf("%d ", nums[i]);
  }
  printf("\n");

  printf("nums[9]: %d at offset %zu\n", nums[9],
         (size_t)((void *)nums - arena.base));
  printf("total used: %zu / %zu\n", arena.offset, arena.capacity);

  ///
  size_t huge = arena.capacity;
  void *over = ArenaAllocate(&arena, huge, 1);
  printf("over: %p\n", over);

  ArenaDestroy(&arena);
  return 0;
}
