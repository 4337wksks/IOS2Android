 

struct OperationQueue;
struct Operation {
    Word object, dependencies, blocks, completion, target, argument, name;
    const char *action;
    void (*cleanup)(void *);
    void *cleanup_context;
    void (*function)(void *);
    void *function_context;
    struct OperationQueue *queue;
    int scheduled, executing, finished, cancelled;
    long quality;
    struct Operation *next;
};
struct OperationQueue {
    Word object, name, operations;
    long maximum;
    long quality;
    int main, suspended, failed;
    size_t active;
    struct OperationQueue *next;
};
static pthread_mutex_t operations_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t operations_condition=PTHREAD_COND_INITIALIZER;
static struct Operation *operation_states;
static Word operation_state_tag;
static struct OperationQueue *operation_queues;
static struct HostObject main_operation_queue;
static _Thread_local struct OperationQueue *active_operation_queue;

static struct Operation *operation_state(Word object) {
    if (__atomic_load_n((Word *)(object+16),__ATOMIC_ACQUIRE)==(Word)&operation_state_tag) return (void *)read_word(object+8);
    pthread_mutex_lock(&operations_mutex);
    struct Operation *state=read_word(object+16)==(Word)&operation_state_tag ? (void *)read_word(object+8) : NULL;
    if (!state) {
        state=calloc(1,sizeof(*state));
        if (!state) { pthread_mutex_unlock(&operations_mutex); stop("Allocation failed","NSOperation"); }
        state->object=object;
        state->quality=-1;
        state->dependencies=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
        state->blocks=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
        state->next=operation_states;
        operation_states=state;
        memcpy((void *)(object+8),&state,sizeof(state)); __atomic_store_n((Word *)(object+16),(Word)&operation_state_tag,__ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&operations_mutex);
    return state;
}

static struct OperationQueue *operation_queue(Word object) {
    pthread_mutex_lock(&operations_mutex);
    struct OperationQueue *queue=operation_queues;
    while (queue && queue->object!=object) queue=queue->next;
    if (!queue) {
        queue=calloc(1,sizeof(*queue));
        if (!queue) { pthread_mutex_unlock(&operations_mutex); stop("Allocation failed","NSOperationQueue"); }
        queue->object=object;
        queue->maximum=4;
        queue->quality=-1;
        queue->main=object==(Word)&main_operation_queue;
        if (queue->main) queue->maximum=1;
        queue->operations=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
        queue->next=operation_queues;
        operation_queues=queue;
    }
    pthread_mutex_unlock(&operations_mutex);
    return queue;
}

static Word operation_finished(Word object) { return __atomic_load_n(&operation_state(object)->finished,__ATOMIC_ACQUIRE); }
static Word operation_executing(Word object) { return __atomic_load_n(&operation_state(object)->executing,__ATOMIC_ACQUIRE); }
static Word operation_cancelled(Word object) { return __atomic_load_n(&operation_state(object)->cancelled,__ATOMIC_ACQUIRE); }
static Word operation_init(Word object) { operation_state(object); return object; }
static Word operation_false(void) { return 0; }
static void operation_quality(Word object,const char *selector,long value) { operation_state(object)->quality=value; }
static Word operation_get_quality(Word object) { return (Word)operation_state(object)->quality; }
static void operation_set_name(Word object,const char *selector,Word name) { operation_state(object)->name=name ? text_copy(name,"copy") : 0; }
static Word operation_name(Word object) { return operation_state(object)->name; }
static void queue_quality(Word object,const char *selector,long value) { operation_queue(object)->quality=value; }
static Word queue_get_quality(Word object) { return (Word)operation_queue(object)->quality; }

static int dependency_finished(Word object) {
    Method getter=find_implementation(object,"isFinished");
    return getter && (getter(object,"isFinished",0,0,0,0)&255);
}

static Word operation_ready(Word object) {
    if (operation_cancelled(object)) return 1;
    struct Sequence *dependencies=(void *)operation_state(object)->dependencies;
    for (size_t index=0;index<dependencies->count;++index)
        if (!dependency_finished(dependencies->items[index])) return 0;
    return 1;
}

static void operation_pump(struct OperationQueue *queue) {
    for (;;) {
        pthread_mutex_lock(&operations_mutex);
        if (queue->suspended || queue->failed) { pthread_mutex_unlock(&operations_mutex); return; }
        struct Sequence *items=(void *)queue->operations;
        size_t count=items->count;
        Word *snapshot=count ? malloc(count*sizeof(Word)) : NULL;
        if (count && !snapshot) { pthread_mutex_unlock(&operations_mutex); stop("Allocation failed","Operation snapshot"); }
        if (count) memcpy(snapshot,items->items,count*sizeof(Word));
        pthread_mutex_unlock(&operations_mutex);
        struct Operation *candidate=NULL;
        for (size_t index=0;index<count;++index) {
            struct Operation *state=operation_state(snapshot[index]);
            if (!__atomic_load_n(&state->scheduled,__ATOMIC_ACQUIRE) && !candidate && operation_ready(state->object)) candidate=state;
        }
        free(snapshot);
        if (!candidate) return;
        pthread_mutex_lock(&operations_mutex);
        long maximum=queue->maximum<1 ? 4 : queue->maximum;
        if (queue->main) maximum=1;
        if (queue->active>=(size_t)maximum || candidate->scheduled || queue->suspended || queue->failed) { pthread_mutex_unlock(&operations_mutex); return; }
        __atomic_store_n(&candidate->scheduled,1,__ATOMIC_RELEASE);
        ++queue->active;
        pthread_mutex_unlock(&operations_mutex);
        JNIEnv *env=attached_environment("NSOperationQueue scheduling");
        (*env)->CallVoidMethod(env,activity,schedule_operation_method,(jlong)(Word)candidate,queue->main);
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env);
            stop("Operation scheduling failed","Android worker");
        }
    }
}

