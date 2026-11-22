 
struct ConnectionState {
    Word object,request,original_request,delegate,runloop,response,delegate_queue,completion;
    Word session_task;
    size_t body_offset;
    int started,cancelled,finished,body_open,synchronous;
    unsigned char *received;
    size_t received_length,received_capacity;
    Word error;
};
struct HttpResponse { Word isa,url,headers,mime,encoding; long status,length; };
struct HttpChallenge { Word isa,space,connection; int answered; };
static int session_http_redirect(Word,Word,Word);
static int session_http_response(Word,Word);

static struct ConnectionState *connection_state(Word object) {
    struct HostObject *connection=(void *)object;
    if (!connection->content[0]) stop("Uninitialized connection","NSURLConnection");
    return (void *)connection->content[0];
}
static jstring http_java_string(JNIEnv *env,Word string) {
    if (!string) return NULL;
    struct TextView text=text_view(string,"HTTP");
    jstring result=java_string(env,text,"HTTP");
    free(text.owned);
    return result;
}
static void connection_java(struct ConnectionState *state,const char *name) {
    JNIEnv *env=attached_environment(name);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,name,"(J)V");
    (*env)->CallVoidMethod(env,activity,method,(jlong)state);
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android connection bridge failed",name); }
}
static void connection_start(Word object) {
    struct ConnectionState *state=connection_state(object);
    if (state->cancelled || __atomic_exchange_n(&state->started,1,__ATOMIC_ACQ_REL)) return;
    if (!state->runloop) state->runloop=state->delegate_queue ? (Word)&main_run_loop : current_runloop(external_class("_OBJC_CLASS_$_NSRunLoop"));
    if (!state->delegate_queue && state->runloop!=(Word)&main_run_loop) stop("Connection requires scheduled main runloop","start");
    connection_java(state,"startConnection");
}
static Word connection_init(Word object,const char *selector,Word request,Word delegate,unsigned char immediately) {
    if (!request) return 0;
    struct ConnectionState *state=calloc(1,sizeof(*state));
    if (!state) stop("Allocation failed",selector);
    state->object=object; state->request=request_copy((void *)request,"copy"); state->original_request=state->request; state->delegate=delegate;
    ((struct HostObject *)object)->content[0]=(Word)state;
    if (immediately) connection_start(object);
    return object;
}
static Word connection_init_basic(Word object,const char *selector,Word request,Word delegate) { return connection_init(object,selector,request,delegate,1); }
static Word connection_factory(Word cls,const char *selector,Word request,Word delegate) { return connection_init(host_allocate(cls),selector,request,delegate,1); }
static void connection_delegate_queue(Word object,const char *selector,Word queue) {
    struct ConnectionState *state=connection_state(object);
    if(state->started && state->delegate_queue!=queue)stop("Delegate queue must be set before starting",selector);
    state->delegate_queue=queue;
}
static Word connection_get_delegate_queue(Word object) { return connection_state(object)->delegate_queue; }
struct ConnectionDelivery { Word block,response,data,error; };
static void connection_delivery_run(void *context) { struct ConnectionDelivery *d=context;struct Block_layout *block=(void *)d->block;((void (*)(Word,Word,Word,Word))block->invoke)(d->block,d->response,d->data,d->error); }
static void connection_delivery_free(void *context) { struct ConnectionDelivery *d=context;_Block_release((void *)d->block);free(d); }
static void connection_deliver_completion(struct ConnectionState *state,Word error) {
    if(!state->completion)return;
    struct ConnectionDelivery *d=calloc(1,sizeof(*d));if(!d)stop("Allocation failed","NSURLConnection completion");
    d->block=state->completion;state->completion=0;d->response=state->response;d->error=error;
    if(!error)d->data=data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"NSURLConnection completion",state->received,state->received_length);
    free(state->received);state->received=NULL;state->received_length=state->received_capacity=0;
    schedule_queue_function(state->delegate_queue,connection_delivery_run,d,connection_delivery_free);
}
static void connection_send_async(Word cls,const char *selector,Word request,Word queue,Word completion) {
    if(!request || !queue || !completion)stop("Missing asynchronous request argument",selector);
    Word object=connection_init(host_allocate(cls),selector,request,0,0);struct ConnectionState *state=connection_state(object);
    state->delegate_queue=queue;state->completion=(Word)_Block_copy((void *)completion);connection_start(object);
}
struct ConnectionRunnable { jobject object; jmethodID run; };
static void connection_runnable_run(void *context) { struct ConnectionRunnable *r=context;JNIEnv *env=attached_environment("Connection delegate queue");(*env)->CallVoidMethod(env,r->object,r->run);if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);stop("Connection callback failed","Android Runnable");} }
static void connection_runnable_free(void *context) { struct ConnectionRunnable *r=context;JNIEnv *env=attached_environment("Connection callback cleanup");(*env)->DeleteGlobalRef(env,r->object);free(r); }
JNIEXPORT jboolean JNICALL HOST_JNI(HttpTransport_dispatchCallback)(JNIEnv *env,jclass cls,jlong token,jobject runnable) {
    struct ConnectionState *state=(void *)token;if(!state->delegate_queue || state->synchronous)return JNI_FALSE;
    static jmethodID run; jmethodID method=__atomic_load_n(&run,__ATOMIC_ACQUIRE);
    if(!method){jclass type=(*env)->FindClass(env,"java/lang/Runnable");method=(*env)->GetMethodID(env,type,"run","()V");(*env)->DeleteLocalRef(env,type);__atomic_store_n(&run,method,__ATOMIC_RELEASE);}
    struct ConnectionRunnable *r=calloc(1,sizeof(*r));if(!r)stop("Allocation failed","Connection callback");r->run=method;r->object=(*env)->NewGlobalRef(env,runnable);
    if(!r->object){free(r);return JNI_FALSE;}
    schedule_queue_function(state->delegate_queue,connection_runnable_run,r,connection_runnable_free);return JNI_TRUE;
}
static void connection_schedule(Word object,const char *selector,Word runloop,Word mode) {
    if (runloop!=(Word)&main_run_loop) stop("Connection requires main runloop",selector);
    const char *name=string_bytes(mode,selector);
    if (strcmp(name,"kCFRunLoopDefaultMode") && strcmp(name,"kCFRunLoopCommonModes") && strcmp(name,"NSDefaultRunLoopMode") && strcmp(name,"NSRunLoopCommonModes")) stop("Unsupported runloop mode",name);
    connection_state(object)->runloop=runloop;
}
static void connection_unschedule(Word object,const char *selector,Word runloop,Word mode) {
    struct ConnectionState *state=connection_state(object);
    if (state->started && !state->finished && !state->cancelled) stop("Active connection unscheduling requires suspended delivery",selector);
    if (state->runloop==runloop) state->runloop=0;
}
static void connection_cancel(Word object) {
    struct ConnectionState *state=connection_state(object);
    __atomic_store_n(&state->cancelled,1,__ATOMIC_RELEASE);
    if (state->started) connection_java(state,"cancelConnection");
}
static Word connection_request(Word object) { return connection_state(object)->request; }
static Word connection_original_request(Word object) { return connection_state(object)->original_request; }
static Word connection_can_handle(Word cls,const char *selector,struct Request *request) {
    if (!request || !request->url) return 0;
    Word scheme=url_component((void *)request->url,"scheme");
    const char *name=string_bytes(scheme,selector);
    return !strcasecmp(name,"http") || !strcasecmp(name,"https");
}

