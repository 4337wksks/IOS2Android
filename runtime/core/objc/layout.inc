/* Clang's non-fragile ObjC ABI emits each class against the superclass size
 * visible in that translation unit. libobjc normally slides subclass ivars
 * when the loaded superclass is larger. This is runtime realization, not a
 * patch to the original executable's instructions or on-disk metadata. */
struct ClassLayout { Word cls; uint32_t size; unsigned visiting,done,initialization; pthread_t initializer; };
static struct ClassLayout class_layouts[CLASSES_COUNT];
static unsigned moved_classes,moved_ivars;
static pthread_mutex_t initialize_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t initialize_condition=PTHREAD_COND_INITIALIZER;

static uint32_t external_instance_size(Word cls) {
    const char *name=host_symbol(cls);
    if (!name) return 8;
    if (!strcmp(name,"_OBJC_CLASS_$_NSOperation") || !strcmp(name,"_OBJC_CLASS_$_NSBlockOperation") || !strcmp(name,"_OBJC_CLASS_$_NSInvocationOperation")) return 24;
    if (!strcmp(name,"_OBJC_CLASS_$_NSArray") || !strcmp(name,"_OBJC_CLASS_$_NSMutableArray") ||
        !strcmp(name,"_OBJC_CLASS_$_NSSet") || !strcmp(name,"_OBJC_CLASS_$_NSMutableSet")) return sizeof(struct Sequence);
    if (!strcmp(name,"_OBJC_CLASS_$_NSDictionary") || !strcmp(name,"_OBJC_CLASS_$_NSMutableDictionary")) return sizeof(struct GuestMap);
    if (!strcmp(name,"_OBJC_CLASS_$_NSString") || !strcmp(name,"_OBJC_CLASS_$_NSMutableString") ||
        !strcmp(name,"_OBJC_CLASS_$_NSData") || !strcmp(name,"_OBJC_CLASS_$_NSMutableData")) return sizeof(struct HostObject);
    if (!strcmp(name,"_OBJC_CLASS_$_NSRecursiveLock")) return 24;
    if (!strcmp(name,"_OBJC_CLASS_$_NSSortDescriptor")) return sizeof(struct HostObject);
    if (!strcmp(name,"_OBJC_CLASS_$_UIAlertController")) return sizeof(struct UIKitAlert);
    if (!strcmp(name,"_OBJC_CLASS_$_UIAlertAction")) return sizeof(struct UIKitAlertAction);
    if (!strcmp(name,"_OBJC_CLASS_$_UIAlertView") || !strcmp(name,"_OBJC_CLASS_$_UIActionSheet")) return sizeof(struct UIKitLegacyAlert);
    if (!strcmp(name,"_OBJC_CLASS_$_SKPayment") || !strcmp(name,"_OBJC_CLASS_$_SKMutablePayment")) return sizeof(struct StorePayment);
    if (!strcmp(name,"_OBJC_CLASS_$_SKPaymentTransaction")) return sizeof(struct StoreTransaction);
    if (!strcmp(name,"_OBJC_CLASS_$_NSLayoutConstraint")) return sizeof(struct LayoutConstraint);
    if (!strcmp(name,"_OBJC_CLASS_$_CLLocation")) return sizeof(struct LocationValue);
    return 8;  
}

static uint32_t realize_class_layout(Word cls,unsigned depth) {
    if (!cls) return 0;
    if (!in_image(cls)) return external_instance_size(cls);
    if (depth>=128) stop("Invalid class inheritance depth","Objective-C realization");
    struct ClassLayout *layout=NULL;
    for (unsigned i=0;i<CLASSES_COUNT;++i) if (class_layouts[i].cls==cls) { layout=class_layouts+i; break; }
    if (!layout) stop("Unlisted superclass","Objective-C realization");
    if (layout->done) return layout->size;
    if (layout->visiting) stop("Cyclic class inheritance",class_name(cls));
    layout->visiting=1;
    uint32_t super_size=realize_class_layout(read_word(cls+8),depth+1);
    Word ro=read_word(cls+32)&~(Word)7;
    if (!in_image(ro)) stop("Invalid class metadata",class_name(cls));
    uint32_t start,size;
    memcpy(&start,(void *)(ro+4),4); memcpy(&size,(void *)(ro+8),4);
    if (size<start) stop("Invalid instance bounds",class_name(cls));
    Word ivars=read_word(ro+48);
    uint32_t count=0,alignment=1;
    if (ivars) {
        uint32_t stride;
        memcpy(&stride,(void *)ivars,4); memcpy(&count,(void *)(ivars+4),4);
        if (stride!=32 || count>4096) stop("Invalid ivar list",class_name(cls));
        for (uint32_t i=0;i<count;++i) {
            uint32_t exponent;
            memcpy(&exponent,(void *)(ivars+8+32*i+24),4);
            if (exponent==UINT32_MAX) exponent=3;
            if (exponent>15) stop("Invalid ivar alignment",class_name(cls));
            if ((1U<<exponent)>alignment) alignment=1U<<exponent;
        }
    }
    if (super_size>start) {
        uint64_t delta=((uint64_t)super_size-start+alignment-1)&~((uint64_t)alignment-1);
        if (delta>UINT32_MAX-size) stop("Realized instance too large",class_name(cls));
        for (uint32_t i=0;i<count;++i) {
            Word pointer=read_word(ivars+8+32*i);
            if (!pointer) continue;
            if (!in_image(pointer)) stop("Invalid ivar offset pointer",class_name(cls));
            uint32_t offset;
            memcpy(&offset,(void *)pointer,4);
            if (offset<start || delta>INT32_MAX-offset) stop("Invalid realized ivar",class_name(cls));
            offset+=(uint32_t)delta;
            memcpy((void *)pointer,&offset,4);
            ++moved_ivars;
        }
        start+=(uint32_t)delta; size+=(uint32_t)delta;
        memcpy((void *)(ro+4),&start,4); memcpy((void *)(ro+8),&size,4);
        ++moved_classes;
    }
    layout->size=size; layout->done=1; layout->visiting=0;
    return size;
}