static void operation_complete(struct Operation *state) {
    int expected=0;
    if (!__atomic_compare_exchange_n(&state->finished,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return;
    __atomic_store_n(&state->executing,0,__ATOMIC_RELEASE);
    if (state->queue) {
        pthread_mutex_lock(&operations_mutex);
        if (state->scheduled && state->queue->active) --state->queue->active;
        struct Sequence *items=(void *)state->queue->operations;
        for(size_t i=0;i<items->count;++i) if(items->items[i]==state->object) { memmove(items->items+i,items->items+i+1,(items->count-i-1)*sizeof(Word)); --items->count; ++items->mutation; break; }
        pthread_mutex_unlock(&operations_mutex);
    }
    if (state->completion) {
        void (*invoke)(Word)=(void (*)(Word))read_word(state->completion+16);
        invoke(state->completion);
    }
    if (state->cleanup) { void (*cleanup)(void *)=state->cleanup; void *context=state->cleanup_context; state->cleanup=NULL; state->cleanup_context=NULL; cleanup(context); }
    pthread_mutex_lock(&operations_mutex);
    pthread_cond_broadcast(&operations_condition);
    struct OperationQueue *queues=operation_queues;
    pthread_mutex_unlock(&operations_mutex);
    for (struct OperationQueue *queue=queues;queue;queue=queue->next) operation_pump(queue);
}

static void operation_main(Word object) {
    struct Operation *state=operation_state(object);
    if (state->function) state->function(state->function_context);
    struct Sequence *blocks=(void *)state->blocks;
    for (size_t index=0;index<blocks->count;++index) {
        Word block=blocks->items[index];
        ((void (*)(Word))read_word(block+16))(block);
    }
    if (state->target && state->action) {
        Method method=find_implementation(state->target,state->action);
        if (!method) stop("Unimplemented operation target",state->action);
        method(state->target,state->action,state->argument,0,0,0);
    }
}

static void operation_start(Word object) {
    struct Operation *state=operation_state(object);
    if (operation_finished(object)) return;
    if (!operation_cancelled(object)) {
        __atomic_store_n(&state->executing,1,__ATOMIC_RELEASE);
        Method main=find_implementation(object,"main");
        if (main) main(object,"main",0,0,0,0);
    }
    operation_complete(state);
}

static void operation_cancel(Word object) { __atomic_store_n(&operation_state(object)->cancelled,1,__ATOMIC_RELEASE); }
static Word operation_dependencies(Word object) { return operation_state(object)->dependencies; }
static void operation_add_dependency(Word object,const char *selector,Word dependency) {
    if (object==dependency) stop("Operation depends on itself",selector);
    struct Sequence *items=(void *)operation_state(object)->dependencies;
    if (!sequence_contains(items,selector,dependency)) sequence_add(items,selector,dependency);
}
static void operation_remove_dependency(Word object,const char *selector,Word dependency) {
    sequence_remove((void *)operation_state(object)->dependencies,selector,dependency);
}
static void operation_completion(Word object,const char *selector,Word block) { operation_state(object)->completion=block ? (Word)_Block_copy((void *)block) : 0; }
static Word operation_get_completion(Word object) { return operation_state(object)->completion; }

static void operation_changed(Word object,const char *selector,Word key) {
    if (!strcmp(string_bytes(key,selector),"isFinished") && dependency_finished(object)) operation_complete(operation_state(object));
}

static Word block_operation(Word cls,const char *selector,Word block) {
    Word object=host_allocate(cls);
    sequence_add((void *)operation_state(object)->blocks,selector,(Word)_Block_copy((void *)block));
    return object;
}
static void operation_add_block(Word object,const char *selector,Word block) {
    sequence_add((void *)operation_state(object)->blocks,selector,(Word)_Block_copy((void *)block));
}
static Word invocation_operation(Word self,const char *selector,Word target,const char *action,Word argument) {
    struct Operation *state=operation_state(self);
    state->target=target; state->action=action; state->argument=argument;
    return self;
}

static Word main_queue(Word cls) { main_operation_queue.isa=cls; return (Word)&main_operation_queue; }
static Word current_operation_queue(Word cls) {
    if (active_operation_queue) return active_operation_queue->object;
    return gettid()==getpid() ? main_queue(cls) : 0;
}
static void queue_set_name(Word self,const char *selector,Word name) { operation_queue(self)->name=name; }
static Word queue_name(Word self) { return operation_queue(self)->name; }
static void queue_set_maximum(Word self,const char *selector,long maximum) {
    struct OperationQueue *queue=operation_queue(self);
    if (maximum==0 || maximum<-1) stop("Invalid operation concurrency",selector);
    pthread_mutex_lock(&operations_mutex); queue->maximum=maximum; pthread_mutex_unlock(&operations_mutex);
    operation_pump(queue);
}
static Word queue_maximum(Word self) { return (Word)operation_queue(self)->maximum; }
static void queue_set_suspended(Word self,const char *selector,unsigned char suspended) {
    struct OperationQueue *queue=operation_queue(self);
    pthread_mutex_lock(&operations_mutex); queue->suspended=suspended; pthread_mutex_unlock(&operations_mutex);
    if (!suspended) operation_pump(queue);
}
static Word queue_suspended(Word self) { return operation_queue(self)->suspended; }
static Word queue_operations(Word self) {
    struct Sequence *source=(void *)operation_queue(self)->operations;
    struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    pthread_mutex_lock(&operations_mutex);
    size_t count=source->count;
    Word *snapshot=count ? malloc(count*sizeof(Word)) : NULL;
    if (count && !snapshot) { pthread_mutex_unlock(&operations_mutex); stop("Allocation failed","Queue snapshot"); }
    if (count) memcpy(snapshot,source->items,count*sizeof(Word));
    pthread_mutex_unlock(&operations_mutex);
    for (size_t index=0;index<count;++index)
        if (!operation_finished(snapshot[index])) sequence_add(result,"operations",snapshot[index]);
    free(snapshot);
    return (Word)result;
}
static void queue_add(Word self,const char *selector,Word object) {
    struct OperationQueue *queue=operation_queue(self);
    struct Operation *state=operation_state(object);
    pthread_mutex_lock(&operations_mutex);
    if (state->queue) { pthread_mutex_unlock(&operations_mutex); stop("Operation already enqueued",selector); }
    state->queue=queue;
    sequence_add((void *)queue->operations,selector,object);
    pthread_mutex_unlock(&operations_mutex);
    operation_pump(queue);
}
static void schedule_queue_function(Word queue,void (*function)(void *),void *context,void (*cleanup)(void *)) {
    Word object=host_allocate(external_class("_OBJC_CLASS_$_NSOperation")); struct Operation *state=operation_state(object);
    state->function=function; state->function_context=context; state->cleanup=cleanup; state->cleanup_context=context;
    queue_add(queue,"addOperation:",object);
}
static void queue_add_block(Word self,const char *selector,Word block) {
    queue_add(self,selector,block_operation(external_class("_OBJC_CLASS_$_NSBlockOperation"),selector,block));
}
static void operation_wait(Word object,const char *selector) {
    struct Operation *state=operation_state(object);
    pthread_mutex_lock(&operations_mutex);
    while (!__atomic_load_n(&state->finished,__ATOMIC_ACQUIRE)) {
        if (state->queue && state->queue->failed) { pthread_mutex_unlock(&operations_mutex); stop("Operation queue failed",selector); }
        if (state->queue && state->queue->main && gettid()==getpid()) { pthread_mutex_unlock(&operations_mutex); stop("Waiting on main-queue operation from main thread",selector); }
        pthread_cond_wait(&operations_condition,&operations_mutex);
    }
    pthread_mutex_unlock(&operations_mutex);
}
static void queue_add_many(Word self,const char *selector,struct Sequence *items,unsigned char wait) {
    for (size_t i=0;i<items->count;++i) queue_add(self,selector,items->items[i]);
    if (wait) for (size_t i=0;i<items->count;++i) operation_wait(items->items[i],selector);
}
static void queue_cancel_all(Word self,const char *selector) {
    struct Sequence *items=(void *)queue_operations(self);
    for (size_t i=0;i<items->count;++i) {
        Method cancel=find_implementation(items->items[i],"cancel");
        if (cancel) cancel(items->items[i],"cancel",0,0,0,0);
    }
    operation_pump(operation_queue(self));
}
static Word queue_count(Word self) {
    struct Sequence *items=(void *)queue_operations(self);
    Word count=items->count;
    free(items->items); free(items);
    return count;
}

static Word recursive_lock_init(Word self) {
    struct HostObject *object=(void *)self;
    if (!object->content[0]) {
        pthread_mutex_t *mutex=calloc(1,sizeof(*mutex));
        pthread_mutexattr_t attr;
        if (!mutex) stop("Allocation failed","NSRecursiveLock");
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
        int result=pthread_mutex_init(mutex,&attr);
        pthread_mutexattr_destroy(&attr);
        if (result) stop("Recursive lock initialization failed","pthread");
        object->content[0]=(Word)mutex;
    }
    return self;
}
static void recursive_lock(Word self) { recursive_lock_init(self); pthread_mutex_lock((void *)read_word(self+8)); }
static void recursive_unlock(Word self) { pthread_mutex_unlock((void *)read_word(self+8)); }
static Word recursive_try_lock(Word self) { recursive_lock_init(self); return pthread_mutex_trylock((void *)read_word(self+8))==0; }
static void recursive_set_name(struct HostObject *self,const char *selector,Word name) { self->content[1]=name; }
static Word recursive_name(struct HostObject *self) { return self->content[1]; }

struct ConditionState { pthread_mutex_t mutex; pthread_cond_t condition; };
static struct ConditionState *condition_state(Word object) {
    struct HostObject *self=(void *)object;
    Word current=__atomic_load_n(&self->content[0],__ATOMIC_ACQUIRE); if (current) return (void *)current;
    struct ConditionState *state=calloc(1,sizeof(*state)); if (!state) stop("Allocation failed","NSCondition");
    pthread_mutexattr_t attributes; pthread_mutexattr_init(&attributes); pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_ERRORCHECK);
    int result=pthread_mutex_init(&state->mutex,&attributes); pthread_mutexattr_destroy(&attributes);
    if (result) { free(state); stop("Condition mutex initialization failed",strerror(result)); }
    result=pthread_cond_init(&state->condition,NULL);
    if (result) { pthread_mutex_destroy(&state->mutex); free(state); stop("Condition initialization failed",strerror(result)); }
    Word expected=0;
    if (!__atomic_compare_exchange_n(&self->content[0],&expected,(Word)state,0,__ATOMIC_RELEASE,__ATOMIC_ACQUIRE)) { pthread_cond_destroy(&state->condition); pthread_mutex_destroy(&state->mutex); free(state); return (void *)expected; }
    return state;
}
static Word condition_init(Word object) { condition_state(object); return object; }
static void condition_lock(Word object) { int error=pthread_mutex_lock(&condition_state(object)->mutex); if (error) stop("Lock failed",strerror(error)); }
static void condition_unlock(Word object) { int error=pthread_mutex_unlock(&condition_state(object)->mutex); if (error) stop("Unlock failed",strerror(error)); }
static Word condition_try(Word object) { int error=pthread_mutex_trylock(&condition_state(object)->mutex); if (error && error!=EBUSY) stop("Try lock failed",strerror(error)); return !error; }
static struct timespec condition_deadline(Word date) {
    double seconds=date_seconds(date); if (!isfinite(seconds) || seconds>(double)INT64_MAX/2) stop("Invalid wait deadline","NSDate");
    if (seconds<0) seconds=0;
    double whole=floor(seconds); return (struct timespec){(time_t)whole,(long)((seconds-whole)*1e9)};
}
static Word condition_lock_until(Word object,const char *selector,Word date) { struct timespec until=condition_deadline(date); int error=pthread_mutex_timedlock(&condition_state(object)->mutex,&until); if (error && error!=ETIMEDOUT) stop("Timed lock failed",strerror(error)); return !error; }
static void condition_wait(Word object) { struct ConditionState *state=condition_state(object); int error=pthread_cond_wait(&state->condition,&state->mutex); if (error) stop("Condition wait failed",strerror(error)); }
static Word condition_wait_until(Word object,const char *selector,Word date) { struct ConditionState *state=condition_state(object); struct timespec until=condition_deadline(date); int error=pthread_cond_timedwait(&state->condition,&state->mutex,&until); if (error && error!=ETIMEDOUT) stop("Condition wait failed",strerror(error)); return !error; }
static void condition_signal(Word object,const char *selector) { struct ConditionState *state=condition_state(object); int error=!strcmp(selector,"broadcast") ? pthread_cond_broadcast(&state->condition) : pthread_cond_signal(&state->condition); if (error) stop("Condition signal failed",strerror(error)); }
static void condition_dealloc(struct HostObject *self) {
    struct ConditionState *state=(void *)self->content[0];
    if (state) { int error=pthread_cond_destroy(&state->condition); if (error) stop("Condition still in use",strerror(error)); error=pthread_mutex_destroy(&state->mutex); if (error) stop("Lock still in use",strerror(error)); free(state); }
    free(self);
}

static Method operations_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSCondition") || !strcmp(receiver,"_OBJC_CLASS_$_NSLock"))) {
        if (!strcmp(selector,"init")) return (Method)condition_init;
        if (!strcmp(selector,"lock")) return (Method)condition_lock;
        if (!strcmp(selector,"unlock")) return (Method)condition_unlock;
        if (!strcmp(selector,"tryLock")) return (Method)condition_try;
        if (!strcmp(selector,"lockBeforeDate:")) return (Method)condition_lock_until;
        if (!strcmp(selector,"setName:")) return (Method)recursive_set_name;
        if (!strcmp(selector,"name")) return (Method)recursive_name;
        if (!strcmp(selector,"dealloc")) return (Method)condition_dealloc;
        if (!strcmp(receiver,"_OBJC_CLASS_$_NSCondition")) {
            if (!strcmp(selector,"wait")) return (Method)condition_wait;
            if (!strcmp(selector,"waitUntilDate:")) return (Method)condition_wait_until;
            if (!strcmp(selector,"signal") || !strcmp(selector,"broadcast")) return (Method)condition_signal;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSOperationQueue")) {
        if (class_receiver && !strcmp(selector,"mainQueue")) return (Method)main_queue;
        if (class_receiver && !strcmp(selector,"currentQueue")) return (Method)current_operation_queue;
        if (!class_receiver) {
            if (!strcmp(selector,"setQualityOfService:")) return (Method)queue_quality;
            if (!strcmp(selector,"qualityOfService")) return (Method)queue_get_quality;
            if (!strcmp(selector,"setName:")) return (Method)queue_set_name;
            if (!strcmp(selector,"name")) return (Method)queue_name;
            if (!strcmp(selector,"setMaxConcurrentOperationCount:")) return (Method)queue_set_maximum;
            if (!strcmp(selector,"maxConcurrentOperationCount")) return (Method)queue_maximum;
            if (!strcmp(selector,"setSuspended:")) return (Method)queue_set_suspended;
            if (!strcmp(selector,"isSuspended")) return (Method)queue_suspended;
            if (!strcmp(selector,"addOperation:")) return (Method)queue_add;
            if (!strcmp(selector,"addOperationWithBlock:")) return (Method)queue_add_block;
            if (!strcmp(selector,"addOperations:waitUntilFinished:")) return (Method)queue_add_many;
            if (!strcmp(selector,"cancelAllOperations")) return (Method)queue_cancel_all;
            if (!strcmp(selector,"operationCount")) return (Method)queue_count;
            if (!strcmp(selector,"operations")) return (Method)queue_operations;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSRecursiveLock") && !class_receiver) {
        if (!strcmp(selector,"init")) return (Method)recursive_lock_init;
        if (!strcmp(selector,"lock")) return (Method)recursive_lock;
        if (!strcmp(selector,"unlock")) return (Method)recursive_unlock;
        if (!strcmp(selector,"tryLock")) return (Method)recursive_try_lock;
        if (!strcmp(selector,"setName:")) return (Method)recursive_set_name;
        if (!strcmp(selector,"name")) return (Method)recursive_name;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSBlockOperation") && class_receiver && !strcmp(selector,"blockOperationWithBlock:")) return (Method)block_operation;
    const char *base=external_base(class_receiver ? object : read_word(object));
    int operation=base && (!strcmp(base,"_OBJC_CLASS_$_NSOperation") || !strcmp(base,"_OBJC_CLASS_$_NSBlockOperation") || !strcmp(base,"_OBJC_CLASS_$_NSInvocationOperation"));
    if (operation && !class_receiver) {
        if (!strcmp(selector,"setName:")) return (Method)operation_set_name;
        if (!strcmp(selector,"name")) return (Method)operation_name;
        if (!strcmp(selector,"setQualityOfService:")) return (Method)operation_quality;
        if (!strcmp(selector,"qualityOfService")) return (Method)operation_get_quality;
        if (!strcmp(selector,"init")) return (Method)operation_init;
        if (!strcmp(selector,"start")) return (Method)operation_start;
        if (!strcmp(selector,"waitUntilFinished")) return (Method)operation_wait;
        if (!strcmp(selector,"main")) return (Method)operation_main;
        if (!strcmp(selector,"cancel")) return (Method)operation_cancel;
        if (!strcmp(selector,"isCancelled")) return (Method)operation_cancelled;
        if (!strcmp(selector,"isFinished")) return (Method)operation_finished;
        if (!strcmp(selector,"isExecuting")) return (Method)operation_executing;
        if (!strcmp(selector,"isReady")) return (Method)operation_ready;
        if (!strcmp(selector,"isAsynchronous") || !strcmp(selector,"isConcurrent")) return (Method)operation_false;
        if (!strcmp(selector,"addDependency:")) return (Method)operation_add_dependency;
        if (!strcmp(selector,"removeDependency:")) return (Method)operation_remove_dependency;
        if (!strcmp(selector,"dependencies")) return (Method)operation_dependencies;
        if (!strcmp(selector,"setCompletionBlock:")) return (Method)operation_completion;
        if (!strcmp(selector,"completionBlock")) return (Method)operation_get_completion;
        if (!strcmp(selector,"addExecutionBlock:")) return (Method)operation_add_block;
        if (!strcmp(selector,"initWithTarget:selector:object:")) return (Method)invocation_operation;
        if (!strcmp(selector,"willChangeValueForKey:")) return (Method)noop;
        if (!strcmp(selector,"didChangeValueForKey:")) return (Method)operation_changed;
    }
    return NULL;
}

JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_runOperation)(JNIEnv *env,jobject instance,jlong token) {
    struct Operation *state=(void *)(Word)token;
    active_operation_queue=state->queue;
    may_unwind=1;
    if (setjmp(boundary)==0) {
        snprintf(stage,sizeof(stage),"Operation %s",class_name(read_word(state->object)));
        Method start=find_implementation(state->object,"start");
        if (!start) stop("Missing operation start",stage);
        start(state->object,"start",0,0,0,0);
        if (dependency_finished(state->object)) operation_complete(state);
        may_unwind=0;
        active_operation_queue=NULL;
        return NULL;
    }
    if (state->queue) {
        pthread_mutex_lock(&operations_mutex); state->queue->failed=1; pthread_cond_broadcast(&operations_condition); pthread_mutex_unlock(&operations_mutex);
    }
    may_unwind=0;
    active_operation_queue=NULL;
    return (*env)->NewStringUTF(env,status);
}
