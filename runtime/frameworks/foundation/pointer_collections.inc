/* Pointer collections follow the runtime's current retained object lifetime.
 * Weak entries can be removed/compacted, but deallocation-driven zeroing awaits
 * the runtime's general object ownership implementation. */
struct PointerMap { Word isa,key_options,value_options; struct GuestMap *entries; };
struct PointerArray { struct Sequence array; Word options; };
struct ObjectEnumerator { Word isa; struct Sequence *array; size_t position; Word mutation; int reverse; };
static Word pointer_map_create(Word cls,const char *selector,Word keys,Word values) {
    if ((keys&~0x207UL) || (values&~0x207UL)) stop("Unsupported pointer-map options",selector);
    struct PointerMap *map=calloc(1,sizeof(*map)); if (!map) stop("Allocation failed",selector);
    map->isa=cls; map->key_options=keys; map->value_options=values; map->entries=(void *)empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary")); return (Word)map;
}
static Word pointer_map_standard(Word cls,const char *selector) {
    return pointer_map_create(cls,selector,!strncmp(selector,"weak",4) ? 5 : 0,strstr(selector,"Weak") ? 5 : 0);
}
static Word pointer_map_init(Word object,const char *selector,Word keys,Word values,Word capacity) {
    Word result=pointer_map_create(read_word(object),selector,keys,values); free((void *)object); return result;
}
static Word pointer_map_init_default(Word object,const char *selector) { return pointer_map_init(object,selector,0,0,0); }
static size_t pointer_map_index(struct PointerMap *map,Word key) {
    for (size_t i=0;i<map->entries->count;++i) if ((map->key_options&0x200) ? key==map->entries->entries[i].key : same_objects(key,map->entries->entries[i].key)) return i;
    return map->entries->count;
}
static Word pointer_map_get(struct PointerMap *map,const char *selector,Word key) { size_t i=pointer_map_index(map,key); return i<map->entries->count ? map->entries->entries[i].value : 0; }
static void pointer_map_set(struct PointerMap *map,const char *selector,Word value,Word key) {
    if (!key) stop("Nil map-table key",selector);
    size_t i=pointer_map_index(map,key);
    if (i<map->entries->count) {
        if (value) { map->entries->entries[i].value=value; ++map->entries->mutation; }
        else dictionary_remove(map->entries,selector,map->entries->entries[i].key);
    } else if (value) map_set(map->entries,selector,value,key);
}
static void pointer_map_remove(struct PointerMap *map,const char *selector,Word key) { pointer_map_set(map,selector,0,key); }
static Word pointer_map_count(struct PointerMap *map) { return map->entries->count; }
static void pointer_map_clear(struct PointerMap *map) { dictionary_clear(map->entries); }
static Word pointer_map_dictionary(struct PointerMap *map) { return map_copy(map->entries,"copy"); }
static Word object_enumerator(Word object,const char *selector) {
    struct ObjectEnumerator *enumerator=calloc(1,sizeof(*enumerator)); if (!enumerator) stop("Allocation failed",selector);
    enumerator->isa=external_class("_OBJC_CLASS_$_NSEnumerator"); enumerator->array=(void *)object;
    enumerator->reverse=!strcmp(selector,"reverseObjectEnumerator"); enumerator->mutation=enumerator->array->mutation;
    return (Word)enumerator;
}
static Word enumerator_next(struct ObjectEnumerator *enumerator,const char *selector) {
    if (enumerator->mutation!=enumerator->array->mutation) stop("Collection changed during enumeration",selector);
    size_t count=enumerator->array->count,index=enumerator->position;
    if (index>=count) return 0;
    ++enumerator->position; return enumerator->array->items[enumerator->reverse ? count-index-1 : index];
}
static Word enumerator_remaining(struct ObjectEnumerator *enumerator,const char *selector) {
    Word result=sequence_empty(external_class("_OBJC_CLASS_$_NSArray")),item;
    while ((item=enumerator_next(enumerator,selector))) sequence_add((void *)result,selector,item);
    return result;
}
static Word enumerator_enumerate(struct ObjectEnumerator *enumerator,const char *selector,struct Enumeration *state,Word *buffer,Word capacity) {
    size_t count=0; Word item;
    while (count<capacity && (item=enumerator_next(enumerator,selector))) buffer[count++]=item;
    state->items=buffer; state->mutations=&enumerator->array->mutation; state->cursor+=count; return count;
}
static Word pointer_map_enumerator(struct PointerMap *map,const char *selector) {
    Word array=!strcmp(selector,"keyEnumerator") ? map_keys(map->entries,selector) : dictionary_values(map->entries,selector);
    return object_enumerator(array,"objectEnumerator");
}
static Word pointer_array_create(Word cls,const char *selector,Word options) {
    if (options&~0x207UL) stop("Unsupported pointer-array options",selector);
    struct PointerArray *array=calloc(1,sizeof(*array)); if (!array) stop("Allocation failed",selector);
    array->array.isa=cls; array->options=options; return (Word)array;
}
static Word pointer_array_standard(Word cls,const char *selector) { return pointer_array_create(cls,selector,!strcmp(selector,"weakObjectsPointerArray") ? 5 : 0); }
static Word pointer_array_init(Word object,const char *selector,Word options) { Word result=pointer_array_create(read_word(object),selector,options); free((void *)object); return result; }
static void pointer_array_add(struct Sequence *array,const char *selector,Word pointer) { sequence_reserve(array,array->count+1,selector); array->items[array->count++]=pointer; ++array->mutation; }
static void pointer_array_replace(struct Sequence *array,const char *selector,Word index,Word pointer) {
    if (index>=array->count) stop("Pointer-array index out of bounds",selector); array->items[index]=pointer; ++array->mutation;
}
static void pointer_array_compact(struct Sequence *array) {
    size_t used=0; for (size_t i=0;i<array->count;++i) if (array->items[i]) array->items[used++]=array->items[i];
    array->count=used; ++array->mutation;
}
static Word pointer_array_objects(struct Sequence *array,const char *selector) {
    Word result=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (size_t i=0;i<array->count;++i) if (array->items[i]) sequence_add((void *)result,selector,array->items[i]);
    return result;
}
static Method pointer_collections_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSMapTable")) {
        if (class_receiver) {
            if (!strcmp(selector,"strongToWeakObjectsMapTable") || !strcmp(selector,"weakToStrongObjectsMapTable") || !strcmp(selector,"strongToStrongObjectsMapTable") || !strcmp(selector,"weakToWeakObjectsMapTable")) return (Method)pointer_map_standard;
            if (!strcmp(selector,"mapTableWithKeyOptions:valueOptions:")) return (Method)pointer_map_create;
        } else {
            if (!strcmp(selector,"init")) return (Method)pointer_map_init_default;
            if (!strcmp(selector,"initWithKeyOptions:valueOptions:capacity:")) return (Method)pointer_map_init;
            if (!strcmp(selector,"count")) return (Method)pointer_map_count;
            if (!strcmp(selector,"setObject:forKey:")) return (Method)pointer_map_set;
            if (!strcmp(selector,"objectForKey:")) return (Method)pointer_map_get;
            if (!strcmp(selector,"removeObjectForKey:")) return (Method)pointer_map_remove;
            if (!strcmp(selector,"removeAllObjects")) return (Method)pointer_map_clear;
            if (!strcmp(selector,"dictionaryRepresentation")) return (Method)pointer_map_dictionary;
            if (!strcmp(selector,"keyEnumerator") || !strcmp(selector,"objectEnumerator")) return (Method)pointer_map_enumerator;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSPointerArray")) {
        if (class_receiver && (!strcmp(selector,"weakObjectsPointerArray") || !strcmp(selector,"strongObjectsPointerArray"))) return (Method)pointer_array_standard;
        if (class_receiver && !strcmp(selector,"pointerArrayWithOptions:")) return (Method)pointer_array_create;
        if (!class_receiver) {
            if (!strcmp(selector,"initWithOptions:")) return (Method)pointer_array_init;
            if (!strcmp(selector,"addPointer:")) return (Method)pointer_array_add;
            if (!strcmp(selector,"pointerAtIndex:")) return (Method)array_at;
            if (!strcmp(selector,"replacePointerAtIndex:withPointer:")) return (Method)pointer_array_replace;
            if (!strcmp(selector,"removePointerAtIndex:")) return (Method)sequence_remove_at;
            if (!strcmp(selector,"compact")) return (Method)pointer_array_compact;
            if (!strcmp(selector,"allObjects")) return (Method)pointer_array_objects;
            if (!strcmp(selector,"count")) return (Method)array_count;
        }
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSArray") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableArray")) && (!strcmp(selector,"objectEnumerator") || !strcmp(selector,"reverseObjectEnumerator"))) return (Method)object_enumerator;
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSEnumerator")) {
        if (!strcmp(selector,"nextObject")) return (Method)enumerator_next;
        if (!strcmp(selector,"allObjects")) return (Method)enumerator_remaining;
        if (!strcmp(selector,"countByEnumeratingWithState:objects:count:")) return (Method)enumerator_enumerate;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSValue")) {
        if (class_receiver && !strcmp(selector,"valueWithNonretainedObject:")) return (Method)value_pointer;
        if (!class_receiver && !strcmp(selector,"nonretainedObjectValue")) return (Method)value_pointer_get;
    }
    return NULL;
}
