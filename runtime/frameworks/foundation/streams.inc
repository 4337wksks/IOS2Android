#include <netdb.h>
static int socket_errno(int);
static Word cf_retain(Word);
static Word cf_data_no_copy(Word,const void *,intptr_t,Word);
static void schedule_main_function(void (*function)(void *),void *context);
static struct HostObject main_run_loop;
static _Thread_local struct HostObject thread_run_loop;
static void *dispatch_export(const char *);
static ALooper *platform_main_looper;
static Word loop_function_create(Word,void (*)(void *),void *,void (*)(void *));
static void loop_function_signal(Word),loop_function_close(Word);
static Word main_runloop(Word cls) { main_run_loop.isa=cls; return (Word)&main_run_loop; }
static Word current_runloop(Word cls) {
    if (gettid()==getpid()) return main_runloop(cls);
    thread_run_loop.isa=cls; return (Word)&thread_run_loop;
}
static Word core_main_runloop(void) { return main_runloop(external_class("_OBJC_CLASS_$_NSRunLoop")); }
static Word core_current_runloop(void) { return current_runloop(external_class("_OBJC_CLASS_$_NSRunLoop")); }
struct StreamSocket;
struct StreamClientContext { intptr_t version;Word info,retain,release,description; };
struct StreamState {
    Word object,source,memory,error,delegate,runloop;
    size_t position,length;
    int output,append,fd;_Atomic int status;
    unsigned pending;
    Word delivery,client,client_mask;struct StreamClientContext client_context;
    Method delegate_method;unsigned delegate_epoch;
    struct StreamSocket *socket;
    struct StreamState *next;
};
static struct StreamState *stream_states;
static pthread_mutex_t stream_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct StreamState *stream_state(Word object) {
    pthread_mutex_lock(&stream_mutex);
    struct StreamState *state=stream_states;
    while (state && state->object!=object) state=state->next;
    if (!state) {
        state=calloc(1,sizeof(*state));
        if (!state) { pthread_mutex_unlock(&stream_mutex); stop("Allocation failed","NSStream"); }
        state->object=object; state->fd=-1;
        state->output=read_word(object)==external_class("_OBJC_CLASS_$_NSOutputStream");
        state->next=stream_states; stream_states=state;
    }
    pthread_mutex_unlock(&stream_mutex);
    return state;
}
static void stream_deliver(void *context) {
    struct StreamState *state=context;
    unsigned events=__atomic_exchange_n(&state->pending,0,__ATOMIC_ACQ_REL);
    if ((!state->delegate && !state->client) || !state->runloop) return;
    if(state->delegate && (!state->delegate_method || state->delegate_epoch!=method_cache_epoch)){state->delegate_method=find_implementation(state->delegate,"stream:handleEvent:");state->delegate_epoch=method_cache_epoch;}
    for (unsigned event=1;event<=16;event<<=1) {
        if (!(events&event) || state->status==6) continue;
        if ((event==2 || event==4) && state->status!=2) continue;
        if(state->client && (state->client_mask&event))((void (*)(Word,Word,Word))state->client)(state->object,event,state->client_context.info);
        else if(state->delegate_method)state->delegate_method(state->delegate,"stream:handleEvent:",state->object,event,0,0);
    }
}
static void stream_event(struct StreamState *state,unsigned events) {
    if (!state->runloop || (!state->delegate && !state->client)) return;
    if (!__atomic_fetch_or(&state->pending,events,__ATOMIC_ACQ_REL)) loop_function_signal(state->delivery);
}
static void stream_failure(struct StreamState *state,int error) {
    file_failure(&state->error,state->source ? string_bytes(state->source,"NSStream") : "Memory stream",error);
    state->status=7; stream_event(state,8);
}
enum { STREAM_DNS=1,STREAM_CONNECT,STREAM_SOCKS_GREETING_SEND,STREAM_SOCKS_GREETING_READ,STREAM_SOCKS_AUTH_SEND,STREAM_SOCKS_AUTH_READ,STREAM_SOCKS_CONNECT_SEND,STREAM_SOCKS_CONNECT_READ,STREAM_SOCKS_LENGTH,STREAM_SOCKS_ADDRESS,STREAM_TLS,STREAM_READY,STREAM_FAILED };
struct StreamSocket {
    pthread_mutex_t mutex;struct StreamState *input,*output;
    char *host,*proxy,*user,*password,*peer;unsigned port,proxy_port;
    int fd,phase,started,dead,encrypted,defer_verification,dns_pending,read_ready,write_ready,connected,tls_writer_waiting;
    unsigned interests;struct addrinfo *addresses,*next_address;struct I2ATLS *tls;
    unsigned char buffer[520];size_t size,offset;
};
static int stream_socket_event(int,int,void *);
static void stream_socket_watch(struct StreamSocket *s,unsigned events) {
    s->interests=events;
    if(s->fd<0 || !platform_main_looper)return;
    if(events)ALooper_addFd(platform_main_looper,s->fd,ALOOPER_POLL_CALLBACK,(int)events,stream_socket_event,s);
    else ALooper_removeFd(platform_main_looper,s->fd);
}
static void stream_socket_error(struct StreamSocket *s,int code,int tls) {
    s->phase=STREAM_FAILED;stream_socket_watch(s,0);
    if(s->fd>=0){close(s->fd);s->fd=-1;}
    struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(info,"Socket stream",copy_string(tls ? "TLS stream failed" : strerror(code)),copy_string("NSLocalizedDescription"));
    Word error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"Socket stream",copy_string(tls ? "NSOSStatusErrorDomain" : "NSPOSIXErrorDomain"),tls ? code : socket_errno(code),(Word)info);
    if(s->input->status!=6){s->input->error=error;s->input->status=7;stream_event(s->input,8);}
    if(s->output->status!=6){s->output->error=error;s->output->status=7;stream_event(s->output,8);}
}
static int stream_socket_read_tls(Word token,void *bytes,size_t *length) {
    struct StreamSocket *s=(void *)token;ssize_t count;do{count=recv(s->fd,bytes,*length,0);}while(count<0 && errno==EINTR);
    *length=count>0 ? (size_t)count : 0;
    return count>0 ? 0 : !count ? -9805 : errno==EAGAIN || errno==EWOULDBLOCK ? -9803 : -9802;
}
static int stream_socket_write_tls(Word token,const void *bytes,size_t *length) {
    struct StreamSocket *s=(void *)token;ssize_t count;do{count=send(s->fd,bytes,*length,MSG_NOSIGNAL);}while(count<0 && errno==EINTR);
    *length=count>0 ? (size_t)count : 0;
    return count>=0 ? 0 : errno==EAGAIN || errno==EWOULDBLOCK ? -9803 : -9802;
}
static void stream_socket_ready(struct StreamSocket *s) {
    s->phase=STREAM_READY;s->write_ready=1;
    if(s->input->status==1){s->input->status=2;stream_event(s->input,1);}
    if(s->output->status==1){s->output->status=2;stream_event(s->output,1);}
    stream_event(s->output,4);stream_socket_watch(s,ALOOPER_EVENT_INPUT);
}
static int stream_socket_connect_next(struct StreamSocket *s) {
    if(s->fd>=0){stream_socket_watch(s,0);close(s->fd);s->fd=-1;}
    int error=EHOSTUNREACH;
    while(s->next_address){struct addrinfo *address=s->next_address;s->next_address=address->ai_next;
        int fd=socket(address->ai_family,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,address->ai_protocol);if(fd<0){error=errno;continue;}
        int result=connect(fd,address->ai_addr,address->ai_addrlen);error=errno;
        if(!result || error==EINPROGRESS){s->fd=fd;s->phase=STREAM_CONNECT;s->connected=!result;if(result)stream_socket_watch(s,ALOOPER_EVENT_OUTPUT);return !result;}
        close(fd);
    }
    stream_socket_error(s,error,0);return 0;
}
static void stream_socket_socks_request(struct StreamSocket *s) {
    size_t length=strlen(s->host);s->buffer[0]=5;s->buffer[1]=1;s->buffer[2]=0;
    unsigned char address[16];size_t offset;
    if(inet_pton(AF_INET,s->host,address)==1){s->buffer[3]=1;memcpy(s->buffer+4,address,4);offset=8;}
    else if(inet_pton(AF_INET6,s->host,address)==1){s->buffer[3]=4;memcpy(s->buffer+4,address,16);offset=20;}
    else {if(length>255){stream_socket_error(s,EINVAL,0);return;}s->buffer[3]=3;s->buffer[4]=(unsigned char)length;memcpy(s->buffer+5,s->host,length);offset=5+length;}
    s->buffer[offset]=(unsigned char)(s->port>>8);s->buffer[offset+1]=(unsigned char)s->port;
    s->size=offset+2;s->offset=0;s->phase=STREAM_SOCKS_CONNECT_SEND;
}
static void stream_socket_progress(struct StreamSocket *s,int events) {
    if(s->dead || s->phase==STREAM_FAILED || s->phase==STREAM_DNS)return;
    if(s->phase==STREAM_CONNECT){if(!s->connected && !(events&(ALOOPER_EVENT_OUTPUT|ALOOPER_EVENT_ERROR|ALOOPER_EVENT_HANGUP)))return;int code=0;socklen_t length=sizeof(code);if(getsockopt(s->fd,SOL_SOCKET,SO_ERROR,&code,&length))code=errno;
        if(code){if(!stream_socket_connect_next(s))return;}
        s->connected=1;if(s->proxy){s->buffer[0]=5;s->buffer[1]=s->user ? 2 : 1;s->buffer[2]=0;s->buffer[3]=2;s->size=s->user ? 4 : 3;s->offset=0;s->phase=STREAM_SOCKS_GREETING_SEND;}
        else s->phase=STREAM_TLS;
    }
    while(s->phase>=STREAM_SOCKS_GREETING_SEND && s->phase<=STREAM_SOCKS_ADDRESS){
        int sending=s->phase==STREAM_SOCKS_GREETING_SEND || s->phase==STREAM_SOCKS_AUTH_SEND || s->phase==STREAM_SOCKS_CONNECT_SEND;
        ssize_t count;do{count=sending ? send(s->fd,s->buffer+s->offset,s->size-s->offset,MSG_NOSIGNAL) : recv(s->fd,s->buffer+s->offset,s->size-s->offset,0);}while(count<0 && errno==EINTR);
        if(count<0){if(errno==EAGAIN || errno==EWOULDBLOCK){stream_socket_watch(s,sending ? ALOOPER_EVENT_OUTPUT : ALOOPER_EVENT_INPUT);return;}stream_socket_error(s,errno,0);return;}
        if(!count){stream_socket_error(s,ECONNRESET,0);return;}s->offset+=(size_t)count;if(s->offset<s->size)continue;
        int phase=s->phase;s->offset=0;
        if(phase==STREAM_SOCKS_GREETING_SEND){s->phase=STREAM_SOCKS_GREETING_READ;s->size=2;}
        else if(phase==STREAM_SOCKS_GREETING_READ){
            if(s->buffer[0]!=5 || (s->buffer[1]!=0 && s->buffer[1]!=2)){stream_socket_error(s,EACCES,0);return;}
            if(!s->buffer[1])stream_socket_socks_request(s);
            else {if(!s->user){stream_socket_error(s,EACCES,0);return;}size_t u=strlen(s->user),p=s->password ? strlen(s->password) : 0;
                if(u>255 || p>255){stream_socket_error(s,EINVAL,0);return;}s->buffer[0]=1;s->buffer[1]=(unsigned char)u;memcpy(s->buffer+2,s->user,u);s->buffer[2+u]=(unsigned char)p;if(p)memcpy(s->buffer+3+u,s->password,p);s->size=3+u+p;s->phase=STREAM_SOCKS_AUTH_SEND;}
        } else if(phase==STREAM_SOCKS_AUTH_SEND){s->phase=STREAM_SOCKS_AUTH_READ;s->size=2;}
        else if(phase==STREAM_SOCKS_AUTH_READ){if(s->buffer[0]!=1 || s->buffer[1]){stream_socket_error(s,EACCES,0);return;}stream_socket_socks_request(s);}
        else if(phase==STREAM_SOCKS_CONNECT_SEND){s->phase=STREAM_SOCKS_CONNECT_READ;s->size=4;}
        else if(phase==STREAM_SOCKS_CONNECT_READ){if(s->buffer[0]!=5 || s->buffer[1] || s->buffer[2]){stream_socket_error(s,ECONNREFUSED,0);return;}
            unsigned kind=s->buffer[3];if(kind==3){s->phase=STREAM_SOCKS_LENGTH;s->size=1;}else if(kind==1 || kind==4){s->phase=STREAM_SOCKS_ADDRESS;s->size=kind==1 ? 6 : 18;}else{stream_socket_error(s,EPROTO,0);return;}}
        else if(phase==STREAM_SOCKS_LENGTH){s->size=(size_t)s->buffer[0]+2;s->phase=STREAM_SOCKS_ADDRESS;}
        else s->phase=STREAM_TLS;
    }
    if(s->phase==STREAM_TLS){
        if(!s->encrypted){stream_socket_ready(s);return;}
        if(!s->tls){s->tls=(void *)i2a_ssl_create(0,1,0);if(!s->tls){stream_socket_error(s,ENOMEM,0);return;}
            const char *peer=s->peer ? s->peer : s->host;
            if(i2a_ssl_hostname(s->tls,peer,strlen(peer)) || i2a_ssl_connection(s->tls,(Word)s) || i2a_ssl_io(s->tls,stream_socket_read_tls,stream_socket_write_tls)){stream_socket_error(s,-50,1);return;}
            s->tls->defer_verification=s->defer_verification;
        }
        int code=i2a_ssl_handshake(s->tls);if(code==-9803){stream_socket_watch(s,s->tls->waiting ? s->tls->waiting : ALOOPER_EVENT_INPUT);return;}
        if(code){stream_socket_error(s,code,1);return;}stream_socket_ready(s);return;
    }
    if(s->phase==STREAM_READY){unsigned next=s->interests&~(unsigned)events;
        if(events&ALOOPER_EVENT_INPUT){s->read_ready=1;stream_event(s->input,2);if(s->tls_writer_waiting){s->write_ready=1;stream_event(s->output,4);}}
        if(events&ALOOPER_EVENT_OUTPUT){if(s->tls && !s->tls_writer_waiting && s->tls->waiting==ALOOPER_EVENT_OUTPUT){s->read_ready=1;stream_event(s->input,2);}else{s->write_ready=1;stream_event(s->output,4);}}
        if(events&(ALOOPER_EVENT_ERROR|ALOOPER_EVENT_INVALID)){stream_socket_error(s,ECONNRESET,0);return;}
        if(events&ALOOPER_EVENT_HANGUP){s->read_ready=1;stream_event(s->input,2);}
        stream_socket_watch(s,next);
    }
}
static void stream_socket_pump(void *context) {
    struct StreamSocket *s=context;pthread_mutex_lock(&s->mutex);
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};active_guest_call_boundary=&call;
    int failed=setjmp(call.environment);if(!failed)stream_socket_progress(s,0);active_guest_call_boundary=call.previous;
    pthread_mutex_unlock(&s->mutex);if(failed){pthread_mutex_lock(&s->mutex);stream_socket_error(s,EIO,0);pthread_mutex_unlock(&s->mutex);}
}
static void stream_socket_dispose_names(struct StreamSocket *s) {
    if(s->dns_pending)return;free(s->host);free(s->proxy);free(s->user);free(s->password);free(s->peer);
    s->host=s->proxy=s->user=s->password=s->peer=NULL;
}
static int stream_socket_event(int fd,int events,void *context) {
    struct StreamSocket *s=context;pthread_mutex_lock(&s->mutex);
    if(s->fd!=fd || s->dead){pthread_mutex_unlock(&s->mutex);return 0;}
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};active_guest_call_boundary=&call;
    int failed=setjmp(call.environment);if(!failed)stream_socket_progress(s,events);active_guest_call_boundary=call.previous;
    int keep=s->interests!=0;pthread_mutex_unlock(&s->mutex);
    if(failed){pthread_mutex_lock(&s->mutex);stream_socket_error(s,EIO,0);pthread_mutex_unlock(&s->mutex);return 0;}return keep;
}
static void stream_socket_resolve(void *context) {
    struct StreamSocket *s=context;struct addrinfo hints={.ai_socktype=SOCK_STREAM,.ai_family=AF_UNSPEC},*addresses=NULL;
    char port[16];snprintf(port,sizeof(port),"%u",s->proxy ? s->proxy_port : s->port);
    int result=getaddrinfo(s->proxy ? s->proxy : s->host,port,&hints,&addresses);
    pthread_mutex_lock(&s->mutex);s->dns_pending=0;
    if(s->dead){if(addresses)freeaddrinfo(addresses);stream_socket_dispose_names(s);pthread_mutex_unlock(&s->mutex);return;}
    s->addresses=s->next_address=addresses;
    if(result)stream_socket_error(s,EHOSTUNREACH,0);else stream_socket_connect_next(s);
    pthread_mutex_unlock(&s->mutex);schedule_main_function(stream_socket_pump,s);
}
static void stream_socket_open(struct StreamState *state) {
    struct StreamSocket *s=state->socket;pthread_mutex_lock(&s->mutex);
    if(s->dead || s->phase==STREAM_FAILED){state->status=7;pthread_mutex_unlock(&s->mutex);return;}
    state->status=1;int start=!s->started;if(start){s->started=1;s->dns_pending=1;s->phase=STREAM_DNS;}
    if(s->phase==STREAM_READY){state->status=2;stream_event(state,1|(state->output ? 4 : s->read_ready ? 2 : 0));}
    pthread_mutex_unlock(&s->mutex);
    if(start){void *(*global)(long,unsigned long)=dispatch_export("dispatch_get_global_queue");void (*enqueue)(void *,void *,void (*)(void *))=dispatch_export("dispatch_async_f");enqueue(global(0,0),s,stream_socket_resolve);}
}
static void stream_socket_pair(Word allocator,Word host,unsigned port,Word *input,Word *output) {
    if(input)*input=0;if(output)*output=0;if(allocator || !host || !port || port>65535 || (!input && !output))return;
    const char *name=string_bytes(host,"Socket stream host");if(!*name || strlen(name)>253)return;
    struct StreamSocket *s=calloc(1,sizeof(*s));if(!s)return;s->fd=-1;s->port=port;s->host=strdup(name);if(!s->host){free(s);return;}pthread_mutex_init(&s->mutex,NULL);
    s->input=stream_state(host_allocate(external_class("_OBJC_CLASS_$_NSInputStream")));s->output=stream_state(host_allocate(external_class("_OBJC_CLASS_$_NSOutputStream")));
    s->input->socket=s->output->socket=s;if(input)*input=s->input->object;else s->input->status=6;if(output)*output=s->output->object;else s->output->status=6;
}
static void stream_socket_close(struct StreamState *state) {
    struct StreamSocket *s=state->socket;pthread_mutex_lock(&s->mutex);state->status=6;
    if(s->fd>=0 && !s->tls)shutdown(s->fd,state->output ? SHUT_WR : SHUT_RD);
    if(s->input->status==6 && s->output->status==6){s->dead=1;stream_socket_watch(s,0);if(s->fd>=0){close(s->fd);s->fd=-1;}
        if(s->tls){secure_transport_release((Word)s->tls);s->tls=NULL;}if(s->addresses){freeaddrinfo(s->addresses);s->addresses=s->next_address=NULL;}stream_socket_dispose_names(s);}
    pthread_mutex_unlock(&s->mutex);
}
static intptr_t stream_socket_transfer(struct StreamState *state,void *bytes,size_t size,int output) {
    struct StreamSocket *s=state->socket;if(!size)return 0;pthread_mutex_lock(&s->mutex);
    if(s->phase!=STREAM_READY || s->dead){pthread_mutex_unlock(&s->mutex);return state->status==7 ? -1 : 0;}
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};active_guest_call_boundary=&call;
    if(setjmp(call.environment)){active_guest_call_boundary=call.previous;pthread_mutex_unlock(&s->mutex);char failure[sizeof(status)];memcpy(failure,status,sizeof(failure));stop("Socket stream transfer failed",failure);}
    size_t amount=0;int code=0;ssize_t count;
    if(s->tls){code=output ? i2a_ssl_write(s->tls,bytes,size,&amount) : i2a_ssl_read(s->tls,bytes,size,&amount);count=(ssize_t)amount;}
    else{do{count=output ? send(s->fd,bytes,size,MSG_NOSIGNAL) : recv(s->fd,bytes,size,0);}while(count<0 && errno==EINTR);
        if(count<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){count=0;code=-9803;}else if(count<0){int error=errno;stream_socket_error(s,error,0);active_guest_call_boundary=call.previous;pthread_mutex_unlock(&s->mutex);return -1;}else if(!count && !output)code=-9805;}
    if(code && code!=-9803 && (code!=-9805 || output)){stream_socket_error(s,code,1);active_guest_call_boundary=call.previous;pthread_mutex_unlock(&s->mutex);return -1;}
    if(output){s->write_ready=code!=-9803;s->tls_writer_waiting=s->tls && code==-9803;if(code==-9803)stream_socket_watch(s,s->interests|(s->tls && s->tls->waiting ? s->tls->waiting : ALOOPER_EVENT_OUTPUT));}
    else{s->read_ready=0;if(code==-9805){state->status=5;stream_event(state,16);stream_socket_watch(s,s->interests&~ALOOPER_EVENT_INPUT);}else stream_socket_watch(s,s->interests|(s->tls && s->tls->waiting ? s->tls->waiting : ALOOPER_EVENT_INPUT));}
    state->position+=(size_t)count;active_guest_call_boundary=call.previous;pthread_mutex_unlock(&s->mutex);return count;
}
static Word stream_init_file(Word object,const char *selector,Word filename,int append) {
    struct StreamState *state=stream_state(object);
    state->source=filename; state->append=append;
    return object;
}
static Word input_init_file(Word object,const char *selector,Word filename) { return stream_init_file(object,selector,filename,0); }
static Word input_file(Word cls,const char *selector,Word filename) { return input_init_file(host_allocate(cls),selector,filename); }
static Word output_file(Word cls,const char *selector,Word filename,unsigned char append) { return stream_init_file(host_allocate(cls),selector,filename,append); }
static Word input_url(Word cls,const char *selector,struct HostObject *url) { return url && url->content[1] ? input_file(cls,selector,url_path(url)) : 0; }
static Word output_url(Word cls,const char *selector,struct HostObject *url,unsigned char append) { return url && url->content[1] ? output_file(cls,selector,url_path(url),append) : 0; }
static Word stream_init_data(Word object,const char *selector,Word data) {
    if (!data) return 0;
    struct StreamState *state=stream_state(object);
    state->memory=data; state->length=read_word(data+8);
    return object;
}
static Word input_data(Word cls,const char *selector,Word data) { return stream_init_data(host_allocate(cls),selector,data); }
static Word stream_init_memory(Word object) {
    struct StreamState *state=stream_state(object);
    state->output=1;
    state->memory=data_with_bytes(external_class("_OBJC_CLASS_$_NSMutableData"),"Memory stream",NULL,0);
    return object;
}
static Word output_memory(Word cls) { return stream_init_memory(host_allocate(cls)); }
static void stream_open(Word object) {
    struct StreamState *state=stream_state(object);
    if (state->status!=0) return;
    if(state->socket){stream_socket_open(state);return;}
    state->status=1;
    if (state->source) {
        const char *logical=string_bytes(state->source,"NSStream open");
        if (state->output && !writable_sandbox_path(logical)) { stream_failure(state,EACCES); return; }
        state->fd=io_open(logical,state->output ? O_WRONLY|O_CREAT|O_CLOEXEC|(state->append ? O_APPEND : O_TRUNC) : O_RDONLY|O_CLOEXEC,0600);
        int code=errno;
        if (state->fd<0) { stream_failure(state,code); return; }
        struct stat info;
        if (!io_fstat(state->fd,&info) && info.st_size>=0) state->length=(size_t)info.st_size;
    } else if (!state->memory) { stream_failure(state,EINVAL); return; }
    state->status=2;
    stream_event(state,1|(state->output ? 4 : state->length ? 2 : 16));
}
static void stream_close(Word object) {
    struct StreamState *state=stream_state(object);
    if(state->socket){stream_socket_close(state);return;}
    if (state->fd>=0) { io_close(state->fd); state->fd=-1; }
    state->status=6;
}
static intptr_t stream_read(Word object,const char *selector,void *buffer,size_t capacity) {
    struct StreamState *state=stream_state(object);
    if (state->output || (state->status!=2 && state->status!=5)) { stream_failure(state,EBADF); return -1; }
    if (!capacity || state->status==5) return 0;
    if(!buffer || capacity>INTPTR_MAX){stream_failure(state,EINVAL);return -1;}
    if(state->socket)return stream_socket_transfer(state,buffer,capacity,0);
    ssize_t count;
    if (state->memory) {
        size_t size=read_word(state->memory+8);
        if (state->position>size) { stream_failure(state,EIO); return -1; }
        size_t amount=size-state->position;
        if (amount>capacity) amount=capacity;
        if (amount) memcpy(buffer,(void *)(data_buffer((void *)state->memory)+state->position),amount);
        count=(ssize_t)amount;
    } else { do { count=io_read(state->fd,buffer,capacity); } while (count<0 && errno==EINTR); }
    if (count<0) { stream_failure(state,errno); return -1; }
    state->position+=(size_t)count;
    if (!count) { state->status=5; stream_event(state,16); }
    else stream_event(state,2);
    return count;
}
static intptr_t stream_write(Word object,const char *selector,const void *bytes,size_t size) {
    struct StreamState *state=stream_state(object);
    if (!state->output || state->status!=2) { stream_failure(state,EBADF); return -1; }
    if((!bytes && size) || size>INTPTR_MAX){stream_failure(state,EINVAL);return -1;}
    if(state->socket)return stream_socket_transfer(state,(void *)bytes,size,1);
    ssize_t count;
    if (state->memory) { data_append_bytes((void *)state->memory,selector,bytes,size); count=(ssize_t)size; }
    else { do { count=write(state->fd,bytes,size); } while (count<0 && errno==EINTR); }
    if (count<0) { stream_failure(state,errno); return -1; }
    state->position+=(size_t)count;
    stream_event(state,4);
    return count;
}
static Word stream_status(Word object) { return (Word)stream_state(object)->status; }
static Word stream_error(Word object) { return stream_state(object)->error; }
static Word stream_has_bytes(Word object) {
    struct StreamState *state=stream_state(object);
    if(state->socket){struct StreamSocket *s=state->socket;pthread_mutex_lock(&s->mutex);int ready=!state->output && state->status==2 && s->phase==STREAM_READY && (s->read_ready || (s->tls && i2a_tls_api.SSL_pending(s->tls->ssl)>0));pthread_mutex_unlock(&s->mutex);return ready;}
    return !state->output && state->status==2 && state->position<state->length;
}
static Word stream_has_space(Word object) { struct StreamState *state=stream_state(object);if(state->socket){struct StreamSocket *s=state->socket;pthread_mutex_lock(&s->mutex);int ready=state->output && state->status==2 && s->phase==STREAM_READY && s->write_ready;pthread_mutex_unlock(&s->mutex);return ready;}return state->output && state->status==2; }
static void stream_set_delegate(Word object,const char *selector,Word delegate) { struct StreamState *state=stream_state(object);state->delegate=delegate;state->delegate_method=delegate ? find_implementation(delegate,"stream:handleEvent:") : NULL;state->delegate_epoch=method_cache_epoch; }
static Word stream_delegate(Word object) { return stream_state(object)->delegate; }
static Word stream_property(Word object,const char *selector,Word key) {
    struct StreamState *state=stream_state(object);
    const char *name=string_bytes(key,selector);
    if(state->socket){struct StreamSocket *s=state->socket;pthread_mutex_lock(&s->mutex);Word value=0;
        if(!strcmp(name,"kCFStreamPropertySSLPeerTrust") && s->tls && s->tls->trust)value=(Word)s->tls->trust;
        else if(!strcmp(name,"kCFStreamPropertySocketNativeHandle") && s->fd>=0)value=data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),selector,&s->fd,sizeof(s->fd));
        pthread_mutex_unlock(&s->mutex);return value;}
    if (!strcmp(name,"NSStreamDataWrittenToMemoryStreamKey") && state->output && state->memory) return data_copy(state->memory,"copy");
    if (!strcmp(name,"NSStreamFileCurrentOffsetKey")) return number_integer(state->position);
    return 0;
}
static Word stream_set_property(Word object,const char *selector,Word value,Word key) {
    struct StreamState *state=stream_state(object);
    const char *name=string_bytes(key,selector);
    if(state->socket){struct StreamSocket *s=state->socket;int valid=0;
        if(!strcmp(name,"NSStreamSOCKSProxyConfigurationKey")){
            if(s->started)return 0;
            struct GuestMap *map=(void *)value;Word host=value ? map_get(map,selector,copy_string("NSStreamSOCKSProxyHostKey")) : 0,port=value ? map_get(map,selector,copy_string("NSStreamSOCKSProxyPortKey")) : 0;
            Word user=value ? map_get(map,selector,copy_string("NSStreamSOCKSProxyUserKey")) : 0,password=value ? map_get(map,selector,copy_string("NSStreamSOCKSProxyPasswordKey")) : 0;
            intptr_t number=port ? (intptr_t)numeric_integer((void *)port) : 1080;
            if(value && (!host || !*string_bytes(host,selector) || number<1 || number>65535)){stream_socket_error(s,EINVAL,0);return 0;}
            char *proxy=host ? strdup(string_bytes(host,selector)) : NULL,*u=user ? strdup(string_bytes(user,selector)) : NULL,*p=password ? strdup(string_bytes(password,selector)) : NULL;
            if((host && !proxy) || (user && !u) || (password && !p)){free(proxy);free(u);free(p);stream_failure(state,ENOMEM);return 0;}
            pthread_mutex_lock(&s->mutex);free(s->proxy);free(s->user);free(s->password);s->proxy=proxy;s->user=u;s->password=p;s->proxy_port=(unsigned)number;pthread_mutex_unlock(&s->mutex);return 1;
        }
        if(!strcmp(name,"kCFStreamPropertySSLSettings")){
            struct GuestMap *map=(void *)value;Word peer=value ? map_get(map,selector,copy_string("kCFStreamSSLPeerName")) : 0,verify=value ? map_get(map,selector,copy_string("kCFStreamSSLValidatesCertificateChain")) : 0;
            if(!peer && value)peer=map_get(map,selector,copy_string("NSStreamSSLPeerName"));
            if(value && map_get(map,selector,copy_string("kCFStreamSSLCertificates")))return 0;
            char *name_copy=peer ? strdup(string_bytes(peer,selector)) : NULL;
            if(peer && !name_copy){stream_failure(state,ENOMEM);return 0;}
            pthread_mutex_lock(&s->mutex);if(s->tls){pthread_mutex_unlock(&s->mutex);free(name_copy);return 0;}
            free(s->peer);s->peer=name_copy;s->encrypted=value!=0;s->defer_verification=verify && !numeric_bool((void *)verify);valid=1;
        }else if(!strcmp(name,"kCFStreamPropertySocketSecurityLevel") || !strcmp(name,"NSStreamSocketSecurityLevelKey")){
            const char *level=value ? string_bytes(value,selector) : "kCFStreamSocketSecurityLevelNone";
            int enabled=strcmp(level,"kCFStreamSocketSecurityLevelNone") && strcmp(level,"NSStreamSocketSecurityLevelNone");
            if(enabled && strcmp(level,"kCFStreamSocketSecurityLevelNegotiatedSSL") && strcmp(level,"NSStreamSocketSecurityLevelNegotiatedSSL"))return 0;
            pthread_mutex_lock(&s->mutex);if(s->tls){pthread_mutex_unlock(&s->mutex);return 0;}s->encrypted=enabled;valid=1;
        }
        if(valid){if(s->phase==STREAM_READY && s->encrypted){s->phase=STREAM_TLS;s->read_ready=s->write_ready=0;}int pump=s->phase==STREAM_TLS;pthread_mutex_unlock(&s->mutex);if(pump)schedule_main_function(stream_socket_pump,s);return 1;}
        return 0;
    }
    if (strcmp(name,"NSStreamFileCurrentOffsetKey")) return 0;
    size_t position=numeric_integer((void *)value);
    if (state->fd>=0 && io_seek(state->fd,(off_t)position,SEEK_SET)<0) return 0;
    if (state->memory && position>read_word(state->memory+8)) return 0;
    state->position=position;
    return 1;
}
static void stream_schedule(Word object,const char *selector,Word runloop,Word mode) {
    if (!runloop || !mode) stop("Missing stream runloop or mode",selector);
    const char *name=string_bytes(mode,selector);if(strcmp(name,"kCFRunLoopDefaultMode") && strcmp(name,"kCFRunLoopCommonModes") && strcmp(name,"NSDefaultRunLoopMode") && strcmp(name,"NSRunLoopCommonModes"))stop("Unsupported stream runloop mode",selector);
    struct StreamState *state=stream_state(object);
    if(state->runloop && state->runloop!=runloop)stop("Stream already belongs to a runloop",selector);
    if(!state->delivery)state->delivery=loop_function_create(runloop,stream_deliver,state,NULL);
    state->runloop=runloop;
    if (state->status==2) stream_event(state,1|(state->output ? 4 : 2));
}
static void stream_unschedule(Word object,const char *selector,Word runloop,Word mode) { struct StreamState *state=stream_state(object);if(state->runloop!=runloop)return;state->runloop=0;Word delivery=state->delivery;state->delivery=0;__atomic_store_n(&state->pending,0,__ATOMIC_RELEASE);loop_function_close(delivery); }
static unsigned char stream_set_client(Word object,Word events,Word callback,const struct StreamClientContext *context) {
    if(context && context->version)return 0;struct StreamState *state=stream_state(object);struct StreamClientContext next={0};if(callback && context)next=*context;
    if(callback && next.retain)next.info=((Word (*)(Word))next.retain)(next.info);
    struct StreamClientContext old=state->client_context;state->client=callback;state->client_mask=events;state->client_context=next;
    if(old.release)((void (*)(Word))old.release)(old.info);return 1;
}
static unsigned char core_stream_open(Word object) { stream_open(object);int state=stream_state(object)->status;return state==1 || state==2 || state==5; }
static intptr_t core_stream_read(Word object,void *bytes,intptr_t size) { return size<0 ? -1 : stream_read(object,"CFReadStreamRead",bytes,(size_t)size); }
static intptr_t core_stream_write(Word object,const void *bytes,intptr_t size) { return size<0 ? -1 : stream_write(object,"CFWriteStreamWrite",bytes,(size_t)size); }
static Word core_stream_copy_property(Word object,Word key) { Word value=stream_property(object,"CFStreamCopyProperty",key);return value ? cf_retain(value) : 0; }
static unsigned char core_stream_set_property(Word object,Word key,Word value) { return stream_set_property(object,"CFStreamSetProperty",value,key)!=0; }
static void core_stream_schedule(Word object,Word loop,Word mode) { stream_schedule(object,"CFStreamScheduleWithRunLoop",loop,mode); }
static void core_stream_unschedule(Word object,Word loop,Word mode) { stream_unschedule(object,"CFStreamUnscheduleFromRunLoop",loop,mode); }
static Word core_stream_copy_error(Word object) { Word error=stream_error(object);return error ? cf_retain(error) : 0; }
struct CoreStreamError { intptr_t domain;int32_t code; };
static struct CoreStreamError core_stream_error(Word object) { Word error=stream_error(object);if(!error)return (struct CoreStreamError){0};const char *domain=string_bytes(error_domain((void *)error),"CFStreamGetError");return (struct CoreStreamError){!strcmp(domain,"NSPOSIXErrorDomain") ? 1 : !strcmp(domain,"NSOSStatusErrorDomain") ? 3 : -1,(int32_t)error_code((void *)error)}; }
static unsigned char stream_get_buffer(Word object,const char *selector,const unsigned char **buffer,size_t *length) {
    if(buffer)*buffer=NULL;if(length)*length=0;struct StreamState *state=stream_state(object);
    if(!buffer || !length || state->output || !state->memory || state->position>read_word(state->memory+8))return 0;
    *length=read_word(state->memory+8)-state->position;*buffer=(const void *)(data_buffer((void *)state->memory)+state->position);return 1;
}
static const unsigned char *core_stream_buffer(Word object,intptr_t maximum,intptr_t *length) { const unsigned char *buffer=NULL;size_t size=0;if(length)*length=0;if(maximum<=0 || !length || !stream_get_buffer(object,"CFReadStreamGetBuffer",&buffer,&size))return NULL;if(size>(size_t)maximum)size=(size_t)maximum;*length=(intptr_t)size;stream_state(object)->position+=size;return buffer; }
static Word core_input_file(Word allocator,Word url) { return allocator ? 0 : input_url(external_class("_OBJC_CLASS_$_NSInputStream"),"CFReadStreamCreateWithFile",(void *)url); }
static Word core_output_file(Word allocator,Word url) { return allocator ? 0 : output_url(external_class("_OBJC_CLASS_$_NSOutputStream"),"CFWriteStreamCreateWithFile",(void *)url,0); }
static Word core_input_bytes(Word allocator,const void *bytes,intptr_t length,Word deallocator) { Word data=cf_data_no_copy(allocator,bytes,length,deallocator);return data ? input_data(external_class("_OBJC_CLASS_$_NSInputStream"),"CFReadStreamCreateWithBytesNoCopy",data) : 0; }
static Word core_input_type(void) { return external_class("_OBJC_CLASS_$_NSInputStream"); }
static Word core_output_type(void) { return external_class("_OBJC_CLASS_$_NSOutputStream"); }
static Word resolve_stream_import(const char *name) {
#define STREAM_API(api,fn) if(!strcmp(name,"_" #api))return (Word)fn
    STREAM_API(CFStreamCreatePairWithSocketToHost,stream_socket_pair);
    STREAM_API(CFReadStreamOpen,core_stream_open);STREAM_API(CFWriteStreamOpen,core_stream_open);
    STREAM_API(CFReadStreamClose,stream_close);STREAM_API(CFWriteStreamClose,stream_close);
    STREAM_API(CFReadStreamRead,core_stream_read);STREAM_API(CFWriteStreamWrite,core_stream_write);
    STREAM_API(CFReadStreamHasBytesAvailable,stream_has_bytes);STREAM_API(CFWriteStreamCanAcceptBytes,stream_has_space);
    STREAM_API(CFReadStreamGetStatus,stream_status);STREAM_API(CFWriteStreamGetStatus,stream_status);
    STREAM_API(CFReadStreamGetError,core_stream_error);STREAM_API(CFWriteStreamGetError,core_stream_error);
    STREAM_API(CFReadStreamCopyError,core_stream_copy_error);STREAM_API(CFWriteStreamCopyError,core_stream_copy_error);
    STREAM_API(CFReadStreamSetClient,stream_set_client);STREAM_API(CFWriteStreamSetClient,stream_set_client);
    STREAM_API(CFReadStreamCopyProperty,core_stream_copy_property);STREAM_API(CFWriteStreamCopyProperty,core_stream_copy_property);
    STREAM_API(CFReadStreamSetProperty,core_stream_set_property);STREAM_API(CFWriteStreamSetProperty,core_stream_set_property);
    STREAM_API(CFReadStreamScheduleWithRunLoop,core_stream_schedule);STREAM_API(CFWriteStreamScheduleWithRunLoop,core_stream_schedule);
    STREAM_API(CFReadStreamUnscheduleFromRunLoop,core_stream_unschedule);STREAM_API(CFWriteStreamUnscheduleFromRunLoop,core_stream_unschedule);
    STREAM_API(CFReadStreamGetBuffer,core_stream_buffer);STREAM_API(CFReadStreamCreateWithFile,core_input_file);STREAM_API(CFWriteStreamCreateWithFile,core_output_file);
    STREAM_API(CFReadStreamCreateWithBytesNoCopy,core_input_bytes);STREAM_API(CFReadStreamGetTypeID,core_input_type);STREAM_API(CFWriteStreamGetTypeID,core_output_type);
#undef STREAM_API
    return 0;
}
static Method streams_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSRunLoop")) {
        if (class_receiver && !strcmp(selector,"mainRunLoop")) return (Method)main_runloop;
        if (class_receiver && !strcmp(selector,"currentRunLoop")) return (Method)current_runloop;
        if (!class_receiver && !strcmp(selector,"getCFRunLoop")) return (Method)identity;
    }
    int input=!strcmp(receiver,"_OBJC_CLASS_$_NSInputStream"),output=!strcmp(receiver,"_OBJC_CLASS_$_NSOutputStream");
    if (!input && !output) return NULL;
    if (class_receiver) {
        if (input && !strcmp(selector,"inputStreamWithFileAtPath:")) return (Method)input_file;
        if (input && !strcmp(selector,"inputStreamWithURL:")) return (Method)input_url;
        if (input && !strcmp(selector,"inputStreamWithData:")) return (Method)input_data;
        if (output && !strcmp(selector,"outputStreamToMemory")) return (Method)output_memory;
        if (output && !strcmp(selector,"outputStreamToFileAtPath:append:")) return (Method)output_file;
        if (output && !strcmp(selector,"outputStreamWithURL:append:")) return (Method)output_url;
    } else {
        if (!strcmp(selector,"open")) return (Method)stream_open;
        if (!strcmp(selector,"close")) return (Method)stream_close;
        if (!strcmp(selector,"streamStatus")) return (Method)stream_status;
        if (!strcmp(selector,"streamError")) return (Method)stream_error;
        if (!strcmp(selector,"setDelegate:")) return (Method)stream_set_delegate;
        if (!strcmp(selector,"delegate")) return (Method)stream_delegate;
        if (!strcmp(selector,"propertyForKey:")) return (Method)stream_property;
        if (!strcmp(selector,"setProperty:forKey:")) return (Method)stream_set_property;
        if (!strcmp(selector,"scheduleInRunLoop:forMode:")) return (Method)stream_schedule;
        if (!strcmp(selector,"removeFromRunLoop:forMode:")) return (Method)stream_unschedule;
        if (input) {
            if (!strcmp(selector,"initWithFileAtPath:")) return (Method)input_init_file;
            if (!strcmp(selector,"initWithData:")) return (Method)stream_init_data;
            if (!strcmp(selector,"read:maxLength:")) return (Method)stream_read;
            if (!strcmp(selector,"hasBytesAvailable")) return (Method)stream_has_bytes;
            if (!strcmp(selector,"getBuffer:length:")) return (Method)stream_get_buffer;
        }
        if (output) {
            if (!strcmp(selector,"initToMemory")) return (Method)stream_init_memory;
            if (!strcmp(selector,"initToFileAtPath:append:")) return (Method)stream_init_file;
            if (!strcmp(selector,"write:maxLength:")) return (Method)stream_write;
            if (!strcmp(selector,"hasSpaceAvailable")) return (Method)stream_has_space;
        }
    }
    return NULL;
}