static void realize_class_layouts(void) {
    const Word *classes=(const Word *)(aslr+CLASSES_OFFSET);
    for (unsigned i=0;i<CLASSES_COUNT;++i) class_layouts[i].cls=classes[i];
    for (unsigned i=0;i<CLASSES_COUNT;++i) realize_class_layout(classes[i],0);
    LOG("Objective-C instance layouts realized: %u classes, %u ivar offsets adjusted",moved_classes,moved_ivars);
}

static void initialize_guest_class(Word cls) {
    struct DynamicClass *dynamic=dynamic_class(cls);
    if (dynamic) {
        if (cls!=(Word)dynamic->words || __atomic_load_n(&dynamic->initialization,__ATOMIC_ACQUIRE)==2) return;
        initialize_guest_class(dynamic->words[1]);
        pthread_mutex_lock(&initialize_mutex);
        while (dynamic->initialization==1 && !pthread_equal(dynamic->initializer,pthread_self())) pthread_cond_wait(&initialize_condition,&initialize_mutex);
        if (dynamic->initialization) { pthread_mutex_unlock(&initialize_mutex); return; }
        dynamic->initializer=pthread_self(); __atomic_store_n(&dynamic->initialization,1,__ATOMIC_RELEASE);
        pthread_mutex_unlock(&initialize_mutex);
        Method method=lookup(dynamic->words[0],"initialize");
        if (method) method(cls,"initialize",0,0,0,0);
        pthread_mutex_lock(&initialize_mutex); __atomic_store_n(&dynamic->initialization,2,__ATOMIC_RELEASE);
        pthread_cond_broadcast(&initialize_condition); pthread_mutex_unlock(&initialize_mutex); return;
    }
    if (!in_image(cls)) return;
    struct ClassLayout *layout=NULL;
    for (unsigned i=0;i<CLASSES_COUNT;++i) if (class_layouts[i].cls==cls) { layout=class_layouts+i; break; }
    if (!layout || __atomic_load_n(&layout->initialization,__ATOMIC_ACQUIRE)==2) return;
    initialize_guest_class(read_word(cls+8));
    pthread_mutex_lock(&initialize_mutex);
    while (layout->initialization==1 && !pthread_equal(layout->initializer,pthread_self())) pthread_cond_wait(&initialize_condition,&initialize_mutex);
    if (layout->initialization) { pthread_mutex_unlock(&initialize_mutex); return; }
    layout->initializer=pthread_self();
    __atomic_store_n(&layout->initialization,1,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&initialize_mutex);
    Method method=lookup(read_word(cls),"initialize");
    if (method) {
        LOG("+[%s initialize] 0x%lx",class_name(cls),(Word)method-aslr+GUEST_IMAGE_BASE);
        method(cls,"initialize",0,0,0,0);
    }
    pthread_mutex_lock(&initialize_mutex);
    __atomic_store_n(&layout->initialization,2,__ATOMIC_RELEASE);
    pthread_cond_broadcast(&initialize_condition);
    pthread_mutex_unlock(&initialize_mutex);
}

static void construct_guest_ivars(Word object,Word cls) {
    if (dynamic_class(cls)) { construct_guest_ivars(object,read_word(cls+8)); return; }
    if (!in_image(cls)) return;
    construct_guest_ivars(object,read_word(cls+8));
    Word ro=read_word(cls+32)&~(Word)7;
    Method construct=method_list(read_word(ro+32),".cxx_construct");
    if (construct && !construct(object,".cxx_construct",0,0,0,0)) stop("C++ ivar construction failed",class_name(cls));
}
