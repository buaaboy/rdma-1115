#include <stdio.h>
#include "data-sender.h"

int main() {
  char * test = malloc(1);
  printf("base of heap: %lx\n", test);
  int * a = memalign(4096, 4096);
  // int a[4];
  a[0]=1;
  a[1]=1;
  a[2]=1;
  a[3]=1;
  printf("addr of a:0x%lx\n", (unsigned long)a);
  printf("addr of a[0]:0x%lx\n", (unsigned long)&a[0]);
  printf("addr of a[1]:0x%lx\n", (unsigned long)&a[1]);
  // char * mapped_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
  //             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  char * mapped_mem = (char*)a;

  
  start_send(mapped_mem);
  while(1) {
    sleep(1);
    a[0]=1;
    a[1]=2;
    a[2]=3;
    a[3]=4;
  }
}