 
static void schedule_main_function(void (*function)(void *),void *context);
static struct HostObject main_run_loop;
static _Thread_local struct HostObject thread_run_loop;
static Word main_runloop(Word cls) { main_run_loop.isa=cls; return (Word)&main_run_loop; }
static Word current_runloop(Word cls) {
    if (gettid()==getpid()) return main_runloop(cls);
    thread_run_loop.isa=cls; return (Word)&thread_run_loop;
}
static Word core_main_runloop(void) { return main_runloop(external_class("_OBJC_CLASS_$_NSRunLoop")); }
static Word core_current_runloop(void) { return current_runloop(external_class("_OBJC_CLASS_$_NSRunLoop")); }
struct StreamState {
    Word object,source,memory,error,delegate,runloop;
    size_t position,length;
    int output,append,fd,status;
    unsigned pending;
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
    if (!state->delegate || !state->runloop) return;
    Method method=find_implementation(state->delegate,"stream:handleEvent:");
    if (!method) return;
    for (unsigned event=1;event<=16;event<<=1) {
        if (!(events&event) || state->status==6) continue;
        if ((event==2 || event==4) && state->status!=2) continue;
        method(state->delegate,"stream:handleEvent:",state->object,event,0,0);
    }
}
static void stream_event(struct StreamState *state,unsigned events) {
    if (!state->runloop || !state->delegate) return;
    if (!__atomic_fetch_or(&state->pending,events,__ATOMIC_ACQ_REL)) schedule_main_function(stream_deliver,state);
}
static void stream_failure(struct StreamState *state,int error) {
    file_failure(&state->error,state->source ? string_bytes(state->source,"NSStream") : "Memory stream",error);
    state->status=7; stream_event(state,8);
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
    state->status=1;
    if (state->source) {
        const char *logical=string_bytes(state->source,"NSStream open");
        char *path=native_path(logical);
        if (!path) { stream_failure(state,errno); return; }
        if (state->output && !writable_sandbox_path(path)) { free(path); stream_failure(state,EACCES); return; }
        state->fd=open(path,state->output ? O_WRONLY|O_CREAT|O_CLOEXEC|(state->append ? O_APPEND : O_TRUNC) : O_RDONLY|O_CLOEXEC,0600);
        int code=errno;
        free(path);
        if (state->fd<0) { stream_failure(state,code); return; }
        struct stat info;
        if (!fstat(state->fd,&info) && info.st_size>=0) state->length=(size_t)info.st_size;
    } else if (!state->memory) { stream_failure(state,EINVAL); return; }
    state->status=2;
    stream_event(state,1|(state->output ? 4 : state->length ? 2 : 16));
}
static void stream_close(Word object) {
    struct StreamState *state=stream_state(object);
    if (state->fd>=0) { close(state->fd); state->fd=-1; }
    state->status=6;
}
static intptr_t stream_read(Word object,const char *selector,void *buffer,size_t capacity) {
    struct StreamState *state=stream_state(object);
    if (state->output || (state->status!=2 && state->status!=5)) { stream_failure(state,EBADF); return -1; }
    if (!capacity || state->status==5) return 0;
    ssize_t count;
    if (state->memory) {
        size_t size=read_word(state->memory+8);
        if (state->position>size) { stream_failure(state,EIO); return -1; }
        size_t amount=size-state->position;
        if (amount>capacity) amount=capacity;
        if (amount) memcpy(buffer,(void *)(data_buffer((void *)state->memory)+state->position),amount);
        count=(ssize_t)amount;
    } else { do { count=read(state->fd,buffer,capacity); } while (count<0 && errno==EINTR); }
    if (count<0) { stream_failure(state,errno); return -1; }
    state->position+=(size_t)count;
    if (!count) { state->status=5; stream_event(state,16); }
    else stream_event(state,2);
    return count;
}
static intptr_t stream_write(Word object,const char *selector,const void *bytes,size_t size) {
    struct StreamState *state=stream_state(object);
    if (!state->output || state->status!=2) { stream_failure(state,EBADF); return -1; }
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
    return !state->output && state->status==2 && state->position<state->length;
}
static Word stream_has_space(Word object) { struct StreamState *state=stream_state(object); return state->output && state->status==2; }
static void stream_set_delegate(Word object,const char *selector,Word delegate) { stream_state(object)->delegate=delegate; }
static Word stream_delegate(Word object) { return stream_state(object)->delegate; }
static Word stream_property(Word object,const char *selector,Word key) {
    struct StreamState *state=stream_state(object);
    const char *name=string_bytes(key,selector);
    if (!strcmp(name,"NSStreamDataWrittenToMemoryStreamKey") && state->output && state->memory) return data_copy(state->memory,"copy");
    if (!strcmp(name,"NSStreamFileCurrentOffsetKey")) return number_integer(state->position);
    return 0;
}
static Word stream_set_property(Word object,const char *selector,Word value,Word key) {
    struct StreamState *state=stream_state(object);
    if (strcmp(string_bytes(key,selector),"NSStreamFileCurrentOffsetKey")) return 0;
    size_t position=numeric_integer((void *)value);
    if (state->fd>=0 && lseek(state->fd,(off_t)position,SEEK_SET)<0) return 0;
    if (state->memory && position>read_word(state->memory+8)) return 0;
    state->position=position;
    return 1;
}
static void stream_schedule(Word object,const char *selector,Word runloop,Word mode) {
    if (runloop!=(Word)&main_run_loop) stop("Stream scheduling requires the main runloop",selector);
    struct StreamState *state=stream_state(object);
    state->runloop=runloop;
    if (state->status==2) stream_event(state,1|(state->output ? 4 : 2));
}
static void stream_unschedule(Word object,const char *selector,Word runloop,Word mode) { stream_state(object)->runloop=0; }
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