static Word connection_send_sync(Word cls,const char *selector,Word request,Word *response,Word *error) {
    if (response) *response=0;
    if (error) *error=0;
    if (!request || !((struct Request *)request)->url) {
        if (error) *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),selector,copy_string("NSURLErrorDomain"),-1000,0);
        return 0;
    }
    struct ConnectionState state={0};
    state.request=request_copy((void *)request,"copy"); state.original_request=state.request;
    state.synchronous=state.started=1;
    connection_java(&state,"sendConnectionSynchronously");
    if (!state.finished) { free(state.received); stop("Synchronous transport did not complete",selector); }
    if (response) *response=state.response;
    if (error) *error=state.error;
    Word result=state.error ? 0 : data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),selector,state.received,state.received_length);
    free(state.received);
    return result;
}
static Word connection_read_url(Word cls,struct HostObject *url,Word *error) {
    Word request=request_basic(external_class("_OBJC_CLASS_$_NSURLRequest"),"dataWithContentsOfURL:",(Word)url),response=0;
    Word result=connection_send_sync(external_class("_OBJC_CLASS_$_NSURLConnection"),"dataWithContentsOfURL:",request,&response,error);
    if (result && response && ((struct HttpResponse *)response)->status>=400) {
        if (error) *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"dataWithContentsOfURL:",copy_string("NSURLErrorDomain"),-1100,0);
        return 0;
    }
    if (result && cls==external_class("_OBJC_CLASS_$_NSMutableData")) return data_copy(result,"mutableCopy");
    return result;
}

