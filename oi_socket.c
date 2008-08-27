#include "oi.h"
#include <ev.h>

static void 
close_socket(oi_socket *socket)
{
#ifdef HAVE_GNUTLS
  if(socket->server->secure)
    ev_io_stop(socket->server->loop, &socket->handshake_watcher);
#endif
  ev_io_stop(socket->server->loop, &socket->read_watcher);
  ev_io_stop(socket->server->loop, &socket->write_watcher);
  ev_timer_stop(socket->server->loop, &socket->timeout_watcher);

  if(0 > close(socket->fd))
    error("problem closing socket fd");

  socket->open = FALSE;

  if(socket->on_close)
    socket->on_close(socket);
  /* No access to the socket past this point! 
   * The user is allowed to free in the callback
   */
}

#ifdef HAVE_GNUTLS
static void 
on_handshake(struct ev_loop *loop, ev_io *watcher, int revents)
{
  oi_socket *socket = watcher->data;

  printf("on_handshake\n");

  assert(ev_is_active(&socket->timeout_watcher));
  assert(!ev_is_active(&socket->read_watcher));
  assert(!ev_is_active(&socket->write_watcher));

  if(EV_ERROR & revents) {
    error("on_handshake() got error event, closing socket.n");
    goto error;
  }

  int r = gnutls_handshake(socket->session);
  if(r < 0) {
    if(gnutls_error_is_fatal(r)) goto error;
    if(r == GNUTLS_E_INTERRUPTED || r == GNUTLS_E_AGAIN)
      ev_io_set( watcher
               , socket->fd
               , EV_ERROR | (GNUTLS_NEED_WRITE ? EV_WRITE : EV_READ)
               );
    return;
  }

  oi_socket_reset_timeout(socket);
  ev_io_stop(loop, watcher);

  ev_io_start(loop, &socket->read_watcher);
  if(CONNECTION_HAS_SOMETHING_TO_WRITE)
    ev_io_start(loop, &socket->write_watcher);

  return;
error:
  close_socket(socket);
}
#endif /* HAVE_GNUTLS */


/* Internal callback 
 * called by socket->timeout_watcher
 */
static void 
on_timeout(struct ev_loop *loop, ev_timer *watcher, int revents)
{
  oi_socket *socket = watcher->data;

  assert(watcher == &socket->timeout_watcher);

  //printf("on_timeout\n");

  if(socket->on_timeout) {
    socket->on_timeout(socket);
    oi_socket_reset_timeout(socket);
  }

  oi_socket_close(socket);
}

/* Internal callback 
 * called by socket->read_watcher
 */
static void 
on_readable(struct ev_loop *loop, ev_io *watcher, int revents)
{
  oi_socket *socket = watcher->data;
  char recv_buffer[TCP_MAXWIN];
  size_t recv_buffer_size = TCP_MAXWIN;
  ssize_t recved;

  printf("on_readable\n");

  assert(ev_is_active(&socket->timeout_watcher)); // TODO -- why is this broken?
  assert(watcher == &socket->read_watcher);

#ifdef HAVE_GNUTLS
  assert(!ev_is_active(&socket->handshake_watcher));

  if(socket->secure) {
    recved = gnutls_record_recv( socket->session
                               , recv_buffer
                               , recv_buffer_size
                               );
    if(recved <= 0) {
      if(gnutls_error_is_fatal(recved)) goto error;
      if( (recved == GNUTLS_E_INTERRUPTED || recved == GNUTLS_E_AGAIN)
       && GNUTLS_NEED_WRITE
        ) ev_io_start(loop, &socket->write_watcher);
      return; 
    } 
  } else {
#endif /* HAVE_GNUTLS */

    recved = recv(socket->fd, recv_buffer, recv_buffer_size, 0);
    if(recved < 0) goto error;
    if(recved == 0) return;

#ifdef HAVE_GNUTLS
  }
#endif 

  oi_socket_reset_timeout(socket);

  if(socket->on_read) {
    socket->on_read(socket, recv_buffer, recved);
  }

  return;
error:
  oi_socket_close(socket);
}

/* Internal callback 
 * called by socket->write_watcher
 */
static void 
on_writable(struct ev_loop *loop, ev_io *watcher, int revents)
{
  oi_socket *socket = watcher->data;
  ssize_t sent;
  
  printf("on_writable\n");

  assert(CONNECTION_HAS_SOMETHING_TO_WRITE);
  assert(socket->written <= socket->to_write_len);
  // TODO -- why is this broken?
  //assert(ev_is_active(&socket->timeout_watcher));
  assert(watcher == &socket->write_watcher);

#ifdef HAVE_GNUTLS
  assert(!ev_is_active(&socket->handshake_watcher));

  if(socket->secure) {
    sent = gnutls_record_send( socket->session
                             , socket->write_buffer->base + socket->write_buffer->written
                             , socket->write_buffer->len - socket->write_buffer->written
                             ); 
    if(sent <= 0) {
      if(gnutls_error_is_fatal(sent)) goto error;
      if( (sent == GNUTLS_E_INTERRUPTED || sent == GNUTLS_E_AGAIN)
       && GNUTLS_NEED_READ
        ) ev_io_stop(loop, watcher);
      return; 
    }
  } else {
#endif /* HAVE_GNUTLS */

    /* TODO use writev() here */
    sent = nosigpipe_push( (void*)socket->fd
                         , socket->write_buffer->base + socket->write_buffer->written
                         , socket->write_buffer->len - socket->write_buffer->written
                         );
    if(sent < 0) goto error;
    if(sent == 0) return;

#ifdef HAVE_GNUTLS
  }
#endif /* HAVE_GNUTLS */

  oi_socket_reset_timeout(socket);

  socket->write_buffer->written += sent;
  socket->written += sent;

  if(socket->write_buffer->written == socket->write_buffer->len) {
    if(socket->write_buffer->release)
      socket->write_buffer->release(socket->write_buffer);
    socket->write_buffer = socket->write_buffer->next;
    if(socket->write_buffer == NULL) {
      ev_io_stop(loop, watcher);
    }
  }
  return;
error:
  error("close socket on write.");
  oi_socket_schedule_close(socket);
}

