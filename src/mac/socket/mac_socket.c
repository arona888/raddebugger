// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal SOCK_Endpoint
mac_sock_endpoint(struct sockaddr_storage *address)
{
  SOCK_Endpoint result = {0};
  if(address->ss_family == AF_INET)
  {
    struct sockaddr_in *v4 = (void *)address;
    result.port = ntohs(v4->sin_port);
    MemoryCopy(result.address_u8, &v4->sin_addr, 4);
  }
  else if(address->ss_family == AF_INET6)
  {
    struct sockaddr_in6 *v6 = (void *)address;
    result.port = ntohs(v6->sin6_port);
    if(IN6_IS_ADDR_V4MAPPED(&v6->sin6_addr)) { MemoryCopy(result.address_u8, &v6->sin6_addr.s6_addr[12], 4); }
    else { result.kind = SOCK_EndpointKind_IPv6; MemoryCopy(result.address_u8, &v6->sin6_addr, 16); }
  }
  return result;
}

internal socklen_t
mac_sock_address(SOCK_Endpoint endpoint, struct sockaddr_storage *address)
{
  MemoryZeroStruct(address);
  if(endpoint.kind == SOCK_EndpointKind_IPv6)
  {
    struct sockaddr_in6 *v6 = (void *)address;
    v6->sin6_len = sizeof(*v6); v6->sin6_family = AF_INET6; v6->sin6_port = htons(endpoint.port);
    MemoryCopy(&v6->sin6_addr, endpoint.address_u8, 16);
    return sizeof(*v6);
  }
  struct sockaddr_in *v4 = (void *)address;
  v4->sin_len = sizeof(*v4); v4->sin_family = AF_INET; v4->sin_port = htons(endpoint.port);
  MemoryCopy(&v4->sin_addr, endpoint.address_u8, 4);
  return sizeof(*v4);
}

internal void
mac_sock_configure(int fd)
{
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0)|O_NONBLOCK);
  int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
}

internal MAC_SOCK_Connection *
mac_sock_add(MAC_SOCK_Session *session, int fd, SOCK_Protocol protocol, SOCK_Endpoint endpoint, B32 listener)
{
  MAC_SOCK_Connection *c = push_array(session->arena, MAC_SOCK_Connection, 1);
  c->fd = fd; c->protocol = protocol; c->endpoint = endpoint; c->listener = listener;
  SLLStackPush(session->connections, c);
  mac_sock_configure(fd);
  struct kevent event;
  EV_SET(&event, fd, EVFILT_READ, EV_ADD, 0, 0, c);
  if(kevent(session->queue, &event, 1, 0, 0, 0) < 0) { close(fd); c->fd = -1; return 0; }
  return c;
}

internal void
mac_sock_close_connection(MAC_SOCK_Connection *c)
{
  if(c->fd >= 0) { close(c->fd); c->fd = -1; }
}

internal B32
mac_sock_wait(int fd, short events, U64 deadline, MAC_SOCK_Session *session)
{
  for(;;)
  {
    if(ins_atomic_u32_eval(&session->closing)) { return 0; }
    U64 now = now_time_us();
    int ms = deadline == max_U64 ? 100 : (int)Min(100, deadline > now ? (deadline-now+999)/1000 : 0);
    struct pollfd p = {.fd = fd, .events = events};
    int result = MAC_RETRY_ON_EINTR(poll(&p, 1, ms));
    if(result < 0 || (p.revents & (POLLERR|POLLNVAL))) { return 0; }
    if(p.revents & events) { return 1; }
    if(p.revents & POLLHUP) { return 0; }
    if(now_time_us() >= deadline) { return 0; }
  }
}

internal MAC_SOCK_Connection *
mac_sock_connect(MAC_SOCK_Session *s, MAC_SOCK_Header *header)
{
  for EachNode(c, MAC_SOCK_Connection, s->connections)
  {
    if(c->fd >= 0 && !c->listener && c->protocol == header->protocol && sock_endpoint_match(c->endpoint, header->endpoint)) { return c; }
  }
  struct sockaddr_storage address;
  socklen_t size = mac_sock_address(header->endpoint, &address);
  int fd = socket(address.ss_family, header->protocol == SOCK_Protocol_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
  if(fd < 0) { return 0; }
  mac_sock_configure(fd);
  int result = connect(fd, (struct sockaddr *)&address, size);
  if(result != 0)
  {
    if(errno != EINPROGRESS || !mac_sock_wait(fd, POLLOUT, header->deadline, s)) { close(fd); return 0; }
    int error = 0; socklen_t error_size = sizeof(error);
    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) != 0 || error) { close(fd); return 0; }
  }
  return mac_sock_add(s, fd, header->protocol, header->endpoint, 0);
}

