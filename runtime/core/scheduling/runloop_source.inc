/* The original HsSRRunLoopThread installs a version0, all-null source solely
 * to keep its condition-backed loop alive. Callback/port sources are not faked. */
struct CoreSourceContext { intptr_t version; Word info,retain,release,description,equal,hash,schedule,cancel,perform; };
struct CoreSourceLink { struct NativeLoop *loop; unsigned mode; struct CoreSourceLink *next; };
struct CoreRunLoopSource { Word tag; unsigned references,valid; intptr_t order; struct CoreSourceLink *links; };
_Static_assert(sizeof(struct CoreSourceContext)==80,"Darwin CFRunLoopSourceContext ABI");
static Word core_source_new(Word allocator,intptr_t order,const struct CoreSourceContext *context) {
    if (allocator || !context || context->version || context->info || context->retain || context->release || context->description || context->equal || context->hash || context->schedule || context->cancel || context->perform) return 0;
    struct CoreRunLoopSource *source=calloc(1,sizeof(*source)); if (!source) return 0;
    source->tag=(Word)&runloop_source_tag; source->references=source->valid=1; source->order=order; return (Word)source;
}
static struct CoreRunLoopSource *core_source_check(Word object) {
    if (!object || read_word(object)!=(Word)&runloop_source_tag) stop("Invalid runloop source","CFRunLoopSource");
    return (void *)object;
}
static int runloop_source_retain(Word object) {
    if (read_word(object)!=(Word)&runloop_source_tag) return 0;
    struct CoreRunLoopSource *source=(void *)object;
    unsigned old=__atomic_fetch_add(&source->references,1,__ATOMIC_RELAXED);
    if (!old || old==UINT_MAX) stop("Invalid source retain","CFRetain"); return 1;
}
static int runloop_source_release(Word object) {
    if (read_word(object)!=(Word)&runloop_source_tag) return 0;
    struct CoreRunLoopSource *source=(void *)object;
    unsigned old=__atomic_fetch_sub(&source->references,1,__ATOMIC_ACQ_REL);
    if (!old) stop("Unbalanced source release","CFRelease");
    if (old==1) { if (source->links) stop("Registered source lost its owner","CFRelease"); free(source); }
    return 1;
}
static unsigned core_source_mode(Word mode) {
    if (!scheduling_mode(mode)) stop("Unsupported source runloop mode","CFRunLoopAddSource");
    return !strcmp(string_bytes(mode,"Runloop source mode"),"kCFRunLoopCommonModes") ? 2 : 1;
}
static void core_source_add(Word loop_object,Word source_object,Word mode) {
    struct CoreRunLoopSource *source=core_source_check(source_object);
    unsigned kind=core_source_mode(mode); struct NativeLoop *loop=native_loop(loop_object);
    pthread_mutex_lock(&scheduling_mutex);
    if (!source->valid) { pthread_mutex_unlock(&scheduling_mutex); return; }
    for (struct CoreSourceLink *p=source->links;p;p=p->next) if (p->loop==loop && p->mode==kind) { pthread_mutex_unlock(&scheduling_mutex); return; }
    struct CoreSourceLink *link=malloc(sizeof(*link));
    if (!link || loop->ports==UINT_MAX) { free(link); pthread_mutex_unlock(&scheduling_mutex); stop("Runloop source allocation failed","CFRunLoopAddSource"); }
    *link=(struct CoreSourceLink){loop,kind,source->links}; source->links=link;
    runloop_source_retain(source_object); ++loop->ports;
    pthread_cond_broadcast(&scheduling_condition); pthread_mutex_unlock(&scheduling_mutex);
}
static void core_source_remove(Word loop_object,Word source_object,Word mode) {
    struct CoreRunLoopSource *source=core_source_check(source_object); unsigned kind=core_source_mode(mode);
    pthread_mutex_lock(&scheduling_mutex); struct CoreSourceLink **slot=&source->links;
    while (*slot && ((*slot)->loop->object!=loop_object || (*slot)->mode!=kind)) slot=&(*slot)->next;
    struct CoreSourceLink *removed=*slot;
    if (removed) { *slot=removed->next; --removed->loop->ports; pthread_cond_broadcast(&scheduling_condition); }
    pthread_mutex_unlock(&scheduling_mutex);
    if (removed) { free(removed); runloop_source_release(source_object); }
}
static void core_source_invalidate(Word object) {
    struct CoreRunLoopSource *source=core_source_check(object); runloop_source_retain(object);
    pthread_mutex_lock(&scheduling_mutex); source->valid=0;
    struct CoreSourceLink *links=source->links; source->links=NULL;
    for (struct CoreSourceLink *p=links;p;p=p->next) --p->loop->ports;
    if (links) pthread_cond_broadcast(&scheduling_condition);
    pthread_mutex_unlock(&scheduling_mutex);
    while (links) { struct CoreSourceLink *next=links->next; free(links); runloop_source_release(object); links=next; }
    runloop_source_release(object);
}
static unsigned char core_source_valid(Word object) {
    struct CoreRunLoopSource *source=core_source_check(object); pthread_mutex_lock(&scheduling_mutex);
    unsigned char valid=source->valid!=0; pthread_mutex_unlock(&scheduling_mutex); return valid;
}
static intptr_t core_source_order(Word object) { return core_source_check(object)->order; }
static void core_source_context(Word object,struct CoreSourceContext *context) {
    core_source_check(object); if (!context || context->version) stop("Unsupported source context version","CFRunLoopSourceGetContext");
    memset(context,0,sizeof(*context));
}
static unsigned char core_source_contains(Word loop,Word object,Word mode) {
    struct CoreRunLoopSource *source=core_source_check(object); unsigned kind=core_source_mode(mode); unsigned char found=0;
    pthread_mutex_lock(&scheduling_mutex);
    for (struct CoreSourceLink *p=source->links;p;p=p->next) if (p->loop->object==loop && p->mode==kind) { found=1; break; }
    pthread_mutex_unlock(&scheduling_mutex); return found;
}
static Word core_source_type(void) { return (Word)&runloop_source_tag; }
static Word resolve_runloop_source_import(const char *name) {
#define RUNLOOP_SOURCE(api,fn) if (!strcmp(name,"_" #api)) return (Word)fn
    RUNLOOP_SOURCE(CFRunLoopSourceCreate,core_source_new); RUNLOOP_SOURCE(CFRunLoopAddSource,core_source_add);
    RUNLOOP_SOURCE(CFRunLoopRemoveSource,core_source_remove); RUNLOOP_SOURCE(CFRunLoopContainsSource,core_source_contains);
    RUNLOOP_SOURCE(CFRunLoopSourceInvalidate,core_source_invalidate); RUNLOOP_SOURCE(CFRunLoopSourceIsValid,core_source_valid);
    RUNLOOP_SOURCE(CFRunLoopSourceGetOrder,core_source_order); RUNLOOP_SOURCE(CFRunLoopSourceGetContext,core_source_context);
    RUNLOOP_SOURCE(CFRunLoopSourceGetTypeID,core_source_type);
#undef RUNLOOP_SOURCE
    return 0;
}
