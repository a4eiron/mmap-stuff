#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char *argv[]) {

  long page_size = sysconf(_SC_PAGESIZE);
  printf("system page size: %ld\n", page_size);

  size_t request_size = page_size * 4;

  void *region = mmap(NULL, request_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (region == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  printf("got region at %p of size %zu\n", region, request_size);

  strcpy((char *)region, "hello from mmap'd memory");
  printf("contents read: %s\n", (char *)region);

  char *last_byte = (char *)region + request_size - 1;
  *last_byte = 'X';

  printf("last byte is set to: %c\n", *last_byte);

  if (munmap(region, request_size) == -1) {
    perror("munmap");
    return 1;
  }

  printf("munmaped\n");

  return 0;
}