static Word http_headers(JNIEnv *env,jobjectArray fields) {
    struct GuestMap *headers=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    jsize count=fields ? (*env)->GetArrayLength(env,fields) : 0;
    for (jsize i=0;i+1<count;i+=2) {
        jstring name=(*env)->GetObjectArrayElement(env,fields,i),value=(*env)->GetObjectArrayElement(env,fields,i+1);
        map_set(headers,"HTTP headers",string_from_java(env,value,"HTTP header"),string_from_java(env,name,"HTTP header"));
        (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,value);
    }
    return (Word)headers;
}
static Word http_response(JNIEnv *env,jstring url,jint status,jobjectArray fields,jlong length,jstring mime,jstring encoding) {
    struct HttpResponse *response=calloc(1,sizeof(*response));
    if (!response) stop("Allocation failed","HTTP response");
    response->isa=external_class("_OBJC_CLASS_$_NSHTTPURLResponse");
    response->url=url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"HTTP response",string_from_java(env,url,"HTTP response"));
    response->headers=http_headers(env,fields); response->status=status; response->length=length;
    response->mime=mime ? string_from_java(env,mime,"HTTP MIME") : 0;
    response->encoding=encoding ? string_from_java(env,encoding,"HTTP encoding") : 0;
    return (Word)response;
}
static Word response_url(struct HttpResponse *object) { return object->url; }
static Word response_headers(struct HttpResponse *object) { return object->headers; }
static Word response_mime(struct HttpResponse *object) { return object->mime; }
static Word response_encoding(struct HttpResponse *object) { return object->encoding; }
static Word response_status(struct HttpResponse *object) { return object->status; }
static Word response_length(struct HttpResponse *object) { return object->length; }

static void authentication_answer(struct HttpChallenge *challenge,int decision) {
    if (__atomic_exchange_n(&challenge->answered,1,__ATOMIC_ACQ_REL)) return;
    JNIEnv *env=attached_environment("HTTP authentication");
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID answer=(*env)->GetMethodID(env,type,"resolveConnectionAuthentication","(JI)V");
    (*env)->CallVoidMethod(env,activity,answer,(jlong)challenge->connection,(jint)decision);
    (*env)->DeleteLocalRef(env,type);
}
static Word challenge_space(struct HttpChallenge *challenge) { return challenge->space; }
static Word challenge_nil(void) { return 0; }
static Word space_host(struct HostObject *space) { return space->content[0]; }
static Word space_port(struct HostObject *space) { return space->content[1]; }
static Word space_trust(struct HostObject *space) { return space->content[2]; }
static Word space_method(struct HostObject *space) { return space->content[3]; }
static Word space_protocol(void) { return copy_string("https"); }
static Word space_secure(void) { return 1; }
static Word credential_trust(Word cls,const char *selector,Word trust) {
    if (!trust || read_word(trust)!=(Word)&trust_tag) return 0;
    struct HostObject *credential=(void *)host_allocate(cls);
    credential->content[0]=trust;
    return (Word)credential;
}
static Word credential_get_trust(struct HostObject *credential) { return credential->content[0]; }
static void challenge_use(Word sender,const char *selector,struct HostObject *credential,struct HttpChallenge *challenge) {
    struct HostObject *space=(void *)challenge->space;
    authentication_answer(challenge,credential && credential->isa==external_class("_OBJC_CLASS_$_NSURLCredential") && credential->content[0]==space->content[2] ? 0 : 2);
}
static void challenge_default(Word sender,const char *selector,struct HttpChallenge *challenge) { authentication_answer(challenge,1); }
static void challenge_reject(Word sender,const char *selector,struct HttpChallenge *challenge) { authentication_answer(challenge,2); }

