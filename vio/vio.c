/* Copyright (c) 2000, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/*
  Note that we can't have assertion on file descriptors;  The reason for
  this is that during mysql shutdown, another thread can close a file
  we are working on.  In this case we should just return read errors from
  the file descriptior.
*/

#include "vio_priv.h"

#ifdef _WIN32

/**
  Stub io_wait method that defaults to indicate that
  requested I/O event is ready.

  Used for named pipe and shared memory VIO types.

  @param vio      Unused.
  @param event    Unused.
  @param timeout  Unused.

  @retval 1       The requested I/O event has occurred.
*/

static int no_io_wait(Vio *vio __attribute__((unused)),
                      enum enum_vio_io_event event __attribute__((unused)),
                      int timeout __attribute__((unused)))
{
  return 1;
}

#endif

static my_bool has_no_data(Vio *vio __attribute__((unused)))
{
  return FALSE;
}

/*
 * Helper to fill most of the Vio* with defaults.
 */

static void vio_init(Vio *vio, enum enum_vio_type type,
                     my_socket sd, uint flags)
{
  DBUG_ENTER("vio_init");
  DBUG_PRINT("enter", ("type: %d  sd: %d  flags: %d", type, sd, flags));

#ifndef HAVE_VIO_READ_BUFF
  flags&= ~VIO_BUFFERED_READ;
#endif
  memset(vio, 0, sizeof(*vio));
  vio->type= type;
  vio->mysql_socket= MYSQL_INVALID_SOCKET;
  mysql_socket_setfd(&vio->mysql_socket, sd);
  vio->localhost= flags & VIO_LOCALHOST;
  vio->read_timeout = vio->write_timeout = timeout_infinite();
  if ((flags & VIO_BUFFERED_READ) &&
      !(vio->read_buffer= (char*)my_malloc(VIO_READ_BUFFER_SIZE, MYF(MY_WME))))
    flags&= ~VIO_BUFFERED_READ;
#ifdef _WIN32
  if (type == VIO_TYPE_NAMEDPIPE)
  {
    vio->viodelete	=vio_delete;
    vio->vioerrno	=vio_errno;
    vio->read           =vio_read_pipe;
    vio->write          =vio_write_pipe;
    vio->fastsend	=vio_fastsend;
    vio->viokeepalive	=vio_keepalive;
    vio->should_retry	=vio_should_retry;
    vio->was_timeout    =vio_was_timeout;
    vio->vioshutdown	=vio_shutdown_pipe;
    vio->peer_addr	=vio_peer_addr;
    vio->io_wait        =no_io_wait;
    vio->is_connected   =vio_is_connected_pipe;
    vio->has_data       =has_no_data;
    vio->is_blocking    =vio_is_blocking;
    vio->set_blocking   =vio_set_blocking;
    DBUG_VOID_RETURN;
  }
#endif
#ifdef HAVE_SMEM
  if (type == VIO_TYPE_SHARED_MEMORY)
  {
    vio->viodelete	=vio_delete_shared_memory;
    vio->vioerrno	=vio_errno;
    vio->read           =vio_read_shared_memory;
    vio->write          =vio_write_shared_memory;
    vio->fastsend	=vio_fastsend;
    vio->viokeepalive	=vio_keepalive;
    vio->should_retry	=vio_should_retry;
    vio->was_timeout    =vio_was_timeout;
    vio->vioshutdown	=vio_shutdown_shared_memory;
    vio->peer_addr	=vio_peer_addr;
    vio->io_wait        =no_io_wait;
    vio->is_connected   =vio_is_connected_shared_memory;
    vio->has_data       =has_no_data;
    vio->is_blocking    =vio_is_blocking;
    vio->set_blocking   =vio_set_blocking;
    DBUG_VOID_RETURN;
  }
#endif
#ifdef HAVE_OPENSSL
  if (type == VIO_TYPE_SSL)
  {
    vio->viodelete	=vio_ssl_delete;
    vio->vioerrno	=vio_errno;
    vio->read		=vio_ssl_read;
    vio->write		=vio_ssl_write;
    vio->fastsend	=vio_fastsend;
    vio->viokeepalive	=vio_keepalive;
    vio->should_retry	=vio_should_retry;
    vio->was_timeout    =vio_was_timeout;
    vio->vioshutdown	=vio_ssl_shutdown;
    vio->peer_addr	=vio_peer_addr;
    vio->io_wait        =vio_io_wait;
    vio->is_connected   =vio_is_connected;
    vio->has_data       =vio_ssl_has_data;
    vio->timeout        =vio_socket_timeout;
    vio->is_blocking    =vio_is_blocking;
    vio->set_blocking   =vio_ssl_set_blocking;
    DBUG_VOID_RETURN;
  }
#endif /* HAVE_OPENSSL */
  vio->viodelete        =vio_delete;
  vio->vioerrno         =vio_errno;
  vio->read=            (flags & VIO_BUFFERED_READ) ? vio_read_buff : vio_read;
  vio->write            =vio_write;
  vio->fastsend         =vio_fastsend;
  vio->viokeepalive     =vio_keepalive;
  vio->should_retry     =vio_should_retry;
  vio->was_timeout      =vio_was_timeout;
  vio->vioshutdown      =vio_shutdown;
  vio->peer_addr        =vio_peer_addr;
  vio->io_wait          =vio_io_wait;
  vio->is_connected     =vio_is_connected;
  vio->timeout          =vio_socket_timeout;
  vio->has_data=        (flags & VIO_BUFFERED_READ) ?
                            vio_buff_has_data : has_no_data;
  vio->is_blocking      =vio_is_blocking;
  vio->set_blocking     =vio_set_blocking;

  vio->is_blocking_flag = TRUE;
  DBUG_VOID_RETURN;
}


