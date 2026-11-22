 
struct SessionTask {
    struct HostObject object;
    struct Session *session;
    Word connection,completion,data,error,description,identifier,creation_error;
    int state,suspensions,kind,file;
    float priority;
    char *path;
    int64_t received,sent,expected_send,expected_receive;
    struct SessionTask *next;
};
struct SessionRecord { struct Session *session; Word tail; int notified; struct SessionRecord *next; };
struct SessionReply { struct SessionTask *task; Word challenge; int answered; };
struct SessionReplyBlock { struct Block_layout layout; struct SessionReply *reply; };
struct SessionEvent { struct Session *session; struct SessionTask *task; int kind; Word first,second; int64_t count,total,expected; struct SessionReply *reply; };
struct SessionEventBlock { struct Block_layout layout; struct SessionEvent *event; };
static pthread_mutex_t session_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct SessionTask *session_tasks;
static struct SessionRecord *session_records;
static Word next_task_identifier=1;
static Word session_ensure_queue(struct Session *session) {
    pthread_mutex_lock(&session_mutex);
    if (!session->queue) {
        session->queue=host_allocate(external_class("_OBJC_CLASS_$_NSOperationQueue"));
        queue_set_maximum(session->queue,"URLSession delegate queue",1);
    }
    Word queue=session->queue; pthread_mutex_unlock(&session_mutex); return queue;
}
static Word session_basic(Word cls,const char *selector,Word configuration) { return session_create(cls,selector,configuration,0,0); }
static Word session_description(struct Session *session) { return session->description; }
static void session_set_description(struct Session *session,const char *selector,Word description) { session->description=description ? text_copy(description,"copy") : 0; }
static Word session_shared(Word cls,const char *selector) {
    static Word shared;
    pthread_mutex_lock(&session_mutex);
    if (!shared) shared=session_basic(cls,selector,0);
    pthread_mutex_unlock(&session_mutex); return shared;
}
static Word session_error(int code,const char *description) {
    return unsupported_service_error("NSURLErrorDomain",code,description);
}
static void session_emit(struct SessionEvent *event);
static void session_check_invalidated(struct Session *session) {
    pthread_mutex_lock(&session_mutex);
    if (!session->invalidated) { pthread_mutex_unlock(&session_mutex); return; }
    for (struct SessionTask *task=session_tasks;task;task=task->next)
        if (task->session==session && task->state!=3) { pthread_mutex_unlock(&session_mutex); return; }
    struct SessionRecord *record=session_records;
    while (record && record->session!=session) record=record->next;
    if (!record) { record=calloc(1,sizeof(*record)); if (!record) { pthread_mutex_unlock(&session_mutex); stop("Allocation failed","NSURLSession"); } record->session=session; record->next=session_records; session_records=record; }
    int notify=!record->notified; record->notified=1;
    pthread_mutex_unlock(&session_mutex);
    if (notify) {
        struct SessionEvent *event=calloc(1,sizeof(*event)); if (!event) stop("Allocation failed","URLSession invalidation");
        event->session=session; event->kind=6; session_emit(event);
    }
}
static void session_finish(struct SessionTask *task,Word error) {
    pthread_mutex_lock(&session_mutex);
    if (task->state==3) { pthread_mutex_unlock(&session_mutex); return; }
    task->state=3; task->error=error;
    if (task->file>=0) { close(task->file); task->file=-1; }
    pthread_mutex_unlock(&session_mutex);
    struct SessionEvent *event=calloc(1,sizeof(*event)); if (!event) stop("Allocation failed","URLSession completion");
    event->session=task->session; event->task=task; event->kind=4; event->first=error; session_emit(event);
    session_check_invalidated(task->session);
}
static void session_cancel(struct SessionTask *task) {
    pthread_mutex_lock(&session_mutex);
    if (task->state==3) { pthread_mutex_unlock(&session_mutex); return; }
    task->state=2; pthread_mutex_unlock(&session_mutex);
    if (task->connection) connection_cancel(task->connection);
    session_finish(task,session_error(-999,"The URLSession task was cancelled"));
}
static void session_resolve_decision(struct SessionTask *task,int accepted) {
    JNIEnv *env=attached_environment("URLSession response decision"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID resolve=(*env)->GetMethodID(env,type,"resolveConnectionDecision","(JI)V");
    (*env)->CallVoidMethod(env,activity,resolve,(jlong)connection_state(task->connection),(jint)accepted); (*env)->DeleteLocalRef(env,type);
}
static void session_response_reply(struct SessionReplyBlock *block,Word disposition) {
    struct SessionReply *reply=block->reply;
    if (__atomic_exchange_n(&reply->answered,1,__ATOMIC_ACQ_REL)) return;
    if (disposition==1) session_resolve_decision(reply->task,1);
    else { session_resolve_decision(reply->task,0); session_cancel(reply->task); }
}
static void session_redirect_reply(struct SessionReplyBlock *block,Word request) {
    struct SessionReply *reply=block->reply;
    if (__atomic_exchange_n(&reply->answered,1,__ATOMIC_ACQ_REL)) return;
    if (!request) { session_resolve_decision(reply->task,0); session_cancel(reply->task); return; }
    struct ConnectionState *connection=connection_state(reply->task->connection);
    connection->request=request_copy((void *)request,"copy"); connection->body_offset=0; connection->body_open=0;
    session_resolve_decision(reply->task,1);
}
static void session_auth_reply(struct SessionReplyBlock *block,Word disposition,Word credential) {
    struct SessionReply *reply=block->reply;
    if (__atomic_exchange_n(&reply->answered,1,__ATOMIC_ACQ_REL)) return;
    struct HttpChallenge *challenge=(void *)reply->challenge;
    if (!disposition) challenge_use((Word)challenge,"useCredential:forAuthenticationChallenge:",(void *)credential,challenge);
    else authentication_answer(challenge,disposition==1 ? 1 : 2);
}
static void session_event_invoke(struct SessionEventBlock *block) {
    struct SessionEvent *event=block->event;
    struct Session *session=event->session;
    struct SessionTask *task=event->task;
    const char *selector=NULL; Method method=NULL;
    static struct Block_descriptor_1 reply_descriptor={0,sizeof(struct SessionReplyBlock)};
    struct SessionReplyBlock reply={.layout={.isa=_NSConcreteStackBlock,.flags=0,.reserved=0,.descriptor=&reply_descriptor},.reply=event->reply};
    if (event->kind==0) {
        selector="URLSession:dataTask:didReceiveResponse:completionHandler:"; method=find_implementation(session->delegate,selector);
        reply.layout.invoke=(void (*)(void *,...))session_response_reply;
        if (method) method(session->delegate,selector,(Word)session,(Word)task,event->first,(Word)&reply);
        else session_response_reply(&reply,1);
    } else if (event->kind==1) {
        selector="URLSession:dataTask:didReceiveData:"; method=find_implementation(session->delegate,selector);
        if (method) method(session->delegate,selector,(Word)session,(Word)task,event->first,0);
    } else if (event->kind==2) {
        selector="URLSession:task:didSendBodyData:totalBytesSent:totalBytesExpectedToSend:"; method=find_implementation(session->delegate,selector);
        if (method) ((void (*)(Word,const char *,Word,Word,int64_t,int64_t,int64_t))method)(session->delegate,selector,(Word)session,(Word)task,event->count,event->total,event->expected);
    } else if (event->kind==3) {
        selector="URLSession:task:willPerformHTTPRedirection:newRequest:completionHandler:"; method=find_implementation(session->delegate,selector);
        reply.layout.invoke=(void (*)(void *,...))session_redirect_reply;
        if (method) ((void (*)(Word,const char *,Word,Word,Word,Word,Word))method)(session->delegate,selector,(Word)session,(Word)task,event->second,event->first,(Word)&reply);
        else session_redirect_reply(&reply,event->first);
    } else if (event->kind==4) {
        Word response=task->connection ? connection_state(task->connection)->response : 0;
        Word result=task->kind==2 ? (!event->first && task->path ? file_url(copy_string(task->path)) : 0) : task->data;
        if (task->completion) ((void (*)(Word,Word,Word,Word))read_word(task->completion+16))(task->completion,result,response,event->first);
        else {
            if (task->kind==2 && result) {
                selector="URLSession:downloadTask:didFinishDownloadingToURL:"; method=find_implementation(session->delegate,selector);
                if (method) method(session->delegate,selector,(Word)session,(Word)task,result,0);
            }
            selector="URLSession:task:didCompleteWithError:"; method=find_implementation(session->delegate,selector);
            if (method) method(session->delegate,selector,(Word)session,(Word)task,event->first,0);
        }
        if (task->path) unlink(task->path);
    } else if (event->kind==5) {
        selector="URLSession:task:didReceiveChallenge:completionHandler:"; method=find_implementation(session->delegate,selector);
        reply.layout.invoke=(void (*)(void *,...))session_auth_reply;
        if (method) method(session->delegate,selector,(Word)session,(Word)task,event->first,(Word)&reply);
        else {
            selector="URLSession:didReceiveChallenge:completionHandler:"; method=find_implementation(session->delegate,selector);
            if (method) method(session->delegate,selector,(Word)session,event->first,(Word)&reply,0);
            else session_auth_reply(&reply,1,0);
        }
    } else if (event->kind==6) {
        selector="URLSession:didBecomeInvalidWithError:"; method=find_implementation(session->delegate,selector);
        if (method) method(session->delegate,selector,(Word)session,0,0,0);
    } else if (event->kind==7) {
        selector="URLSession:downloadTask:didWriteData:totalBytesWritten:totalBytesExpectedToWrite:"; method=find_implementation(session->delegate,selector);
        if (method) ((void (*)(Word,const char *,Word,Word,int64_t,int64_t,int64_t))method)(session->delegate,selector,(Word)session,(Word)task,event->count,event->total,event->expected);
    } else if (event->kind==8 || event->kind==9) {
        Word arrays[3]={sequence_empty(external_class("_OBJC_CLASS_$_NSArray")),sequence_empty(external_class("_OBJC_CLASS_$_NSArray")),sequence_empty(external_class("_OBJC_CLASS_$_NSArray"))};
        pthread_mutex_lock(&session_mutex);
        for (struct SessionTask *item=session_tasks;item;item=item->next) if (item->session==session && item->state!=3)
            sequence_add((void *)arrays[event->kind==9 ? 0 : item->kind],"URLSession task enumeration",(Word)item);
        pthread_mutex_unlock(&session_mutex);
        if (event->kind==9) ((void (*)(Word,Word))read_word(event->first+16))(event->first,arrays[0]);
        else ((void (*)(Word,Word,Word,Word))read_word(event->first+16))(event->first,arrays[0],arrays[1],arrays[2]);
        _Block_release((void *)event->first);
    }
    free(event);
}
static void session_emit(struct SessionEvent *event) {
    static struct Block_descriptor_1 descriptor={0,sizeof(struct SessionEventBlock)};
    struct SessionEventBlock block={.layout={.isa=_NSConcreteStackBlock,.flags=0,.reserved=0,.invoke=(void (*)(void *,...))session_event_invoke,.descriptor=&descriptor},.event=event};
    Word queue=session_ensure_queue(event->session);
    Word operation=block_operation(external_class("_OBJC_CLASS_$_NSBlockOperation"),"URLSession callback",(Word)&block);
    pthread_mutex_lock(&session_mutex);
    struct SessionRecord *record=session_records;
    while (record && record->session!=event->session) record=record->next;
    if (!record) { record=calloc(1,sizeof(*record)); if (!record) { pthread_mutex_unlock(&session_mutex); stop("Allocation failed","NSURLSession"); } record->session=event->session; record->next=session_records; session_records=record; }
    if (record->tail) operation_add_dependency(operation,"URLSession ordering",record->tail);
    record->tail=operation;
    pthread_mutex_unlock(&session_mutex);
    queue_add(queue,"URLSession callback",operation);
}
static struct SessionEvent *session_event(struct SessionTask *task,int kind,Word first,Word second,int reply) {
    struct SessionEvent *event=calloc(1,sizeof(*event)); if (!event) stop("Allocation failed","NSURLSession event");
    event->session=task->session; event->task=task; event->kind=kind; event->first=first; event->second=second;
    if (reply) { event->reply=calloc(1,sizeof(*event->reply)); if (!event->reply) stop("Allocation failed","URLSession reply"); event->reply->task=task; }
    return event;
}
static int session_http_redirect(Word object,Word request,Word response) {
    struct SessionTask *task=(void *)object;
    if (!find_implementation(task->session->delegate,"URLSession:task:willPerformHTTPRedirection:newRequest:completionHandler:")) {
        struct ConnectionState *connection=connection_state(task->connection);
        connection->request=request_copy((void *)request,"copy"); connection->body_offset=0; connection->body_open=0; return 1;
    }
    session_emit(session_event(task,3,request,response,1)); return 2;
}
static int session_http_response(Word object,Word response) {
    struct SessionTask *task=(void *)object; task->expected_receive=((struct HttpResponse *)response)->length;
    if (task->completion || task->kind==2 || !find_implementation(task->session->delegate,"URLSession:dataTask:didReceiveResponse:completionHandler:")) return 1;
    session_emit(session_event(task,0,response,0,1)); return 2;
}
static struct SessionTask *transport_task(struct HostObject *delegate) { return (void *)delegate->content[0]; }
static void session_received_data(struct HostObject *delegate,const char *selector,Word connection,struct HostObject *data) {
    struct SessionTask *task=transport_task(delegate); task->received+=(int64_t)data->content[0];
    if (task->kind==2) {
        size_t done=0;
        while (done<data->content[0]) {
            ssize_t count=write(task->file,(char *)data_buffer(data)+done,data->content[0]-done);
            if (count<0 && errno==EINTR) continue;
            if (count<=0) { connection_cancel(task->connection); session_finish(task,session_error(-3003,"Writing the downloaded file failed")); return; }
            done+=(size_t)count;
        }
        struct SessionEvent *event=session_event(task,7,0,0,0); event->count=data->content[0]; event->total=task->received; event->expected=task->expected_receive; session_emit(event);
    } else if (task->completion) {
        if (!task->data) task->data=data_allocate(external_class("_OBJC_CLASS_$_NSMutableData"),"dataWithCapacity:",data->content[0]);
        data_append((void *)task->data,selector,data);
    } else session_emit(session_event(task,1,(Word)data,0,0));
}
static void session_sent_body(struct HostObject *delegate,const char *selector,Word connection,int64_t count,int64_t total,int64_t expected) {
    struct SessionTask *task=transport_task(delegate); task->sent=total; task->expected_send=expected;
    struct SessionEvent *event=session_event(task,2,0,0,0); event->count=count; event->total=total; event->expected=expected; session_emit(event);
}
static void session_connection_failed(struct HostObject *delegate,const char *selector,Word connection,Word error) { session_finish(transport_task(delegate),error); }
static void session_connection_complete(struct HostObject *delegate,const char *selector,Word connection) {
    struct SessionTask *task=transport_task(delegate);
    if (task->completion && task->kind!=2 && !task->data) task->data=data_empty(external_class("_OBJC_CLASS_$_NSData"));
    session_finish(task,0);
}
static void session_connection_auth(struct HostObject *delegate,const char *selector,Word connection,Word challenge) {
    struct SessionTask *task=transport_task(delegate);
    struct SessionEvent *event=session_event(task,5,challenge,0,1); event->reply->challenge=challenge; session_emit(event);
}
static Word session_task_create(struct Session *session,Word request,Word completion,int kind) {
    if (session->invalidated) stop("Session is invalidated","NSURLSession task construction");
    struct SessionTask *task=calloc(1,sizeof(*task)); if (!task) stop("Allocation failed","NSURLSessionTask");
    task->object.isa=external_class(kind==2 ? "_OBJC_CLASS_$_NSURLSessionDownloadTask" : kind==1 ? "_OBJC_CLASS_$_NSURLSessionUploadTask" : "_OBJC_CLASS_$_NSURLSessionDataTask");
    task->session=session; task->state=1; task->suspensions=1; task->kind=kind; task->file=-1; task->priority=0.5f;
    task->expected_receive=task->expected_send=-1; task->identifier=__atomic_fetch_add(&next_task_identifier,1,__ATOMIC_RELAXED);
    task->completion=completion ? (Word)_Block_copy((void *)completion) : 0;
    if (configuration_values((void *)session->configuration)->background_identifier)
        task->creation_error=unsupported_service_error("I2ACompatibilityErrorDomain",1,"Background transfer persistence across process termination is not implemented");
    if (request) {
        struct Request *copy=(void *)request_copy((void *)request,"copy");
        struct SessionConfig *config=configuration_values((void *)session->configuration);
        if (copy->timeout==60) copy->timeout=config->request_timeout;
        if (!copy->policy) copy->policy=config->policy;
        struct GuestMap *headers=(void *)config->headers;
        for (size_t i=0;headers && i<headers->count;++i) if (!request_header_value(copy,"Session headers",headers->entries[i].key)) request_header(copy,"setValue:forHTTPHeaderField:",headers->entries[i].value,headers->entries[i].key);
        struct HostObject *delegate=(void *)host_allocate(external_class("_OBJC_CLASS_$_I2ASessionTransport")); delegate->content[0]=(Word)task;
        task->connection=connection_init(host_allocate(external_class("_OBJC_CLASS_$_NSURLConnection")),"Session connection",(Word)copy,(Word)delegate,0);
        struct ConnectionState *state=connection_state(task->connection); state->runloop=core_main_runloop(); state->session_task=(Word)task;
    }
    pthread_mutex_lock(&session_mutex); task->next=session_tasks; session_tasks=task; pthread_mutex_unlock(&session_mutex);
    return (Word)task;
}
static Word session_data_request(struct Session *session,const char *selector,Word request,Word completion) { return session_task_create(session,request,completion,0); }
static Word session_data_request_basic(struct Session *session,const char *selector,Word request) { return session_data_request(session,selector,request,0); }
static Word session_data_url(struct Session *session,const char *selector,Word url,Word completion) { return session_data_request(session,selector,request_basic(external_class("_OBJC_CLASS_$_NSURLRequest"),selector,url),completion); }
static Word session_data_url_basic(struct Session *session,const char *selector,Word url) { return session_data_url(session,selector,url,0); }
static Word session_download_request(struct Session *session,const char *selector,Word request,Word completion) { return session_task_create(session,request,completion,2); }
static Word session_download_request_basic(struct Session *session,const char *selector,Word request) { return session_download_request(session,selector,request,0); }
static Word session_download_url(struct Session *session,const char *selector,Word url,Word completion) { return session_download_request(session,selector,request_basic(external_class("_OBJC_CLASS_$_NSURLRequest"),selector,url),completion); }
static Word session_download_url_basic(struct Session *session,const char *selector,Word url) { return session_download_url(session,selector,url,0); }
static Word session_download_resume(struct Session *session,const char *selector,Word data) {
    struct SessionTask *task=(void *)session_task_create(session,0,0,2);
    task->creation_error=unsupported_service_error("I2ACompatibilityErrorDomain",1,"This iOS download-resume format is not implemented"); return (Word)task;
}
static Word session_upload(struct Session *session,const char *selector,Word request,Word source,Word completion) {
    struct Request *copy=(void *)request_copy((void *)request,"copy");
    int file=strstr(selector,"fromFile:")!=NULL;
    Word data=file ? data_url(external_class("_OBJC_CLASS_$_NSData"),selector,(void *)source) : source;
    copy->body=data; copy->body_stream=0;
    struct SessionTask *task=(void *)session_task_create(session,(Word)copy,completion,1);
    if (file && !data) task->creation_error=session_error(-1100,"The upload file does not exist");
    return (Word)task;
}
static Word session_upload_basic(struct Session *session,const char *selector,Word request,Word source) { return session_upload(session,selector,request,source,0); }
static void session_task_resume(struct SessionTask *task) {
    pthread_mutex_lock(&session_mutex);
    if (task->state>=2 || !task->suspensions) { pthread_mutex_unlock(&session_mutex); return; }
    --task->suspensions; if (task->suspensions) { pthread_mutex_unlock(&session_mutex); return; }
    task->state=0; pthread_mutex_unlock(&session_mutex);
    if (task->creation_error) { session_finish(task,task->creation_error); return; }
    struct SessionConfig *config=configuration_values((void *)task->session->configuration);
    uint32_t network_flags=0;
    if (!config->cellular && android_network_flags(&network_flags) && (network_flags&0x40000)) {
        session_finish(task,session_error(-1020,"Cellular data is disabled for this URLSession")); return;
    }
    if (task->kind==2 && !task->path) {
        if (asprintf(&task->path,"%s/session-download-XXXXXX",string_bytes(temporary_directory(),"Download directory"))<0) stop("Allocation failed","Download path");
        task->file=mkstemp(task->path);
        if (task->file<0) { session_finish(task,session_error(-3000,"Creating the download file failed")); return; }
    }
    struct ConnectionState *state=connection_state(task->connection);
    if (!state->started) connection_start(task->connection);
    else {
        JNIEnv *env=attached_environment("NSURLSession resume"); jclass type=(*env)->GetObjectClass(env,activity);
        jmethodID resume=(*env)->GetMethodID(env,type,"suspendConnection","(JZ)V"); (*env)->CallVoidMethod(env,activity,resume,(jlong)state,JNI_FALSE); (*env)->DeleteLocalRef(env,type);
    }
}
static void session_task_suspend(struct SessionTask *task) {
    pthread_mutex_lock(&session_mutex);
    if (task->state>=2) { pthread_mutex_unlock(&session_mutex); return; }
    ++task->suspensions; task->state=1; pthread_mutex_unlock(&session_mutex);
    if (task->connection && connection_state(task->connection)->started) {
        JNIEnv *env=attached_environment("NSURLSession suspend"); jclass type=(*env)->GetObjectClass(env,activity);
        jmethodID suspend=(*env)->GetMethodID(env,type,"suspendConnection","(JZ)V"); (*env)->CallVoidMethod(env,activity,suspend,(jlong)connection_state(task->connection),JNI_TRUE); (*env)->DeleteLocalRef(env,type);
    }
}
static void session_task_cancel_resume(struct SessionTask *task,const char *selector,Word block) { session_cancel(task); if (block) ((void (*)(Word,Word))read_word(block+16))(block,0); }
static Word session_task_property(struct SessionTask *task,const char *selector) {
    if (!strcmp(selector,"taskIdentifier")) return task->identifier;
    if (!strcmp(selector,"state")) return __atomic_load_n(&task->state,__ATOMIC_ACQUIRE);
    if (!strcmp(selector,"error")) return task->error;
    if (!strcmp(selector,"taskDescription")) return task->description;
    if (!strcmp(selector,"countOfBytesReceived")) return task->received;
    if (!strcmp(selector,"countOfBytesSent")) return task->sent;
    if (!strcmp(selector,"countOfBytesExpectedToReceive")) return task->expected_receive;
    if (!strcmp(selector,"countOfBytesExpectedToSend")) return task->expected_send;
    struct ConnectionState *connection=task->connection ? connection_state(task->connection) : NULL;
    if (!connection) return 0;
    return !strcmp(selector,"response") ? connection->response : !strcmp(selector,"originalRequest") ? connection->original_request : connection->request;
}
static float session_task_priority(struct SessionTask *task) { return task->priority; }
static void session_task_set_priority(struct SessionTask *task,const char *selector,float priority) { task->priority=fmaxf(0,fminf(1,priority)); }
static void session_task_description(struct SessionTask *task,const char *selector,Word value) { task->description=value ? text_copy(value,"copy") : 0; }
static void session_invalidate(struct Session *session,const char *selector) {
    pthread_mutex_lock(&session_mutex); session->invalidated=1; struct SessionTask *head=session_tasks; pthread_mutex_unlock(&session_mutex);
    if (!strcmp(selector,"invalidateAndCancel")) for (struct SessionTask *task=head;task;task=task->next) if (task->session==session) session_cancel(task);
    session_check_invalidated(session);
}
static void session_enumerate(struct Session *session,const char *selector,Word block) {
    if (!block) return;
    struct SessionEvent *event=calloc(1,sizeof(*event)); if (!event) stop("Allocation failed",selector);
    event->session=session; event->kind=!strcmp(selector,"getAllTasksWithCompletionHandler:") ? 9 : 8; event->first=(Word)_Block_copy((void *)block); session_emit(event);
}
JNIEXPORT jlongArray JNICALL HOST_JNI(HttpTransport_settings)(JNIEnv *env,jclass cls,jlong token) {
    struct ConnectionState *connection=(void *)token; struct SessionTask *task=(void *)connection->session_task;
    jlong values[4]={0,6,604800000,1};
    if (task) {
        struct SessionConfig *config=configuration_values((void *)task->session->configuration);
        values[0]=(jlong)task->session; values[1]=config->maximum_connections; values[2]=(jlong)(config->resource_timeout*1000); values[3]=config->cookies;
    }
    jlongArray result=(*env)->NewLongArray(env,4); if (result) (*env)->SetLongArrayRegion(env,result,0,4,values); return result;
}
static Method session_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLSession")) {
        if (class_receiver) {
            if (!strcmp(selector,"sessionWithConfiguration:")) return (Method)session_basic;
            if (!strcmp(selector,"sharedSession")) return (Method)session_shared;
        } else {
            if (!strcmp(selector,"sessionDescription")) return (Method)session_description;
            if (!strcmp(selector,"setSessionDescription:")) return (Method)session_set_description;
            if (!strcmp(selector,"dataTaskWithRequest:completionHandler:")) return (Method)session_data_request;
            if (!strcmp(selector,"dataTaskWithRequest:")) return (Method)session_data_request_basic;
            if (!strcmp(selector,"dataTaskWithURL:completionHandler:")) return (Method)session_data_url;
            if (!strcmp(selector,"dataTaskWithURL:")) return (Method)session_data_url_basic;
            if (!strcmp(selector,"downloadTaskWithRequest:completionHandler:")) return (Method)session_download_request;
            if (!strcmp(selector,"downloadTaskWithRequest:")) return (Method)session_download_request_basic;
            if (!strcmp(selector,"downloadTaskWithURL:completionHandler:")) return (Method)session_download_url;
            if (!strcmp(selector,"downloadTaskWithURL:")) return (Method)session_download_url_basic;
            if (!strcmp(selector,"downloadTaskWithResumeData:")) return (Method)session_download_resume;
            if (!strcmp(selector,"uploadTaskWithRequest:fromData:completionHandler:") || !strcmp(selector,"uploadTaskWithRequest:fromFile:completionHandler:")) return (Method)session_upload;
            if (!strcmp(selector,"uploadTaskWithRequest:fromData:") || !strcmp(selector,"uploadTaskWithRequest:fromFile:")) return (Method)session_upload_basic;
            if (!strcmp(selector,"invalidateAndCancel") || !strcmp(selector,"finishTasksAndInvalidate")) return (Method)session_invalidate;
            if (!strcmp(selector,"getTasksWithCompletionHandler:") || !strcmp(selector,"getAllTasksWithCompletionHandler:")) return (Method)session_enumerate;
        }
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSURLSessionTask") || !strcmp(receiver,"_OBJC_CLASS_$_NSURLSessionDataTask") || !strcmp(receiver,"_OBJC_CLASS_$_NSURLSessionUploadTask") || !strcmp(receiver,"_OBJC_CLASS_$_NSURLSessionDownloadTask"))) {
        if (!strcmp(selector,"resume")) return (Method)session_task_resume;
        if (!strcmp(selector,"suspend")) return (Method)session_task_suspend;
        if (!strcmp(selector,"cancel")) return (Method)session_cancel;
        if (!strcmp(selector,"cancelByProducingResumeData:") && !strcmp(receiver,"_OBJC_CLASS_$_NSURLSessionDownloadTask")) return (Method)session_task_cancel_resume;
        if (!strcmp(selector,"priority")) return (Method)session_task_priority;
        if (!strcmp(selector,"setPriority:")) return (Method)session_task_set_priority;
        if (!strcmp(selector,"setTaskDescription:")) return (Method)session_task_description;
        if (!strcmp(selector,"taskIdentifier") || !strcmp(selector,"state") || !strcmp(selector,"error") || !strcmp(selector,"taskDescription") || !strcmp(selector,"response") ||
            !strcmp(selector,"originalRequest") || !strcmp(selector,"currentRequest") || !strcmp(selector,"countOfBytesReceived") || !strcmp(selector,"countOfBytesSent") ||
            !strcmp(selector,"countOfBytesExpectedToSend") || !strcmp(selector,"countOfBytesExpectedToReceive")) return (Method)session_task_property;
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_I2ASessionTransport")) {
        if (!strcmp(selector,"connection:didReceiveData:")) return (Method)session_received_data;
        if (!strcmp(selector,"connection:didSendBodyData:totalBytesWritten:totalBytesExpectedToWrite:")) return (Method)session_sent_body;
        if (!strcmp(selector,"connection:didFailWithError:")) return (Method)session_connection_failed;
        if (!strcmp(selector,"connectionDidFinishLoading:")) return (Method)session_connection_complete;
        if (!strcmp(selector,"connection:willSendRequestForAuthenticationChallenge:")) return (Method)session_connection_auth;
    }
    return NULL;
}