static void http_java_failure(JNIEnv *env) {
    may_unwind=0;
    if ((*env)->ExceptionCheck(env)) return;
    jclass type=(*env)->FindClass(env,"java/lang/IllegalStateException");
    (*env)->ThrowNew(env,type,status);
    (*env)->DeleteLocalRef(env,type);
}
#define HTTP_ENTER(fallback) \
    may_unwind=1; snprintf(stage,sizeof(stage),"NSURLConnection %s",__func__); \
    if (setjmp(boundary)) { http_java_failure(env); return fallback; }
#define HTTP_EXIT() (may_unwind=0)

JNIEXPORT jobjectArray JNICALL HOST_JNI(HttpTransport_requestInfo)(JNIEnv *env,jclass cls,jlong token) {
    HTTP_ENTER(NULL);
    struct ConnectionState *state=(void *)token;
    struct Request *request=(void *)state->request;
    if (request->policy>1) stop("HTTP cache policy requires cache storage","NSURLConnection");
    struct GuestMap *headers=(void *)request->headers;
    jclass strings=(*env)->FindClass(env,"java/lang/String");
    jobjectArray result=(*env)->NewObjectArray(env,(jsize)(6+headers->count*2),strings,NULL);
    (*env)->DeleteLocalRef(env,strings);
    char timeout[32],policy[32],length[32];
    double milliseconds=isfinite(request->timeout) && request->timeout>0 ? request->timeout*1000 : 60000;
    snprintf(timeout,sizeof(timeout),"%.0f",fmin(milliseconds,INT32_MAX));
    snprintf(policy,sizeof(policy),"%lu",request->policy);
    snprintf(length,sizeof(length),"%ld",request->body ? (long)read_word(request->body+8) : request->body_stream ? -1L : -2L);
    Word values[]={url_absolute((void *)request->url,"HTTP"),request->method,copy_string(timeout),copy_string(policy),copy_string(length),copy_string(request->cookies ? "1" : "0")};
    for (size_t i=0;i<6+headers->count*2;++i) {
        Word value=i<6 ? values[i] : (i%2 ? headers->entries[(i-6)/2].value : headers->entries[(i-6)/2].key);
        jstring string=http_java_string(env,value);
        (*env)->SetObjectArrayElement(env,result,(jsize)i,string); (*env)->DeleteLocalRef(env,string);
    }
    HTTP_EXIT(); return result;
}
JNIEXPORT jint JNICALL HOST_JNI(HttpTransport_readBody)(JNIEnv *env,jclass cls,jlong token,jbyteArray buffer) {
    HTTP_ENTER(-1);
    struct ConnectionState *state=(void *)token;
    struct Request *request=(void *)state->request;
    unsigned char bytes[32768];
    size_t capacity=(size_t)(*env)->GetArrayLength(env,buffer);
    if (capacity>sizeof(bytes)) capacity=sizeof(bytes);
    intptr_t count=0;
    if (request->body) {
        size_t length=read_word(request->body+8);
        count=(intptr_t)(state->body_offset<length ? length-state->body_offset : 0);
        if ((size_t)count>capacity) count=(intptr_t)capacity;
        if (count) memcpy(bytes,(void *)(data_buffer((void *)request->body)+state->body_offset),(size_t)count);
    } else if (request->body_stream) {
        if (!state->body_open) {
            Method open=find_implementation(request->body_stream,"open");
            if (!open) stop("Missing body stream open","NSURLConnection");
            open(request->body_stream,"open",0,0,0,0); state->body_open=1;
        }
        Method read=find_implementation(request->body_stream,"read:maxLength:");
        if (!read) stop("Missing body stream read","NSURLConnection");
        count=(intptr_t)read(request->body_stream,"read:maxLength:",(Word)bytes,capacity,0,0);
    }
    if (count>0) { (*env)->SetByteArrayRegion(env,buffer,0,(jsize)count,(const jbyte *)bytes); state->body_offset+=(size_t)count; }
    HTTP_EXIT(); return (jint)count;
}
JNIEXPORT jint JNICALL HOST_JNI(HttpTransport_redirect)(JNIEnv *env,jclass cls,jlong token,jstring url,jint code,jobjectArray headers,jstring target) {
    HTTP_ENTER(JNI_FALSE);
    struct ConnectionState *state=(void *)token;
    if (state->cancelled || state->finished) { HTTP_EXIT(); return JNI_FALSE; }
    Word response=code ? http_response(env,url,code,headers,-1,NULL,NULL) : 0;
    struct Request *request=(void *)request_copy((void *)state->request,"mutableCopy");
    if (target) {
        Word next=url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"HTTP redirect",string_from_java(env,target,"HTTP redirect"));
        struct UrlParts a=url_parts((void *)request->url),b=url_parts((void *)next);
        int same=a.authority_start!=SIZE_MAX && b.authority_start!=SIZE_MAX && a.scheme_end==b.scheme_end &&
            a.authority_end-a.authority_start==b.authority_end-b.authority_start && !strncasecmp(a.text,b.text,a.scheme_end) &&
            !strncasecmp(a.text+a.authority_start,b.text+b.authority_start,a.authority_end-a.authority_start);
        if (!same) {
            request_header(request,"setValue:forHTTPHeaderField:",0,copy_string("Authorization"));
            request_header(request,"setValue:forHTTPHeaderField:",0,copy_string("Proxy-Authorization"));
            request_header(request,"setValue:forHTTPHeaderField:",0,copy_string("Cookie"));
        }
        const char *method=string_bytes(request->method,"HTTP redirect");
        if (((code==301 || code==302) && !strcmp(method,"POST")) || (code==303 && strcmp(method,"HEAD"))) {
            request->method=copy_string("GET"); request->body=request->body_stream=0;
            request_header(request,"setValue:forHTTPHeaderField:",0,copy_string("Content-Length"));
            request_header(request,"setValue:forHTTPHeaderField:",0,copy_string("Content-Type"));
        } else if (state->body_offset && request->body_stream) {
            Method replacement=find_implementation(state->delegate,"connection:needNewBodyStream:");
            request->body_stream=replacement ? replacement(state->delegate,"connection:needNewBodyStream:",state->object,request->body_stream,0,0) : 0;
            if (!request->body_stream) stop("Redirect needs a replayable request body","NSURLConnection");
        }
        request->url=next;
    }
    if (state->session_task && response) {
        int result=session_http_redirect(state->session_task,(Word)request,response); HTTP_EXIT(); return result;
    }
    Method method=find_implementation(state->delegate,"connection:willSendRequest:redirectResponse:");
    Word accepted=method ? method(state->delegate,"connection:willSendRequest:redirectResponse:",state->object,(Word)request,response,0) : (Word)request;
    if (!accepted) { state->cancelled=1; HTTP_EXIT(); return JNI_FALSE; }
    state->request=request_copy((void *)accepted,"copy"); state->body_offset=0; state->body_open=0;
    HTTP_EXIT(); return JNI_TRUE;
}
JNIEXPORT jint JNICALL HOST_JNI(HttpTransport_response)(JNIEnv *env,jclass cls,jlong token,jstring url,jint code,jobjectArray headers,jlong length,jstring mime,jstring encoding) {
    HTTP_ENTER(0);
    struct ConnectionState *state=(void *)token;
    if (!state->cancelled && !state->finished) {
        state->response=http_response(env,url,code,headers,length,mime,encoding);
        if (state->session_task) { int result=session_http_response(state->session_task,state->response); HTTP_EXIT(); return result; }
        Method method=find_implementation(state->delegate,"connection:didReceiveResponse:");
        if (method) method(state->delegate,"connection:didReceiveResponse:",state->object,state->response,0,0);
    }
    HTTP_EXIT(); return state->cancelled ? 0 : 1;
}
JNIEXPORT void JNICALL HOST_JNI(HttpTransport_data)(JNIEnv *env,jclass cls,jlong token,jbyteArray buffer,jint length) {
    HTTP_ENTER();
    struct ConnectionState *state=(void *)token;
    if (!state->cancelled && !state->finished) {
        if (length<0 || length>(*env)->GetArrayLength(env,buffer)) stop("Invalid HTTP data length","NSURLConnection");
        if (state->synchronous || state->completion) {
            if ((size_t)length>SIZE_MAX-state->received_length) stop("HTTP response too large","NSURLConnection");
            size_t needed=state->received_length+(size_t)length;
            if (needed>state->received_capacity) {
                size_t capacity=state->received_capacity<=SIZE_MAX/2 ? state->received_capacity*2 : needed;
                if (capacity<needed) capacity=needed;
                if (capacity<32768) capacity=32768;
                void *bytes=realloc(state->received,capacity);
                if (!bytes) stop("Allocation failed","NSURLConnection response");
                state->received=bytes; state->received_capacity=capacity;
            }
            if (length) (*env)->GetByteArrayRegion(env,buffer,0,length,(jbyte *)(state->received+state->received_length));
            state->received_length=needed;
            HTTP_EXIT(); return;
        }
        Method method=find_implementation(state->delegate,"connection:didReceiveData:");
        if (method) {
            jbyte *bytes=(*env)->GetByteArrayElements(env,buffer,NULL);
            Word data=data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"HTTP data",bytes,(size_t)length);
            (*env)->ReleaseByteArrayElements(env,buffer,bytes,JNI_ABORT);
            method(state->delegate,"connection:didReceiveData:",state->object,data,0,0);
        }
    }
    HTTP_EXIT();
}
JNIEXPORT void JNICALL HOST_JNI(HttpTransport_sentBody)(JNIEnv *env,jclass cls,jlong token,jlong count,jlong total,jlong expected) {
    HTTP_ENTER();
    struct ConnectionState *state=(void *)token;
    if (!state->cancelled && !state->finished) {
        const char *selector="connection:didSendBodyData:totalBytesWritten:totalBytesExpectedToWrite:";
        Method method=find_implementation(state->delegate,selector);
        if (method) method(state->delegate,selector,state->object,(Word)count,(Word)total,(Word)expected);
    }
    HTTP_EXIT();
}
JNIEXPORT void JNICALL HOST_JNI(HttpTransport_complete)(JNIEnv *env,jclass cls,jlong token,jint code,jstring description) {
    HTTP_ENTER();
    struct ConnectionState *state=(void *)token;
    if (!state->cancelled && !state->finished) {
        state->finished=1;
        Word error=0;
        if (code) {
            struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
            map_set(info,"HTTP error",description ? string_from_java(env,description,"HTTP error") : copy_string("HTTP transport failed"),copy_string("NSLocalizedDescription"));
            Word url=((struct Request *)state->request)->url;
            map_set(info,"HTTP error",url,copy_string("NSErrorFailingURLKey"));
            map_set(info,"HTTP error",url_absolute((void *)url,"HTTP error"),copy_string("NSErrorFailingURLStringKey"));
            error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"HTTP",copy_string("NSURLErrorDomain"),code,(Word)info);
            LOG("HTTP transport completed with error=%d",code);
        }
        state->error=error;
        if(state->completion)connection_deliver_completion(state,error);
        const char *selector=code ? "connection:didFailWithError:" : "connectionDidFinishLoading:";
        Method method=find_implementation(state->delegate,selector);
        if (method) method(state->delegate,selector,state->object,error,0,0);
    }
    HTTP_EXIT();
}
JNIEXPORT void JNICALL HOST_JNI(HttpTransport_authenticate)(JNIEnv *env,jclass cls,jlong token,jstring hostname,jint port,jobjectArray chain) {
    HTTP_ENTER();
    struct ConnectionState *state=(void *)token;
    struct Trust *trust=calloc(1,sizeof(*trust));
    struct HttpChallenge *challenge=calloc(1,sizeof(*challenge));
    if (!trust || !challenge) stop("Allocation failed","Server trust challenge");
    trust->tag=(Word)&trust_tag; trust->chain=(*env)->NewGlobalRef(env,chain);
    struct HostObject *space=(void *)host_allocate(external_class("_OBJC_CLASS_$_NSURLProtectionSpace"));
    space->content[0]=string_from_java(env,hostname,"TLS host"); space->content[1]=(Word)port;
    space->content[2]=(Word)trust; space->content[3]=copy_string("NSURLAuthenticationMethodServerTrust");
    challenge->isa=external_class("_OBJC_CLASS_$_NSURLAuthenticationChallenge"); challenge->space=(Word)space; challenge->connection=(Word)state;
    const char *selector="connection:willSendRequestForAuthenticationChallenge:";
    Method method=find_implementation(state->delegate,selector);
    if (!method) {
        selector="connection:didReceiveAuthenticationChallenge:";
        method=find_implementation(state->delegate,selector);
        Method can=find_implementation(state->delegate,"connection:canAuthenticateAgainstProtectionSpace:");
        if (can && !can(state->delegate,"connection:canAuthenticateAgainstProtectionSpace:",state->object,(Word)space,0,0)) method=NULL;
    }
    if (state->cancelled) authentication_answer(challenge,2);
    else if (method) method(state->delegate,selector,state->object,(Word)challenge,0,0);
    else authentication_answer(challenge,1);
    HTTP_EXIT();
}
#undef HTTP_ENTER
#undef HTTP_EXIT