/**
  Reinitialize an existing Vio object.

  @remark Used to rebind an initialized socket-based Vio object
          to another socket-based transport type. For example,
          rebind a TCP/IP transport to SSL.

  @remark If new socket handle passed to vio_reset() is not equal
          to the socket handle stored in Vio then socket handle will
          be closed before storing new value. If handles are equal
          then old socket is not closed. This is important for
          vio_reset() usage in ssl_do().

  @remark If any error occurs then Vio members won't be altered thus
          preserving socket handle stored in Vio and not taking
          ownership over socket handle passed as parameter.

  @param vio    A VIO object.
  @param type   A socket-based transport type.
  @param sd     The socket.
  @param ssl    An optional SSL structure.
  @param flags  Flags passed to vio_init.

  @return Return value is zero on success.
*/

my_bool vio_reset(Vio* vio, enum enum_vio_type type,
                  my_socket sd, void *ssl __attribute__((unused)), uint flags)
{
  int ret= FALSE;
  Vio new_vio;
  DBUG_ENTER("vio_reset");

  /* The only supported rebind is from a socket-based transport type. */
  DBUG_ASSERT(vio->type == VIO_TYPE_TCPIP || vio->type == VIO_TYPE_SOCKET);

  vio_init(&new_vio, type, sd, flags);

  /* Preserve perfschema info for this connection */
  new_vio.mysql_socket.m_psi= vio->mysql_socket.m_psi;

#ifdef HAVE_OPENSSL
  new_vio.ssl_arg= ssl;
#endif

  /*
    Propagate the timeout values. Necessary to also propagate
    the underlying proprieties associated with the timeout,
    such as the socket blocking mode.
  */
  if (timeout_is_nonzero(vio->read_timeout))
    ret|= vio_timeout(&new_vio, 0, vio->read_timeout);

  if (timeout_is_nonzero(vio->write_timeout))
    ret|= vio_timeout(&new_vio, 1, vio->write_timeout);

  if (ret)
  {
    /*
      vio_reset() failed
      free resources allocated by vio_init
    */
    my_free(new_vio.read_buffer);
  }
  else
  {
    /*
      vio_reset() succeeded
      free old resources and then overwrite VIO structure
    */

    /*
      Close socket only when it is not equal to the new one.
    */
    if (sd != mysql_socket_getfd(vio->mysql_socket))
      if (vio->inactive == FALSE)
        vio->vioshutdown(vio);

    my_free(vio->read_buffer);

    *vio= new_vio;
  }

  DBUG_RETURN(MY_TEST(ret));
}


/* Create a new VIO for socket or TCP/IP connection. */

Vio *mysql_socket_vio_new(MYSQL_SOCKET mysql_socket, enum enum_vio_type type, uint flags)
{
  Vio *vio;
  my_socket sd= mysql_socket_getfd(mysql_socket);
  DBUG_ENTER("mysql_socket_vio_new");
  DBUG_PRINT("enter", ("sd: %d", sd));
  if ((vio = (Vio*) my_malloc(sizeof(*vio),MYF(MY_WME))))
  {
    vio_init(vio, type, sd, flags);
    vio->mysql_socket= mysql_socket;
  }
  DBUG_RETURN(vio);
}