internal void
mac_sock_drain_sends(MAC_SOCK_Session *s)
{
  for(;;)
  {
    MAC_SOCK_Header header;
    RingGuard guard = guarded_ring_open(s->send);
    B32 got = guarded_ring_try_read(&guard, sizeof(header), &header);
    if(got) { guarded_ring_read_or_wait(&guard, header.size, s->send_buffer, max_U64); }
    guarded_ring_close(&guard);
    if(!got || ins_atomic_u32_eval(&s->closing)) { break; }
    MAC_SOCK_Connection *c = mac_sock_connect(s, &header);
    if(!c) { continue; }
    U64 offset = 0;
    do
    {
      ssize_t n = MAC_RETRY_ON_EINTR(send(c->fd, s->send_buffer+offset, header.size-offset, 0));
      if(n >= 0)
      {
        offset += n;
        if(header.protocol == SOCK_Protocol_UDP || !n) { break; }
      }
      else if((errno != EAGAIN && errno != EWOULDBLOCK) || !mac_sock_wait(c->fd, POLLOUT, header.deadline, s))
      {
        mac_sock_close_connection(c); break;
      }
    } while(offset < header.size);
  }
}

internal B32
mac_sock_queue_receive(MAC_SOCK_Session *s, SOCK_Protocol protocol, SOCK_Endpoint endpoint, U8 *bytes, U64 size)
{
  MAC_SOCK_Header header = {.endpoint = endpoint, .size = size, .protocol = protocol};
  RingGuard guard = guarded_ring_open(s->recv);
  B32 result = 0;
  while(!ins_atomic_u32_eval(&s->closing))
  {
    // Reserve the whole record before publishing its header, including when the
    // receiver drains concurrently or a session is closing under backpressure.
    U64 used = s->recv->ring->write_pos-s->recv->ring->read_pos;
    if(s->recv->ring->size-used >= sizeof(header)+size)
    {
      guarded_ring_try_write(&guard, sizeof(header), &header);
      guarded_ring_try_write(&guard, size, bytes);
      result = 1; break;
    }
    cond_var_wait(s->recv->cv, s->recv->mutex, now_time_us()+100000);
  }
  guarded_ring_close(&guard);
  if(result && s->wakeup) { s->wakeup(); }
  return result;
}

internal void
mac_sock_thread(void *ptr)
{
  ThreadNameF("mac_socket_io");
  MAC_SOCK_Session *s = ptr;
  U8 buffer[65536];
  while(!ins_atomic_u32_eval(&s->closing))
  {
    struct kevent events[16];
    int count = MAC_RETRY_ON_EINTR(kevent(s->queue, 0, 0, events, ArrayCount(events), 0));
    if(count < 0) { break; }
    for(int i = 0; i < count && !ins_atomic_u32_eval(&s->closing); i += 1)
    {
      if(events[i].filter == EVFILT_USER) { mac_sock_drain_sends(s); continue; }
      MAC_SOCK_Connection *c = events[i].udata;
      if(!c || c->fd < 0) { continue; }
      if(events[i].flags & EV_ERROR) { mac_sock_close_connection(c); continue; }
      if(c->listener && c->protocol == SOCK_Protocol_TCP)
      {
        for(;;)
        {
          struct sockaddr_storage address; socklen_t size = sizeof(address);
          int fd = MAC_RETRY_ON_EINTR(accept(c->fd, (struct sockaddr *)&address, &size));
          if(fd < 0) { break; }
          mac_sock_add(s, fd, SOCK_Protocol_TCP, mac_sock_endpoint(&address), 0);
        }
      }
      else
      {
        for(;;)
        {
          struct sockaddr_storage address; socklen_t address_size = sizeof(address);
          ssize_t n = MAC_RETRY_ON_EINTR(recvfrom(c->fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&address, &address_size));
          if(n < 0)
          {
            if(errno != EAGAIN && errno != EWOULDBLOCK) { mac_sock_close_connection(c); }
            break;
          }
          if(!n && c->protocol == SOCK_Protocol_TCP) { mac_sock_close_connection(c); break; }
          SOCK_Endpoint source = c->protocol == SOCK_Protocol_UDP ? mac_sock_endpoint(&address) : c->endpoint;
          if(!mac_sock_queue_receive(s, c->protocol, source, buffer, n)) { break; }
        }
      }
    }
  }
  for EachNode(c, MAC_SOCK_Connection, s->connections) { mac_sock_close_connection(c); }
}

internal void sock_init(void) { /* Sessions own their independent kqueue and I/O thread. */ }
internal void sock_async_tick(void) { /* I/O is serviced by kqueue rather than async lanes. */ }

