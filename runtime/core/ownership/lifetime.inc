/* Selective ownership for compatibility-owned Metal objects and the dedicated
 * NSDirectoryEnumerator (to close abandoned DIR handles), not general Foundation.
 * Class addresses index immutable synthetic-class arrays in O(1). No resource
 * registry, per-retain table scan, global lock or GPU wait is involved. */
struct MetalLifetime { unsigned references; void (*destroy)(Word); };
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
    life->references=1; life->destroy=destroy;
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
        if (previous==1) life->destroy(object);
    }
}
static Word metal_object_autorelease(Word);
static Word metal_claim_autoreleased(Word);
static Word metal_retain_autorelease(Word);
static void metal_block_retain(const void *object) { metal_object_retain((Word)object); }
static void metal_block_release(const void *object) { metal_object_release((Word)object); }
static void metal_block_destruct(const void *block) { (void)block; }
