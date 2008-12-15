#include <ev.h>
#include <pthread.h>
#include "ngx_queue.h"

#ifndef oi_async_h
#define oi_async_h

typedef struct oi_async oi_async;
typedef struct oi_task  oi_task;

struct oi_async {
  /* private */
  ev_async watcher;  
  struct ev_loop *loop;

  pthread_mutex_t lock; /* for finished_tasks */
  ngx_queue_t finished_tasks;
  ngx_queue_t new_tasks;

  /* public */
  void *data;
}; 

struct oi_task {
  /* private */
  oi_async *async;
  ngx_queue_t queue;
  int type;
  union {

    struct {
      const char *pathname;
      int flags;
      mode_t mode;
      void (*cb) (oi_task *, int result);
      int result;
    } open;

    struct {
      int fd;
      void *buf;
      size_t count;
      void (*cb) (oi_task *, ssize_t result);
      ssize_t result;
    } read;

    struct {
      int fd;
      const void *buf;
      size_t count;
      void (*cb) (oi_task *, ssize_t result);
      ssize_t result;
    } write;

    struct {
      int fd;
      void (*cb) (oi_task *, int result);
      int result;
    } close;
    
  } params;

  /* read-only */
  volatile unsigned active:1;
  int errorno;

  /* public */
  void *data;
}; 

void oi_async_init    (oi_async *);
void oi_async_destroy (oi_async *);
void oi_async_attach  (struct ev_loop *loop, oi_async *);
void oi_async_detach  (oi_async *);
void oi_async_submit  (oi_async *, oi_task *);

/* To submit a task for async processing
 * (0) allocate memory for your task
 * (1) initialize the task with one of the functions below
 * (2) set task->done() callback and optionally the task->data pointer
 * (3) oi_async_submit() the task 
 */
void oi_task_open   (oi_task *, const char *pathname, int flags, mode_t mode);
void oi_task_read   (oi_task *, int fd, void *buf, size_t count);
void oi_task_write  (oi_task *, int fd, const void *buf, size_t count);
void oi_task_close  (oi_task *, int fd);

enum { OI_TASK_OPEN
     , OI_TASK_READ
     , OI_TASK_WRITE
     , OI_TASK_CLOSE
     };

#define oi_task_init_common(task) do {\
  (task)->active = 0;\
  (task)->async = NULL;\
} while(0)

#define oi_task_init_open(task, _cb, _pathname, _flags, _mode) do { \
  oi_task_init_common(task); \
  (task)->type = OI_TASK_OPEN; \
  (task)->params.open.cb = _cb; \
  (task)->params.open.pathname = _pathname; \
  (task)->params.open.flags = _flags; \
  (task)->params.open.mode = _mode; \
} while(0)

#define oi_task_init_read(task, _cb, _fd, _buf, _count) do { \
  oi_task_init_common(task); \
  (task)->type = OI_TASK_READ; \
  (task)->params.read.cb = _cb; \
  (task)->params.read.fd = _fd; \
  (task)->params.read.buf = _buf; \
  (task)->params.read.count = _count; \
} while(0)

#define oi_task_init_write(task, _cb, _fd, _buf, _count) do { \
  oi_task_init_common(task); \
  (task)->type = OI_TASK_WRITE; \
  (task)->params.read.cb = _cb; \
  (task)->params.read.fd = _fd; \
  (task)->params.read.buf = _buf; \
  (task)->params.read.count = _count; \
} while(0)

#define oi_task_init_close(task, _cb, _fd) do { \
  oi_task_init_common(task); \
  (task)->type = OI_TASK_CLOSE; \
  (task)->params.close.cb = _cb; \
  (task)->params.close.fd = _fd; \
} while(0)

#endif /* oi_async_h */