internal SOCK_Session
sock_session_open(U16 port, SOCK_WakeupFunctionType *wakeup)
{
  Arena *arena = arena_alloc();
  MAC_SOCK_Session *s = push_array(arena, MAC_SOCK_Session, 1);
  s->arena = arena; s->wakeup = wakeup; s->queue = kqueue();
  if(s->queue < 0) { arena_release(arena); return (SOCK_Session){0}; }
  fcntl(s->queue, F_SETFD, FD_CLOEXEC);
  struct kevent event;
  EV_SET(&event, 1, EVFILT_USER, EV_ADD|EV_CLEAR, 0, 0, 0);
  if(kevent(s->queue, &event, 1, 0, 0, 0) < 0) { close(s->queue); arena_release(arena); return (SOCK_Session){0}; }
  s->send = guarded_ring_alloc(arena, KB(256)); s->recv = guarded_ring_alloc(arena, KB(256));
  s->send_buffer = push_array_no_zero(arena, U8, KB(256));
  B32 have_tcp_listener = port == 0;
  if(port) for(int family_index = 0; family_index < 2; family_index += 1) for(int protocol_index = 0; protocol_index < 2; protocol_index += 1)
  {
    SOCK_Endpoint endpoint = {.port = port, .kind = family_index == 0 ? SOCK_EndpointKind_IPv4 : SOCK_EndpointKind_IPv6};
    struct sockaddr_storage address; socklen_t size = mac_sock_address(endpoint, &address);
    SOCK_Protocol protocol = protocol_index == 0 ? SOCK_Protocol_TCP : SOCK_Protocol_UDP;
    int fd = socket(address.ss_family, protocol == SOCK_Protocol_TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if(fd < 0) { continue; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if(address.ss_family == AF_INET6) { setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)); }
    if(bind(fd, (struct sockaddr *)&address, size) != 0 || (protocol == SOCK_Protocol_TCP && listen(fd, SOMAXCONN) != 0)) { close(fd); continue; }
    if(mac_sock_add(s, fd, protocol, endpoint, 1) && protocol == SOCK_Protocol_TCP) { have_tcp_listener = 1; }
  }
  if(have_tcp_listener) { s->thread = thread_launch(mac_sock_thread, s); }
  if(!s->thread.u64[0])
  {
    for EachNode(c, MAC_SOCK_Connection, s->connections) { mac_sock_close_connection(c); }
    close(s->queue); guarded_ring_release(s->send); guarded_ring_release(s->recv); arena_release(arena);
    return (SOCK_Session){0};
  }
  return (SOCK_Session){(U64)s};
}

internal void
sock_session_close(SOCK_Session handle)
{
  MAC_SOCK_Session *s = (void *)handle.u64[0];
  if(!s) { return; }
  ins_atomic_u32_eval_assign(&s->closing, 1);
  cond_var_broadcast(s->recv->cv); cond_var_broadcast(s->send->cv);
  struct kevent event; EV_SET(&event, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, 0);
  kevent(s->queue, &event, 1, 0, 0, 0);
  thread_join(s->thread, max_U64);
  close(s->queue); guarded_ring_release(s->send); guarded_ring_release(s->recv);
  arena_release(s->arena);
}

internal B32
sock_send(SOCK_Session handle, SOCK_Protocol protocol, SOCK_Endpoint endpoint, String8 data, U64 deadline)
{
  MAC_SOCK_Session *s = (void *)handle.u64[0];
  if(!s || ins_atomic_u32_eval(&s->closing) || (protocol != SOCK_Protocol_TCP && protocol != SOCK_Protocol_UDP) ||
     data.size > s->send->ring->size-sizeof(MAC_SOCK_Header) || (protocol == SOCK_Protocol_UDP && data.size > 65507)) { return 0; }
  MAC_SOCK_Header header = {.endpoint = endpoint, .size = data.size, .deadline = deadline, .protocol = protocol};
  RingGuard guard = guarded_ring_open(s->send);
  B32 result = 0;
  for(;;)
  {
    if(ins_atomic_u32_eval(&s->closing)) { break; }
    U64 used = s->send->ring->write_pos-s->send->ring->read_pos;
    if(s->send->ring->size-used >= sizeof(header)+data.size)
    {
      guarded_ring_try_write(&guard, sizeof(header), &header);
      guarded_ring_try_write(&guard, data.size, data.str);
      result = 1; break;
    }
    if(now_time_us() >= deadline) { break; }
    cond_var_wait(s->send->cv, s->send->mutex, deadline);
  }
  guarded_ring_close(&guard);
  if(result)
  {
    struct kevent event; EV_SET(&event, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, 0);
    result = kevent(s->queue, &event, 1, 0, 0, 0) == 0;
  }
  return result;
}

internal B32
sock_recv(Arena *arena, SOCK_Session handle, SOCK_Protocol *protocol, SOCK_Endpoint *endpoint, String8 *data, U64 deadline)
{
  MAC_SOCK_Session *s = (void *)handle.u64[0];
  if(!s || !data) { return 0; }
  RingGuard guard = guarded_ring_open(s->recv);
  MAC_SOCK_Header header; B32 result = 0;
  for(;;)
  {
    if(guarded_ring_try_read(&guard, sizeof(header), &header))
    {
      if(protocol) { *protocol = header.protocol; } if(endpoint) { *endpoint = header.endpoint; }
      data->size = header.size; data->str = push_array_no_zero(arena, U8, header.size);
      guarded_ring_read_or_wait(&guard, header.size, data->str, max_U64);
      result = 1; break;
    }
    if(ins_atomic_u32_eval(&s->closing) || now_time_us() >= deadline) { break; }
    cond_var_wait(s->recv->cv, s->recv->mutex, deadline);
  }
  guarded_ring_close(&guard);
  return result;
}
