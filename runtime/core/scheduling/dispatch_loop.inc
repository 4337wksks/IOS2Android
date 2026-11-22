 
static void *dispatch_library;
static void (*dispatch_drain_main)(void *);
static int dispatch_main_fd=-1;
static void *dispatch_export(const char *name) {
    if (!dispatch_library) dispatch_library=dlopen("libdispatch.so",RTLD_NOW|RTLD_LOCAL);
    if (!dispatch_library) stop("Loading libdispatch",dlerror());
    return dlsym(dispatch_library,name);
}
static void schedule_main_function(void (*function)(void *),void *context) {
    void (*enqueue)(void *,void *,void (*)(void *))=dispatch_export("dispatch_async_f");
    void *queue=dispatch_export("_dispatch_main_q");
    if (!enqueue || !queue) stop("Main dispatch queue unavailable","dispatch_async_f");
    enqueue(queue,context,function);
}
static int drain_android_dispatch(int fd,int events,void *context) {
    if (!(events&ALOOPER_EVENT_INPUT)) return 0;
    uint64_t value;
    ssize_t bytes;
    do { bytes=read(fd,&value,sizeof(value)); } while (bytes<0 && errno==EINTR);
    if (bytes<0 && errno==EAGAIN) return 1;
    if (bytes!=(ssize_t)sizeof(value)) return 0;
    may_unwind=1;
    snprintf(stage,sizeof(stage),"Android main dispatch queue");
    int failed=setjmp(boundary);
    if (!failed) dispatch_drain_main(NULL);
    may_unwind=0;
    if (failed) {
        JNIEnv *env=attached_environment("Main dispatch failure");
        jclass type=(*env)->GetObjectClass(env,activity);
        jmethodID report=(*env)->GetMethodID(env,type,"guestFailure","(Ljava/lang/String;)V");
        jstring text=(*env)->NewStringUTF(env,status);
        (*env)->CallVoidMethod(env,activity,report,text);
        (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,type);
        return 0;
    }
    return 1;
}
static void initialize_dispatch_loop(void) {
    int (*get_port)(void)=dispatch_export("_dispatch_get_main_queue_port_4CF");
    dispatch_drain_main=dispatch_export("_dispatch_main_queue_callback_4CF");
    ALooper *looper=ALooper_forThread();
    if (!get_port || !dispatch_drain_main || !looper) stop("Main runloop integration unavailable","libdispatch");
    dispatch_main_fd=get_port();
    if (dispatch_main_fd<0 || ALooper_addFd(looper,dispatch_main_fd,ALOOPER_POLL_CALLBACK,ALOOPER_EVENT_INPUT,drain_android_dispatch,NULL)!=1)
        stop("Main dispatch fd registration failed","Android Looper");
    LOG("libdispatch main queue connected to Android Looper fd=%d",dispatch_main_fd);
}
