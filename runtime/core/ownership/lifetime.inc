/* Managed classes keep an O(1) lifetime lookup; weak slots are indexed separately. */
struct MetalLifetime { unsigned references,weak; void (*destroy)(Word); };
static void managed_weak_clear(Word);
static size_t metal_symbol_lifetime_offsets[COUNT(symbols)];
static size_t metal_extra_lifetime_offsets[COUNT(extra_class_names)];
static size_t *metal_lifetime_class_slot(Word cls) {
    Word base=(Word)symbol_objects;
    if (cls>=base && cls-base<sizeof(symbol_objects) && (cls-base)%sizeof(symbol_objects[0])==0)
        return &metal_symbol_lifetime_offsets[(cls-base)/sizeof(symbol_objects[0])];
    base=(Word)extra_class_objects;
    if (cls>=base && cls-base<sizeof(extra_class_objects) && (cls-base)%sizeof(extra_class_objects[0])==0)
        return &metal_extra_lifetime_offsets[(cls-base)/sizeof(extra_class_objects[0])];
    return NULL;
}
static void metal_lifetime_init(Word object,size_t offset,void (*destroy)(Word)) {
    struct MetalLifetime *life=(void *)(object+offset);
    life->references=1; life->weak=0; life->destroy=destroy;
    size_t *slot=metal_lifetime_class_slot(read_word(object));
    if (!slot) stop("Unsupported Metal ownership class","lifetime initialization");
    size_t previous=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
    if (previous && previous!=offset+1) stop("Inconsistent Metal ownership layout","lifetime initialization");
    if (!previous) __atomic_store_n(slot,offset+1,__ATOMIC_RELEASE);
}
static struct MetalLifetime *metal_object_lifetime(Word object) {
     
    if (object<4096 || (object&(sizeof(Word)-1))) return NULL;
    size_t *slot=metal_lifetime_class_slot(read_word(object));
    size_t offset=slot ? __atomic_load_n(slot,__ATOMIC_ACQUIRE) : 0;
    return offset ? (void *)(object+offset-1) : NULL;
}
static Word metal_object_retain(Word object) {
    struct MetalLifetime *life=metal_object_lifetime(object);
    if (life) {
        unsigned previous=__atomic_fetch_add(&life->references,1,__ATOMIC_RELAXED);
        if (!previous || previous==UINT_MAX) stop("Invalid Metal retain count","retain");
    }
    return object;
}
static void metal_object_release(Word object) {
    struct MetalLifetime *life=metal_object_lifetime(object);
    if (life) {
        unsigned previous=__atomic_fetch_sub(&life->references,1,__ATOMIC_ACQ_REL);
        if (!previous) stop("Unbalanced Metal release","release");
        if (previous==1) { if(__atomic_load_n(&life->weak,__ATOMIC_ACQUIRE))managed_weak_clear(object);life->destroy(object); }
    }
}
static Word metal_object_autorelease(Word);
static Word metal_claim_autoreleased(Word);
static Word metal_retain_autorelease(Word);
static void metal_block_retain(const void *object) { metal_object_retain((Word)object); }
static void metal_block_release(const void *object) { metal_object_release((Word)object); }
static void metal_block_destruct(const void *block) { (void)block; }

struct WeakSlot { Word *slot,object;struct WeakSlot *slot_next,*object_next; };
static struct WeakSlot *weak_slots[64],*weak_objects[64];
static pthread_mutex_t weak_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned weak_bucket(Word value) { return (unsigned)((value>>3)^(value>>11))&63; }
static Word managed_try_retain(Word object) {
    struct MetalLifetime *life=metal_object_lifetime(object);if(!life)return object;
    unsigned references=__atomic_load_n(&life->references,__ATOMIC_ACQUIRE);
    while(references){if(references==UINT_MAX)stop("Invalid retain count","Weak reference");if(__atomic_compare_exchange_n(&life->references,&references,references+1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return object;}
    return 0;
}
static void weak_remove_slot(Word *slot) {
    struct WeakSlot **link=&weak_slots[weak_bucket((Word)slot)];while(*link && (*link)->slot!=slot)link=&(*link)->slot_next;
    struct WeakSlot *entry=*link;if(!entry)return;*link=entry->slot_next;
    link=&weak_objects[weak_bucket(entry->object)];while(*link && *link!=entry)link=&(*link)->object_next;if(*link)*link=entry->object_next;free(entry);
}
static Word store_weak(Word *slot,Word value) {
    if(!slot)return value;pthread_mutex_lock(&weak_mutex);weak_remove_slot(slot);
    struct MetalLifetime *life=metal_object_lifetime(value);Word retained=life ? managed_try_retain(value) : 0;if(life && !retained)value=0;
    if(retained){struct WeakSlot *entry=malloc(sizeof(*entry));if(!entry){*slot=0;pthread_mutex_unlock(&weak_mutex);metal_object_release(retained);stop("Allocation failed","Weak reference");}
        unsigned a=weak_bucket((Word)slot),b=weak_bucket(value);*entry=(struct WeakSlot){slot,value,weak_slots[a],weak_objects[b]};weak_slots[a]=weak_objects[b]=entry;__atomic_store_n(&life->weak,1,__ATOMIC_RELEASE);}
    *slot=value;pthread_mutex_unlock(&weak_mutex);if(retained)metal_object_release(retained);return value;
}
static Word load_weak_retained(Word *slot) {
    if(!slot)return 0;pthread_mutex_lock(&weak_mutex);Word value=managed_try_retain(*slot);pthread_mutex_unlock(&weak_mutex);return value;
}
static Word load_weak(Word *slot) { return metal_object_autorelease(load_weak_retained(slot)); }
static void destroy_weak(Word *slot) { if(!slot)return;pthread_mutex_lock(&weak_mutex);weak_remove_slot(slot);*slot=0;pthread_mutex_unlock(&weak_mutex); }
static void move_weak(Word *destination,Word *source) {
    if(destination==source)return;Word value=load_weak_retained(source);store_weak(destination,value);destroy_weak(source);metal_object_release(value);
}
static void managed_weak_clear(Word object) {
    pthread_mutex_lock(&weak_mutex);struct WeakSlot **link=&weak_objects[weak_bucket(object)];
    while(*link){struct WeakSlot *entry=*link;if(entry->object!=object){link=&entry->object_next;continue;}*link=entry->object_next;
        struct WeakSlot **slot=&weak_slots[weak_bucket((Word)entry->slot)];while(*slot && *slot!=entry)slot=&(*slot)->slot_next;if(*slot)*slot=entry->slot_next;*entry->slot=0;free(entry);}
    pthread_mutex_unlock(&weak_mutex);
}