/* Open the socket or TCP/IP connection and read the fnctl() status */

Vio *vio_new(my_socket sd, enum enum_vio_type type, uint flags)
{
  Vio *vio;
  MYSQL_SOCKET mysql_socket= MYSQL_INVALID_SOCKET;
  DBUG_ENTER("vio_new");
  DBUG_PRINT("enter", ("sd: %d", sd));

  mysql_socket_setfd(&mysql_socket, sd);
  vio = mysql_socket_vio_new(mysql_socket, type, flags);

  DBUG_RETURN(vio);
}

#ifdef _WIN32

Vio *vio_new_win32pipe(HANDLE hPipe)
{
  Vio *vio;
  DBUG_ENTER("vio_new_handle");
  if ((vio = (Vio*) my_malloc(sizeof(Vio),MYF(MY_WME))))
  {
    vio_init(vio, VIO_TYPE_NAMEDPIPE, 0, VIO_LOCALHOST);
    /* Create an object for event notification. */
    vio->overlapped.hEvent= CreateEvent(NULL, FALSE, FALSE, NULL);
    if (vio->overlapped.hEvent == NULL)
    {
      my_free(vio);
      DBUG_RETURN(NULL);
    }
    vio->hPipe= hPipe;
    strmov(vio->desc, "named pipe");
  }
  DBUG_RETURN(vio);
}

#ifdef HAVE_SMEM
Vio *vio_new_win32shared_memory(HANDLE handle_file_map, HANDLE handle_map,
                                HANDLE event_server_wrote, HANDLE event_server_read,
                                HANDLE event_client_wrote, HANDLE event_client_read,
                                HANDLE event_conn_closed)
{
  Vio *vio;
  DBUG_ENTER("vio_new_win32shared_memory");
  if ((vio = (Vio*) my_malloc(sizeof(Vio),MYF(MY_WME))))
  {
    vio_init(vio, VIO_TYPE_SHARED_MEMORY, 0, VIO_LOCALHOST);
    vio->handle_file_map= handle_file_map;
    vio->handle_map= handle_map;
    vio->event_server_wrote= event_server_wrote;
    vio->event_server_read= event_server_read;
    vio->event_client_wrote= event_client_wrote;
    vio->event_client_read= event_client_read;
    vio->event_conn_closed= event_conn_closed;
    vio->shared_memory_remain= 0;
    vio->shared_memory_pos= handle_map;
    strmov(vio->desc, "shared memory");
  }
  DBUG_RETURN(vio);
}
#endif
#endif


/**
  Set timeout for a network send or receive operation.

  @remark A non-infinite timeout causes the socket to be
          set to non-blocking mode. On infinite timeouts,
          the socket is set to blocking mode.

  @remark A negative timeout means an infinite timeout.

  @param vio      A VIO object.
  @param which    Whether timeout is for send (1) or receive (0).
  @param timeout  Timeout interval, as a timeout_t.

  @return FALSE on success, TRUE otherwise.
*/

int vio_timeout(Vio *vio, uint which, timeout_t timeout)
{
  my_bool old_mode;

  /* Deduce the current timeout status mode. */
  old_mode =
    timeout_is_infinite(vio->write_timeout) &&
    timeout_is_infinite(vio->read_timeout);

  if (which)
    vio->write_timeout = timeout;
  else
    vio->read_timeout = timeout;

  /* VIO-specific timeout handling. Might change the blocking mode. */
  return vio->timeout ? vio->timeout(vio, which, old_mode) : 0;
}


void vio_delete(Vio* vio)
{
  if (!vio)
    return; /* It must be safe to delete null pointers. */

  if (vio->inactive == FALSE)
    vio->vioshutdown(vio);
  my_free(vio->read_buffer);
  my_free(vio);
}


/*
  Cleanup memory allocated by vio or the
  components below it when application finish

*/
void vio_end(void)
{
#if defined(HAVE_YASSL)
  yaSSL_CleanUp();
#elif defined(HAVE_OPENSSL)
  // This one is needed on the client side
  ERR_remove_state(0);
  ERR_free_strings();
  EVP_cleanup();
  CRYPTO_cleanup_all_ex_data();
#endif
}
