#include "rdma-common.h"
#include "message.h"
#include "data-sender.h"
 
#define MAX_PAGE_NUM 64
#define SHOW_INTERVAL 3
#define SCAN_INTERVAL 1

int PAGE_NUM; 
int pagesize;

const int TIMEOUT_IN_MS = 500; /* ms */
    
long uffd;          /* userfaultfd file descriptor */
static char* mapped_mem;

int on_addr_resolved(struct rdma_cm_id *id);
int on_connection(struct rdma_cm_id *id);
// static int on_disconnect(struct rdma_cm_id *id);
int on_event(struct rdma_cm_event *event);
int on_route_resolved(struct rdma_cm_id *id);
void * show_buffer_client();
void * check_and_rdmasend();
void usage(const char *argv0);

int written_flag = 0;
int written_page_id[MAX_PAGE_NUM];

u_int64_t get_page_index(__u64 addr);
void set_page_written(uint64_t page_index);

void * fault_handler_thread(void *arg);
void handler_setup();

void * client_map();

void start_send(char * arg_addr) {
  mapped_mem = arg_addr;
  pthread_t test_thr;
  pthread_create(&test_thr, NULL, client_map, NULL);
}

void * client_map() {

  struct addrinfo *addr;
  struct rdma_cm_event *event = NULL;
  struct rdma_cm_id *conn= NULL;
  struct rdma_event_channel *ec = NULL;

  set_client(1);

  PAGE_NUM = 1;
  if(PAGE_NUM > MAX_PAGE_NUM) 
    die("TOO MUCH PAGE!!!");

  // ------------------ SETUP PAGE FAULT HANDLER ------------------
  pagesize = sysconf(_SC_PAGE_SIZE);
  printf("page size is:%d\n", pagesize);
  handler_setup();

  // ------------------ BUILD RDMA CONNECTION ------------------
  TEST_NZ(getaddrinfo("192.168.112.129", DEFAULT_PORT, NULL, &addr));

  TEST_Z(ec = rdma_create_event_channel());
  TEST_NZ(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
  TEST_NZ(rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS));

  freeaddrinfo(addr);

  int r = 0;

  while (rdma_get_cm_event(ec, &event) == 0) {
    struct rdma_cm_event event_copy;

    memcpy(&event_copy, event, sizeof(*event));
    rdma_ack_cm_event(event);

    if (on_event(&event_copy))
      break;
    if(r++ == 2) break;
  }

  printf("on cennection.\n");
  sleep(3);

  // ------------------ CREATE MONITOR THREAD AND SCAN THREAD ------------------
  pthread_t memory_monitor;
  TEST_NZ(pthread_create(&memory_monitor, NULL, show_buffer_client, NULL));

  pthread_t scanner;
  TEST_NZ(pthread_create(&scanner, NULL, check_and_rdmasend, NULL));

  // ------------------ CHANGE MEMORY ------------------
  
  while (1)
  {
    
  };

  rdma_destroy_event_channel(ec);

  return 0;
}

int on_addr_resolved(struct rdma_cm_id *id)
{
  printf("address resolved.\n");
  build_connection(id);
  TEST_NZ(rdma_resolve_route(id, TIMEOUT_IN_MS));

  return 0;
}

int on_connection(struct rdma_cm_id *id)
{
  on_connect(id->context);
  send_mr(id->context);

  return 0;
}

// int on_disconnect(struct rdma_cm_id *id)
// {
//   printf("disconnected.\n");

//   destroy_connection(id->context);
//   return 1; /* exit event loop */
// }

int on_event(struct rdma_cm_event *event)
{
  int r = 0;

  if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED)
    r = on_addr_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED)
    r = on_route_resolved(event->id);
  else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    r = on_connection(event->id);
  // else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
  //   r = on_disconnect(event->id);
  else {
    fprintf(stderr, "on_event: %d\n", event->event);
    die("on_event: unknown event.");
  }

  return r;
}

int on_route_resolved(struct rdma_cm_id *id)
{
  struct rdma_conn_param cm_params;

  printf("route resolved.\n");
  build_params(&cm_params);
  TEST_NZ(rdma_connect(id, &cm_params));

  return 0;
}