static Method connection_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLConnection")) {
        if (class_receiver) {
            if (!strcmp(selector,"sendAsynchronousRequest:queue:completionHandler:")) return (Method)connection_send_async;
            if (!strcmp(selector,"sendSynchronousRequest:returningResponse:error:")) return (Method)connection_send_sync;
            if (!strcmp(selector,"connectionWithRequest:delegate:")) return (Method)connection_factory;
            if (!strcmp(selector,"canHandleRequest:")) return (Method)connection_can_handle;
        } else {
            if (!strcmp(selector,"setDelegateQueue:")) return (Method)connection_delegate_queue;
            if (!strcmp(selector,"delegateQueue")) return (Method)connection_get_delegate_queue;
            if (!strcmp(selector,"initWithRequest:delegate:startImmediately:")) return (Method)connection_init;
            if (!strcmp(selector,"initWithRequest:delegate:")) return (Method)connection_init_basic;
            if (!strcmp(selector,"scheduleInRunLoop:forMode:")) return (Method)connection_schedule;
            if (!strcmp(selector,"unscheduleFromRunLoop:forMode:")) return (Method)connection_unschedule;
            if (!strcmp(selector,"start")) return (Method)connection_start;
            if (!strcmp(selector,"cancel")) return (Method)connection_cancel;
            if (!strcmp(selector,"originalRequest")) return (Method)connection_original_request;
            if (!strcmp(selector,"currentRequest")) return (Method)connection_request;
        }
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSHTTPURLResponse") || !strcmp(receiver,"_OBJC_CLASS_$_NSURLResponse"))) {
        if (!strcmp(selector,"URL")) return (Method)response_url;
        if (!strcmp(selector,"allHeaderFields")) return (Method)response_headers;
        if (!strcmp(selector,"statusCode")) return (Method)response_status;
        if (!strcmp(selector,"expectedContentLength")) return (Method)response_length;
        if (!strcmp(selector,"MIMEType")) return (Method)response_mime;
        if (!strcmp(selector,"textEncodingName")) return (Method)response_encoding;
        if (!strcmp(selector,"copy")) return (Method)identity;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLCredential")) {
        if (class_receiver && !strcmp(selector,"credentialForTrust:")) return (Method)credential_trust;
        if (!class_receiver && !strcmp(selector,"trust")) return (Method)credential_get_trust;
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSURLProtectionSpace")) {
        if (!strcmp(selector,"host")) return (Method)space_host;
        if (!strcmp(selector,"port")) return (Method)space_port;
        if (!strcmp(selector,"protocol")) return (Method)space_protocol;
        if (!strcmp(selector,"serverTrust")) return (Method)space_trust;
        if (!strcmp(selector,"authenticationMethod")) return (Method)space_method;
        if (!strcmp(selector,"receivesCredentialSecurely")) return (Method)space_secure;
        if (!strcmp(selector,"realm") || !strcmp(selector,"isProxy")) return (Method)challenge_nil;
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSURLAuthenticationChallenge")) {
        if (!strcmp(selector,"sender")) return (Method)identity;
        if (!strcmp(selector,"protectionSpace")) return (Method)challenge_space;
        if (!strcmp(selector,"previousFailureCount") || !strcmp(selector,"proposedCredential") || !strcmp(selector,"error") || !strcmp(selector,"failureResponse")) return (Method)challenge_nil;
        if (!strcmp(selector,"useCredential:forAuthenticationChallenge:")) return (Method)challenge_use;
        if (!strcmp(selector,"performDefaultHandlingForAuthenticationChallenge:") || !strcmp(selector,"continueWithoutCredentialForAuthenticationChallenge:")) return (Method)challenge_default;
        if (!strcmp(selector,"cancelAuthenticationChallenge:") || !strcmp(selector,"rejectProtectionSpaceAndContinueWithChallenge:")) return (Method)challenge_reject;
    }
    return NULL;
}