#ifdef HAVE_GNUTLS
static void 
on_goodbye_tls(struct ev_loop *loop, ev_io *watcher, int revents)
{
  oi_socket *socket = watcher->data;
  assert(watcher == &socket->goodbye_tls_watcher);

  if(EV_ERROR & revents) {
    error("on_goodbye() got error event, closing socket.");
    goto die;
  }

  int r = gnutls_bye(socket->session, GNUTLS_SHUT_RDWR);
  if(r < 0) {
    if(gnutls_error_is_fatal(r)) goto die;
    if(r == GNUTLS_E_INTERRUPTED || r == GNUTLS_E_AGAIN)
      ev_io_set( watcher
               , socket->fd
               , EV_ERROR | (GNUTLS_NEED_WRITE ? EV_WRITE : EV_READ)
               );
    return;
  }

die:
  ev_io_stop(loop, watcher);
  if(socket->session) 
    gnutls_deinit(socket->session);
  close_socket(socket);
}
#endif /* HAVE_GNUTLS*/

static void 
on_goodbye(struct ev_loop *loop, ev_timer *watcher, int revents)
{
  oi_socket *socket = watcher->data;
  assert(watcher == &socket->goodbye_watcher);
  close_socket(socket);
}

/**
 * If using SSL do consider setting
 *   gnutls_db_set_retrieve_function (socket->session, _);
 *   gnutls_db_set_remove_function (socket->session, _);
 *   gnutls_db_set_store_function (socket->session, _);
 *   gnutls_db_set_ptr (socket->session, _);
 * To provide a better means of storing SSL session caches. libebb provides
 * only a simple default implementation. 
 */
void 
oi_socket_init(oi_socket *socket, float timeout)
{
  socket->fd = -1;
  socket->server = NULL;
  socket->ip = NULL;
  socket->open = FALSE;

  ev_init (&socket->write_watcher, on_writable);
  socket->write_watcher.data = socket;
  socket->write_buffer = NULL;

  ev_init(&socket->read_watcher, on_readable);
  socket->read_watcher.data = socket;

  ev_init(&socket->error_watcher, on_error);
  socket->error_watcher.data = socket;

#ifdef HAVE_GNUTLS
  socket->handshake_watcher.data = socket;
  ev_init(&socket->handshake_watcher, on_handshake);

  ev_init(&socket->goodbye_tls_watcher, on_goodbye_tls);
  socket->goodbye_tls_watcher.data = socket;

  socket->session = NULL;
#endif /* HAVE_GNUTLS */

  ev_timer_init(&socket->goodbye_watcher, on_goodbye, 0., 0.);
  socket->goodbye_watcher.data = socket;  

  ev_timer_init(&socket->timeout_watcher, on_timeout, timeout, 0.);
  socket->timeout_watcher.data = socket;  

  socket->on_connected = NULL;
  socket->on_read = NULL;
  socket->on_drain = NULL;
  socket->on_error = NULL;
  socket->on_timeout = NULL;
  socket->data = NULL;
}

void 
oi_socket_close (oi_socket *socket)
{
#ifdef HAVE_GNUTLS
  if(socket->server->secure) {
    ev_io_set(&socket->goodbye_tls_watcher, socket->fd, EV_ERROR | EV_READ | EV_WRITE);
    ev_io_start(socket->server->loop, &socket->goodbye_tls_watcher);
    return;
  }
#endif
  ev_timer_start(socket->server->loop, &socket->goodbye_watcher);
}

/* 
 * Resets the timeout to stay alive for another socket->timeout seconds
 */
void 
oi_socket_reset_timeout(oi_socket *socket)
{
  ev_timer_again(socket->server->loop, &socket->timeout_watcher);
}

/**
 * Writes a string to the socket. This is actually sets a watcher
 * which may take multiple iterations to write the entire string.
 *
 * This can only be called once at a time. If you call it again
 * while the socket is writing another buffer the oi_socket_write
 * will return FALSE and ignore the request.
 */
int 
oi_socket_write (oi_socket *socket, const char *buf, size_t len, oi_after_write_cb cb)
{
  if(ev_is_active(&socket->write_watcher))
    return FALSE;
  assert(!CONNECTION_HAS_SOMETHING_TO_WRITE);
  socket->to_write = buf;
  socket->to_write_len = len;
  socket->written = 0;
  socket->after_write_cb = cb;
  ev_io_start(socket->server->loop, &socket->write_watcher);
  return TRUE;
}