void usage(const char *argv0)
{
  fprintf(stderr, "usage: %s <mode> <server-address><page-num>\n  mode = \"read\", \"write\"\n", argv0);
  exit(1);
}


void * show_buffer_client() {
  while(1) {
    sleep(SHOW_INTERVAL);
    for (int i = 0; i < PAGE_NUM; i++) {
      char * base = mapped_mem+i*pagesize;
      for(int j = 0;j < 64; j+=4) {
        printf("%d\n", *(base+j));
      }
      // printf("PAGE %d:%s\n", i, mapped_mem+i*pagesize);
    }
    puts("");
  }
  return NULL;
}

void * check_and_rdmasend() {
  char* send_word = malloc(pagesize);
  while (1)
  {
    sleep(SCAN_INTERVAL);
    
    if (written_flag == 0) continue;
    
    for (int i = 0; i < PAGE_NUM; i++) {
      if (written_page_id[i] == 0) continue;

      struct uffdio_writeprotect uffdio_wp;
      uffdio_wp.range.start = (unsigned long) mapped_mem;
      uffdio_wp.range.len = pagesize;
      uffdio_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
      if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_wp) == -1)
          die("ioctl-UFFDIO_WRITEPROTECT");

      // TEST_NZ(mprotect(mapped_mem+pagesize*i, pagesize, PROT_READ));
      memset(send_word, 0, pagesize);
      written_page_id[i] = 0;
      
      memcpy(send_word, mapped_mem+i*pagesize, pagesize);
      for (size_t j = 0; j < pagesize; j++)
      {
        printf("%c", send_word[j]);
      }
      puts("");
      rdma_write(send_word, i);
    }
  }
  free(send_word);
  return NULL;
}

u_int64_t get_page_index(__u64 addr) {
  u_int64_t lack_addr = (u_int64_t)addr - (u_int64_t)mapped_mem;
  return lack_addr / pagesize;
}

void set_page_written(uint64_t page_index) {
  written_flag = 1;
  written_page_id[page_index] = 1;
  // printf("set written_flag to 1\nset page %ld to 1\n", page_index);
}

void * fault_handler_thread(void *arg)
{
  static struct uffd_msg msg;   /* Data read from userfaultfd */
  static int fault_cnt = 0;     /* Number of faults so far handled */
  long uffd;                    /* userfaultfd file descriptor */
  static char *page = NULL;
  struct uffdio_copy uffdio_copy;
  struct uffdio_writeprotect uffdio_register_wp;
  ssize_t nread;

  uffd = (long) arg;

  /* Create a page that will be copied into the faulting region. */

  if (page == NULL) {
    page = mmap(NULL, pagesize, PROT_READ | PROT_WRITE,
              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED)
      die("mmap");
  }

  /* Loop, handling incoming events on the userfaultfd
    file descriptor. */

  for (;;) {

    /* See what poll() tells us about the userfaultfd. */

    struct pollfd pollfd;
    int nready;
    pollfd.fd = uffd;
    pollfd.events = POLLIN;
    nready = poll(&pollfd, 1, -1);
    if (nready == -1)
      die("poll");

    printf("\nfault_handler_thread():\n");
    printf("    poll() returns: nready = %d; "
          "POLLIN = %d; POLLERR = %d\n", nready,
          (pollfd.revents & POLLIN) != 0,
          (pollfd.revents & POLLERR) != 0);

    /* Read an event from the userfaultfd. */

    nread = read(uffd, &msg, sizeof(msg));
    if (nread == 0) {
      printf("EOF on userfaultfd!\n");
      exit(EXIT_FAILURE);
    }

    if (nread == -1)
      die("read");

    /* We expect only one kind of event; verify that assumption. */

    if (msg.event != UFFD_EVENT_PAGEFAULT) {
      fprintf(stderr, "Unexpected event on userfaultfd\n");
      exit(EXIT_FAILURE);
    }

    /* Display info about the page-fault event. */

    printf("    UFFD_EVENT_PAGEFAULT event: ");
    printf("flags = %lld; ", msg.arg.pagefault.flags);
    printf("address = 0x%llx\n", msg.arg.pagefault.address);
    if(msg.arg.pagefault.flags == 0) {
      /* Copy the page pointed to by 'page' into the faulting
      region. Vary the contents that are copied in, so that it
      is more obvious that each fault is handled separately. */

      memset(page, 0, pagesize);
      fault_cnt++;

      uffdio_copy.src = (unsigned long) page;

      /* We need to handle page faults in units of pages(!).
      So, round faulting address down to page boundary. */

      uffdio_copy.dst = (unsigned long) msg.arg.pagefault.address &
                                      ~(pagesize - 1);
      uffdio_copy.len = pagesize;
      uffdio_copy.mode = 0;
      uffdio_copy.copy = 0;
      if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
        die("ioctl-UFFDIO_COPY");

      printf("        (uffdio_copy.copy returned %lld)\n",
              uffdio_copy.copy);
    } else {
      unsigned long target = (unsigned long) msg.arg.pagefault.address &
                                      ~(pagesize - 1);
      uffdio_register_wp.range.start = (unsigned long) target;
      uffdio_register_wp.range.len = 4096;
      uffdio_register_wp.mode = 0;
      if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_register_wp) == -1)
        die("ioctl-UFFDIO_WRITEPROTECT");
      u_int64_t page_index = get_page_index(msg.arg.pagefault.address);
      set_page_written(page_index);
    }
  }
} 

void handler_setup() {
  uint64_t len;       /* Length of region handled by userfaultfd */
  pthread_t thr;      /* ID of thread that handles page faults */
  struct uffdio_api uffdio_api;
  struct uffdio_register uffdio_register;

  struct uffdio_writeprotect uffdio_register_wp;

  int s;

  len = PAGE_NUM * pagesize;

  /* Create and enable userfaultfd object. */
  puts("before creating uffd");

  uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
  if (uffd == -1)
      die("userfaultfd");

  uffdio_api.api = UFFD_API;
  uffdio_api.features = 0;
  if (ioctl(uffd, UFFDIO_API, &uffdio_api) == -1)
      die("ioctl-UFFDIO_API");

  /* Create a private anonymous mapping. The memory will be
    demand-zero paged--that is, not yet allocated. When we
    actually touch the memory, it will be allocated via
    the userfaultfd. */

  // mapped_mem = mmap(NULL, len, PROT_READ | PROT_WRITE,
  //             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mapped_mem == MAP_FAILED)
      die("mmap");


  // printf("Address returned by mmap() = %p\n", mapped_mem);

  /* Register the memory range of the mapping we just created for
    handling by the userfaultfd object. In mode, we request to track
    missing pages (i.e., pages that have not yet been faulted in). */
  // register, so that the page fault handler will be effective in this area

  printf("len is:%ld\n", len);
  uffdio_register.range.start = (unsigned long) mapped_mem;
  uffdio_register.range.len = len;
  uffdio_register.mode = UFFDIO_REGISTER_MODE_WP | UFFDIO_REGISTER_MODE_MISSING;
  if (ioctl(uffd, UFFDIO_REGISTER, &uffdio_register) == -1)
      printf("%s\n", strerror(errno));
      // die("ioctl-UFFDIO_REGISTER");

  printf("len is:%ld\n", len);
  uffdio_register_wp.range.start = (unsigned long) mapped_mem;
  uffdio_register_wp.range.len = len;
  uffdio_register_wp.mode = UFFDIO_WRITEPROTECT_MODE_WP;
  if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_register_wp) == -1)
      die("ioctl-UFFDIO_WRITEPROTECT");

  /* Create a thread that will process the userfaultfd events. */

  s = pthread_create(&thr, NULL, fault_handler_thread, (void *) uffd);
  if (s != 0) {
      errno = s;
      die("pthread_create");
  }

  /* Main thread now touches memory in the mapping, touching
    locations 1024 bytes apart. This will trigger userfaultfd
    events for all pages in the region. */

  int l;
  l = 0xf;    /* Ensure that faulting address is not on a page
                boundary, in order to test that we correctly
                handle that case in fault_handling_thread(). */
  while (l < len) {
      char c = mapped_mem[l];
      printf("Read address %p in main(): ", mapped_mem + l);
      printf("%c\n", c);
      l += pagesize;
      usleep(100000);         /* Slow things down a little */
  }

  if (ioctl(uffd, UFFDIO_WRITEPROTECT, &uffdio_register_wp) == -1)
      die("ioctl-UFFDIO_WRITEPROTECT");
}