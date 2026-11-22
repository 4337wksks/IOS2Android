 
 
static Word predicate_regex_create(Word,const char *,Word,unsigned);
static Word predicate_regex_evaluate(struct HostObject *,const char *,Word);
__attribute__((used,noinline)) static Word collection_predicate_format(Word cls,const char *selector,Word format,const Word *arguments) {
    const char *text=string_bytes(format,selector),*cursor=text;
    while (*cursor==' ') ++cursor;
    if (!strncmp(cursor,"SELF MATCHES",12)) {
        cursor+=12; unsigned flags=0;
        if (!strncmp(cursor,"[c]",3)) { flags=1; cursor+=3; }
        while (*cursor==' ') ++cursor;
        if (strncmp(cursor,"%@",2)) stop("Unsupported predicate regex operand",text);
        cursor+=2; while (*cursor==' ') ++cursor;
        if (*cursor) stop("Unsupported predicate regex expression",text);
        return predicate_regex_create(cls,selector,arguments[0],flags);
    }
    if (!strncmp(cursor,"SELF.",5)) cursor+=5;
    const char *property=NULL;
    if (!strncmp(cursor,"length",6)) { property="length"; cursor+=6; }
    else if (!strncmp(cursor,"count",5)) { property="count"; cursor+=5; }
    if (!property) stop("Unsupported predicate property",text);
    while (*cursor==' ') ++cursor;
    Word operation=0;
    if (!strncmp(cursor,">=",2)) { operation=1; cursor+=2; }
    else if (!strncmp(cursor,"<=",2)) { operation=2; cursor+=2; }
    else if (!strncmp(cursor,"==",2)) { operation=3; cursor+=2; }
    else if (!strncmp(cursor,"!=",2) || !strncmp(cursor,"<>",2)) { operation=4; cursor+=2; }
    else if (*cursor=='>') { operation=5; ++cursor; }
    else if (*cursor=='<') { operation=6; ++cursor; }
    else if (*cursor=='=') { operation=3; ++cursor; }
    else stop("Unsupported predicate comparison",text);
    while (*cursor==' ') ++cursor;
    if (*cursor<'0' || *cursor>'9') stop("Unsupported predicate operand",text);
    char *end; errno=0; unsigned long long value=strtoull(cursor,&end,10);
    while (*end==' ') ++end;
    if (errno || *end) stop("Unsupported predicate expression",text);
    struct HostObject *predicate=calloc(1,sizeof(*predicate)); if (!predicate) stop("Allocation failed",selector);
    predicate->isa=cls; predicate->content[0]=(Word)property; predicate->content[1]=operation; predicate->content[2]=value;
    return (Word)predicate;
}
__attribute__((naked)) static void collection_predicate(void) { __asm__ volatile("mov x3,sp\n b collection_predicate_format"); }
static Word collection_predicate_block(Word cls,const char *selector,Word block) {
    if (!block) stop("Nil predicate block",selector);
    struct HostObject *predicate=(void *)host_allocate(cls);
    predicate->content[0]=(Word)_Block_copy((void *)block); predicate->content[1]=8;
    if (!predicate->content[0]) { free(predicate); stop("Allocation failed",selector); }
    return (Word)predicate;
}
static Word collection_predicate_value(Word cls,const char *selector,unsigned char value) {
    struct HostObject *predicate=(void *)host_allocate(cls); predicate->content[1]=9; predicate->content[2]=value!=0;
    return (Word)predicate;
}
static Word collection_predicate_block_get(struct HostObject *predicate) { return predicate->content[0]; }
static void collection_predicate_block_dealloc(struct HostObject *predicate) {
    _Block_release((void *)predicate->content[0]); free(predicate);
}
static Word collection_predicate_evaluate_variables(struct HostObject *predicate,const char *selector,Word object,Word variables) {
    if (predicate->content[1]==7) return predicate_regex_evaluate(predicate,selector,object);
    if (predicate->content[1]==8) {
        Word block=predicate->content[0]; unsigned char (*invoke)(Word,Word,Word)=(void *)read_word(block+16);
        return invoke(block,object,variables)!=0;
    }
    if (predicate->content[1]==9) return predicate->content[2];
    Word value=0;
    if (object) {
        Method getter=find_implementation(object,(const char *)predicate->content[0]);
        if (!getter) stop("Missing predicate property getter",(const char *)predicate->content[0]);
        value=getter(object,(const char *)predicate->content[0],0,0,0,0);
    }
    Word operand=predicate->content[2];
    switch (predicate->content[1]) {
    case 1:return value>=operand; case 2:return value<=operand; case 3:return value==operand;
    case 4:return value!=operand; case 5:return value>operand; case 6:return value<operand;
    default:stop("Invalid predicate comparison",selector);
    }
}
static Word collection_predicate_evaluate(struct HostObject *predicate,const char *selector,Word object) {
    return collection_predicate_evaluate_variables(predicate,selector,object,0);
}
static Word collection_filter(struct Sequence *self,const char *selector,Word predicate) {
    if (!predicate) stop("Nil predicate",selector);
    Method evaluate=find_implementation(predicate,"evaluateWithObject:");
    if (!evaluate) stop("Missing predicate evaluator",selector);
    Word mutation=self->mutation;
    struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (size_t i=0;i<self->count;++i) {
        Word object=self->items[i];
        if (evaluate(predicate,"evaluateWithObject:",object,0,0,0)) sequence_add(result,selector,object);
        if (self->mutation!=mutation) stop("Collection mutated during predicate",selector);
    }
    return (Word)result;
}
static void collection_filter_mutable(struct Sequence *self,const char *selector,Word predicate) {
    struct Sequence *result=(void *)collection_filter(self,selector,predicate);
    free(self->items); self->items=result->items; self->count=result->count; self->capacity=result->capacity;
    ++self->mutation; free(result);
}
static Word collection_init_array(Word self,const char *selector,struct Sequence *source) {
    Word result=sequence_from_array(read_word(self),selector,source);
    struct Sequence *old=(void *)self;
    free(old->items); free(old);
    return result;
}

static Word collection_one(Word cls,const char *selector,Word object) {
    if (!object) stop("Nil collection object",selector);
    return new_array(cls,selector,&object,1);
}

static void collection_add_array(struct Sequence *self,const char *selector,struct Sequence *source) {
    if (!source) return;
    size_t count=source->count;
    if (count>SIZE_MAX-self->count) stop("Collection too large",selector);
    sequence_reserve(self,self->count+count,selector);
    for (size_t index=0;index<count;++index) sequence_add(self,selector,source->items[index]);
}

static void collection_remove_array(struct Sequence *self,const char *selector,struct Sequence *source) {
    if (!source) stop("Nil removal array",selector);
    if (self==source) { sequence_clear(self); return; }
    Word mutation=self->mutation,source_mutation=source->mutation;
    size_t retained=0;
    for (size_t index=0;index<self->count;++index) {
        Word value=self->items[index];
        int remove=0;
        for (size_t candidate=0;candidate<source->count;++candidate) {
            remove=same_objects(value,source->items[candidate]);
            if (self->mutation!=mutation || source->mutation!=source_mutation) stop("Collection mutated during comparison",selector);
            if (remove) break;
        }
        if (!remove) self->items[retained++]=value;
    }
    if (retained!=self->count) { self->count=retained; ++self->mutation; }
}

static Word collection_from_set(Word cls,const char *selector,struct Sequence *source) {
    return source ? sequence_from_array(cls,selector,source) : sequence_empty(cls);
}

static Word collection_init_set(Word self,const char *selector,struct Sequence *source) {
    Word result=collection_from_set(read_word(self),selector,source);
    struct Sequence *old=(void *)self;
    free(old->items); free(old);
    return result;
}

static void collection_set_contents(struct Sequence *self,const char *selector,struct Sequence *source) {
    if (!source) stop("Nil collection",selector);
    if (self==source) return;
    struct Sequence *replacement=(void *)sequence_from_array(self->isa,selector,source);
    free(self->items); self->items=replacement->items; self->count=replacement->count; self->capacity=replacement->capacity;
    ++self->mutation; free(replacement);
}

static Word collection_subarray(struct Sequence *self,const char *selector,struct GuestRange range) {
    check_range(range,self->count,selector);
    return new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,self->items+range.location,range.length);
}

static void collection_remove_range(struct Sequence *self,const char *selector,struct GuestRange range) {
    check_range(range,self->count,selector);
    memmove(self->items+range.location,self->items+range.location+range.length,(self->count-range.location-range.length)*sizeof(Word));
    self->count-=range.length; ++self->mutation;
}

static void collection_replace(struct Sequence *self,const char *selector,size_t index,Word value) {
    if (!value || index>=self->count) stop("Invalid array replacement",selector);
    self->items[index]=value; ++self->mutation;
}

static void dictionary_remove(struct GuestMap *self,const char *selector,Word key) {
    size_t index=map_index(self,key);
    if (index==self->count) return;
    memmove(self->entries+index,self->entries+index+1,(self->count-index-1)*sizeof(*self->entries));
    --self->count; ++self->mutation;
}

static void dictionary_subscript(struct GuestMap *self,const char *selector,Word value,Word key) {
    if (value) map_set(self,selector,value,key); else dictionary_remove(self,selector,key);
}

static Word dictionary_from_dictionary(Word cls,const char *selector,struct GuestMap *source) {
    if (!source) return empty_map(cls);
    struct GuestMap *result=(void *)map_copy(source,"copy");
    result->isa=cls;
    return (Word)result;
}

static Word dictionary_init_dictionary(Word self,const char *selector,struct GuestMap *source) {
    Word result=dictionary_from_dictionary(read_word(self),selector,source);
    struct GuestMap *old=(void *)self;
    free(old->entries); free(old);
    return result;
}

static void dictionary_merge(struct GuestMap *self,const char *selector,struct GuestMap *source) {
    if (self==source) return;
    for (size_t i=0;i<source->count;++i) map_set(self,selector,source->entries[i].value,source->entries[i].key);
}

static void dictionary_clear(struct GuestMap *self) { self->count=0; ++self->mutation; }

static Word dictionary_values(struct GuestMap *self,const char *selector) {
    struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for (size_t i=0;i<self->count;++i) sequence_add(result,selector,self->entries[i].value);
    return (Word)result;
}

static Word dictionary_one(Word cls,const char *selector,Word value,Word key) {
    struct GuestMap *result=(void *)empty_map(cls);
    map_set(result,selector,value,key);
    return (Word)result;
}

static Word perform_selector(Word self,const char *selector,const char *action,Word first,Word second) {
    Method method=find_implementation(self,action);
    if (!method) stop("Unimplemented selector",action);
    return method(self,action,first,second,0,0);
}

static Word data_empty(Word cls) { return data_with_bytes(cls,"data",NULL,0); }
static Word data_subrange(struct HostObject *self,const char *selector,struct GuestRange range) {
    check_range(range,self->content[0],selector);
    return data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),selector,(char *)data_buffer(self)+range.location,range.length);
}
static void data_get_range(struct HostObject *self,const char *selector,void *output,struct GuestRange range) {
    check_range(range,self->content[0],selector);
    if (range.length) memcpy(output,(char *)data_buffer(self)+range.location,range.length);
}
static void data_get_length(struct HostObject *self,const char *selector,void *output,size_t length) {
    data_get_range(self,selector,output,(struct GuestRange){0,length});
}
static Word data_equal(struct HostObject *self,const char *selector,struct HostObject *other) {
    return other && self->content[0]==other->content[0] && !memcmp((void *)data_buffer(self),(void *)data_buffer(other),self->content[0]);
}

static void dictionary_enumerate_block(struct GuestMap *self,const char *selector,Word block) {
    if (!block) stop("Nil enumeration block",selector);
    void (*invoke)(Word,Word,Word,unsigned char *)=(void *)read_word(block+16);
    Word mutation=self->mutation;
    unsigned char done=0;
    for (size_t index=0;index<self->count && !done;++index) {
        invoke(block,self->entries[index].key,self->entries[index].value,&done);
        if (self->mutation!=mutation) stop("Dictionary mutated during enumeration",selector);
    }
}
static void array_enumerate_block(struct Sequence *self,const char *selector,Word block) {
    if (!block) stop("Nil enumeration block",selector);
    void (*invoke)(Word,Word,Word,unsigned char *)=(void *)read_word(block+16);
    Word mutation=self->mutation;
    unsigned char done=0;
    for (size_t index=0;index<self->count && !done;++index) {
        invoke(block,self->items[index],index,&done);
        if (self->mutation!=mutation) stop("Array mutated during enumeration",selector);
    }
}
static void set_enumerate_block(struct Sequence *self,const char *selector,Word block) {
    if (!block) stop("Nil enumeration block",selector);
    void (*invoke)(Word,Word,unsigned char *)=(void *)read_word(block+16);
    Word mutation=self->mutation;
    unsigned char done=0;
    for (size_t index=0;index<self->count && !done;++index) {
        invoke(block,self->items[index],&done);
        if (self->mutation!=mutation) stop("Set mutated during enumeration",selector);
    }
}
static Word collection_set_member(struct Sequence *self,const char *selector,Word value) {
    Word index=sequence_index(self,selector,value);
    return index==INT64_MAX ? 0 : self->items[index];
}
static Word collection_sets_intersect(struct Sequence *self,const char *selector,struct Sequence *other) {
    if (!other) return 0;
    for (size_t index=0;index<self->count;++index)
        if (sequence_contains(other,selector,self->items[index])) return 1;
    return 0;
}
static void collection_set_intersection(struct Sequence *self,const char *selector,struct Sequence *other) {
    if (self==other) return;
    if (!other) { sequence_clear(self); return; }
    Word mutation=self->mutation,other_mutation=other->mutation;
    size_t retained=0;
    for (size_t index=0;index<self->count;++index) {
        Word value=self->items[index];
        int keep=sequence_contains(other,selector,value);
        if (self->mutation!=mutation || other->mutation!=other_mutation) stop("Collection mutated during comparison",selector);
        if (keep) self->items[retained++]=value;
    }
    if (retained!=self->count) { self->count=retained; ++self->mutation; }
}
static Word array_index_block(struct Sequence *self,const char *selector,Word block) {
    if (!block) stop("Nil predicate block",selector);
    unsigned char (*invoke)(Word,Word,Word,unsigned char *)=(void *)read_word(block+16);
    Word mutation=self->mutation;
    unsigned char done=0;
    for (size_t index=0;index<self->count && !done;++index) {
        unsigned char result=invoke(block,self->items[index],index,&done);
        if (self->mutation!=mutation) stop("Array mutated during enumeration",selector);
        if (result) return index;
    }
    return INT64_MAX;
}
 
static Word index_set_copy(struct Sequence *self,const char *selector) {
    return new_array(external_class(!strcmp(selector,"mutableCopy") ? "_OBJC_CLASS_$_NSMutableIndexSet" : "_OBJC_CLASS_$_NSIndexSet"),selector,self->items,self->count);
}
static void index_set_add(struct Sequence *self,const char *selector,Word index) {
    if (index>=INT64_MAX) stop("Invalid index",selector);
    size_t at=0; while (at<self->count && self->items[at]<index) ++at;
    if (at<self->count && self->items[at]==index) return;
    sequence_reserve(self,self->count+1,selector);
    memmove(self->items+at+1,self->items+at,(self->count-at)*sizeof(Word));
    self->items[at]=index; ++self->count; ++self->mutation;
}
static size_t index_set_lower_bound(struct Sequence *self,Word index) {
    size_t first=0,last=self->count;
    while (first<last) {
        size_t middle=first+(last-first)/2;
        if (self->items[middle]<index) first=middle+1; else last=middle;
    }
    return first;
}
static Word index_set_range_end(struct GuestRange range,const char *selector) {
    if (range.location>INT64_MAX || range.length>INT64_MAX-range.location) stop("Invalid index set range",selector);
    return range.location+range.length;
}
static void index_set_add_range(struct Sequence *self,const char *selector,struct GuestRange range) {
    Word end=index_set_range_end(range,selector);
    if (!range.length) return;
    size_t first=index_set_lower_bound(self,range.location),last=index_set_lower_bound(self,end);
    if (last-first==range.length) return;
    size_t retained=self->count-(last-first);
    if (range.length>SIZE_MAX-retained) stop("Index set too large",selector);
    sequence_reserve(self,retained+range.length,selector);
    memmove(self->items+first+range.length,self->items+last,(self->count-last)*sizeof(Word));
    for (size_t offset=0;offset<range.length;++offset) self->items[first+offset]=range.location+offset;
    self->count=retained+range.length; ++self->mutation;
}
static void index_set_remove_range(struct Sequence *self,const char *selector,struct GuestRange range) {
    Word end=index_set_range_end(range,selector);
    size_t first=index_set_lower_bound(self,range.location),last=index_set_lower_bound(self,end);
    if (first!=last) collection_remove_range(self,selector,(struct GuestRange){first,last-first});
}
static void index_set_remove_index(struct Sequence *self,const char *selector,Word index) {
    if (index>=INT64_MAX) stop("Invalid index",selector);
    index_set_remove_range(self,selector,(struct GuestRange){index,1});
}
static Word index_set_range(Word cls,const char *selector,struct GuestRange range) {
    struct Sequence *result=(void *)sequence_empty(cls);
    index_set_add_range(result,selector,range); return (Word)result;
}
static Word index_set_single(Word cls,const char *selector,Word index) { return index_set_range(cls,selector,(struct GuestRange){index,1}); }
static Word index_set_init_range(Word object,const char *selector,struct GuestRange range) {
    Word result=index_set_range(read_word(object),selector,range);
    struct Sequence *old=(void *)object; free(old->items); free(old); return result;
}
static Word index_set_init_single(Word object,const char *selector,Word index) { return index_set_init_range(object,selector,(struct GuestRange){index,1}); }
static Word index_set_contains_range(struct Sequence *self,const char *selector,struct GuestRange range) {
    Word end=index_set_range_end(range,selector);
    return index_set_lower_bound(self,end)-index_set_lower_bound(self,range.location)==range.length;
}
static Word index_set_first(struct Sequence *self) { return self->count ? self->items[0] : INT64_MAX; }
static Word index_set_last(struct Sequence *self) { return self->count ? self->items[self->count-1] : INT64_MAX; }
static Word index_set_next(struct Sequence *self,const char *selector,Word index) {
    int greater=!strcmp(selector,"indexGreaterThanIndex:");
    if (greater) { for (size_t i=0;i<self->count;++i) if (self->items[i]>index) return self->items[i]; }
    else { for (size_t i=self->count;i>0;--i) if (self->items[i-1]<index) return self->items[i-1]; }
    return INT64_MAX;
}
static Word index_set_contains(struct Sequence *self,const char *selector,Word index) {
    for (size_t i=0;i<self->count;++i) if (self->items[i]==index) return 1;
    return 0;
}
static void index_set_enumerate(struct Sequence *self,const char *selector,Word block) {
    if (!block) stop("Nil index enumeration block",selector);
    void (*invoke)(Word,Word,unsigned char *)=(void *)read_word(block+16);
    Word mutation=self->mutation; unsigned char done=0;
    for (size_t i=0;i<self->count && !done;++i) {
        invoke(block,self->items[i],&done);
        if (self->mutation!=mutation) stop("Index set mutated during enumeration",selector);
    }
}
static Word array_indexes_block(struct Sequence *self,const char *selector,Word block) {
    if (!block) stop("Nil predicate block",selector);
    unsigned char (*invoke)(Word,Word,Word,unsigned char *)=(void *)read_word(block+16);
    struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSIndexSet"));
    Word mutation=self->mutation; unsigned char done=0;
    for (size_t i=0;i<self->count && !done;++i) {
        unsigned char matches=invoke(block,self->items[i],i,&done);
        if (self->mutation!=mutation) stop("Array mutated during predicate",selector);
        if (matches) index_set_add(result,selector,i);
    }
    return (Word)result;
}
static void array_remove_indexes(struct Sequence *self,const char *selector,struct Sequence *indexes) {
    if (!indexes) stop("Nil index set",selector);
    if (indexes->count && indexes->items[indexes->count-1]>=self->count) stop("Array index out of range",selector);
    for (size_t i=indexes->count;i>0;--i) sequence_remove_at(self,selector,indexes->items[i-1]);
}
static int compare_block_objects(const void *first,const void *second,void *context) {
    Word block=(Word)context;
    intptr_t (*invoke)(Word,Word,Word)=(void *)read_word(block+16);
    intptr_t order=invoke(block,*(const Word *)first,*(const Word *)second);
    return order<0 ? -1 : order>0;
}
static void sort_by_block(Word *items,size_t count,Word block,const char *selector) {
    if (!block) stop("Nil comparison block",selector);
    sort_object_values(items,count,compare_block_objects,(void *)block,selector);
}
static Word array_sorted_block(struct Sequence *self,const char *selector,Word block) {
    struct Sequence *copy=(void *)new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,self->items,self->count);
    sort_by_block(copy->items,copy->count,block,selector);
    return (Word)copy;
}
static void array_sort_block(struct Sequence *self,const char *selector,Word block) {
    sort_by_block(self->items,self->count,block,selector);
    ++self->mutation;
}
static void array_sort_selector(struct Sequence *self,const char *selector,const char *comparison) {
    Word mutation=self->mutation;
    struct Sequence *sorted=(void *)sorted_array(self,selector,comparison);
    if (self->mutation!=mutation) stop("Collection mutated during sorting",selector);
    free(self->items); self->items=sorted->items; self->count=sorted->count; self->capacity=sorted->capacity;
    ++self->mutation; free(sorted);
}
static Word key_value(Word,const char *,Word);
static Word sort_descriptor_init(struct HostObject *self,const char *selector,Word key,Word ascending,const char *comparison) {
    self->content[0]=key ? text_copy(key,"copy") : 0;
    self->content[1]=ascending!=0;
    self->content[2]=(Word)(comparison ? comparison : "compare:");
    self->content[3]=key ? split_string(key,selector,copy_string(".")) : 0;
    return (Word)self;
}
static Word sort_descriptor_init_basic(struct HostObject *self,const char *selector,Word key,Word ascending) {
    return sort_descriptor_init(self,selector,key,ascending,"compare:");
}
static Word sort_descriptor_create(Word cls,const char *selector,Word key,Word ascending,const char *comparison) {
    return sort_descriptor_init((void *)host_allocate(cls),selector,key,ascending,comparison);
}
static Word sort_descriptor_create_basic(Word cls,const char *selector,Word key,Word ascending) {
    return sort_descriptor_create(cls,selector,key,ascending,"compare:");
}
static Word sort_descriptor_key(struct HostObject *self) { return self->content[0]; }
static Word sort_descriptor_ascending(struct HostObject *self) { return self->content[1]; }
static Word sort_descriptor_selector(struct HostObject *self) { return self->content[2]; }
static Word sort_descriptor_reverse(struct HostObject *self,const char *selector) {
    return sort_descriptor_create(self->isa,selector,self->content[0],!self->content[1],(void *)self->content[2]);
}
static Word sort_descriptor_value(struct HostObject *self,const char *selector,Word object) {
    struct Sequence *keys=(void *)self->content[3];
    for (size_t i=0;keys && i<keys->count && object;++i) object=key_value(object,selector,keys->items[i]);
    return object;
}
static Word sort_descriptor_compare(struct HostObject *self,const char *selector,Word first,Word second) {
    first=sort_descriptor_value(self,selector,first); second=sort_descriptor_value(self,selector,second);
    intptr_t order=0;
    if (first!=second) {
        if (!first) order=-1;
        else if (!second) order=1;
        else {
            const char *comparison=(void *)self->content[2]; Method method=find_implementation(first,comparison);
            if (!method) stop("Missing sort comparison method",comparison);
            intptr_t value=(intptr_t)method(first,comparison,second,0,0,0); order=value<0 ? -1 : value>0;
        }
    }
    return (Word)(self->content[1] ? order : -order);
}
struct DescriptorSort { struct Sequence *array,*descriptors; Word mutation,descriptor_mutation; Method *methods; };
static int compare_descriptor_objects(const void *first,const void *second,void *context) {
    struct DescriptorSort *sort=context;
    for (size_t i=0;i<sort->descriptors->count;++i) {
        struct HostObject *descriptor=(void *)sort->descriptors->items[i];
        intptr_t order=(intptr_t)sort->methods[i]((Word)descriptor,"compareObject:toObject:",*(const Word *)first,*(const Word *)second,0,0);
        if (sort->array->mutation!=sort->mutation || sort->descriptors->mutation!=sort->descriptor_mutation) stop("Collection mutated during sorting","sortUsingDescriptors:");
        if (order) return order<0 ? -1 : 1;
    }
    return 0;
}
static void array_sort_descriptors(struct Sequence *self,const char *selector,struct Sequence *descriptors) {
    if (!descriptors || !descriptors->count) return;
    Method *methods=malloc(descriptors->count*sizeof(*methods));
    if (!methods) stop("Allocation failed",selector);
    for (size_t i=0;i<descriptors->count;++i) {
        if (!class_inherits(read_word(descriptors->items[i]),external_class("_OBJC_CLASS_$_NSSortDescriptor"))) stop("Invalid sort descriptor",selector);
        methods[i]=find_implementation(descriptors->items[i],"compareObject:toObject:");
        if (!methods[i]) stop("Missing sort descriptor comparison",selector);
    }
    struct DescriptorSort sort={self,descriptors,self->mutation,descriptors->mutation,methods};
    sort_object_values(self->items,self->count,compare_descriptor_objects,&sort,selector); free(methods); ++self->mutation;
}
static Word array_sorted_descriptors(struct Sequence *self,const char *selector,struct Sequence *descriptors) {
    struct Sequence *copy=(void *)new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,self->items,self->count);
    array_sort_descriptors(copy,selector,descriptors); return (Word)copy;
}
static void array_remove_last(struct Sequence *self,const char *selector) {
    if (self->count) sequence_remove_at(self,selector,self->count-1);
}
static void data_enumerate_block(struct HostObject *self,const char *selector,Word block) {
    void (*invoke)(Word,const void *,struct GuestRange,unsigned char *)=(void *)read_word(block+16);
    unsigned char done=0;
    invoke(block,(void *)data_buffer(self),(struct GuestRange){0,self->content[0]},&done);
}

static Method collections_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSSortDescriptor")) {
        if (class_receiver) {
            if (!strcmp(selector,"sortDescriptorWithKey:ascending:")) return (Method)sort_descriptor_create_basic;
            if (!strcmp(selector,"sortDescriptorWithKey:ascending:selector:")) return (Method)sort_descriptor_create;
        } else {
            if (!strcmp(selector,"initWithKey:ascending:")) return (Method)sort_descriptor_init_basic;
            if (!strcmp(selector,"initWithKey:ascending:selector:")) return (Method)sort_descriptor_init;
            if (!strcmp(selector,"key")) return (Method)sort_descriptor_key;
            if (!strcmp(selector,"ascending")) return (Method)sort_descriptor_ascending;
            if (!strcmp(selector,"selector")) return (Method)sort_descriptor_selector;
            if (!strcmp(selector,"reversedSortDescriptor")) return (Method)sort_descriptor_reverse;
            if (!strcmp(selector,"compareObject:toObject:")) return (Method)sort_descriptor_compare;
            if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:")) return (Method)identity;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSIndexSet") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableIndexSet")) {
        if (class_receiver && !strcmp(selector,"indexSet")) return (Method)sequence_empty;
        if (class_receiver && !strcmp(selector,"indexSetWithIndex:")) return (Method)index_set_single;
        if (class_receiver && !strcmp(selector,"indexSetWithIndexesInRange:")) return (Method)index_set_range;
        if (!class_receiver) {
            if (!strcmp(selector,"initWithIndex:")) return (Method)index_set_init_single;
            if (!strcmp(selector,"initWithIndexesInRange:")) return (Method)index_set_init_range;
            if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:") || !strcmp(selector,"mutableCopy")) return (Method)index_set_copy;
            if (!strcmp(selector,"count")) return (Method)array_count;
            if (!strcmp(selector,"firstIndex")) return (Method)index_set_first;
            if (!strcmp(selector,"lastIndex")) return (Method)index_set_last;
            if (!strcmp(selector,"containsIndex:")) return (Method)index_set_contains;
            if (!strcmp(selector,"containsIndexesInRange:")) return (Method)index_set_contains_range;
            if (!strcmp(selector,"indexGreaterThanIndex:") || !strcmp(selector,"indexLessThanIndex:")) return (Method)index_set_next;
            if (!strcmp(selector,"enumerateIndexesUsingBlock:")) return (Method)index_set_enumerate;
            if (!strcmp(receiver,"_OBJC_CLASS_$_NSMutableIndexSet")) {
                if (!strcmp(selector,"addIndex:")) return (Method)index_set_add;
                if (!strcmp(selector,"addIndexesInRange:")) return (Method)index_set_add_range;
                if (!strcmp(selector,"removeIndex:")) return (Method)index_set_remove_index;
                if (!strcmp(selector,"removeIndexesInRange:")) return (Method)index_set_remove_range;
                if (!strcmp(selector,"removeAllIndexes")) return (Method)sequence_clear;
            }
        }
    }
    if (!strcmp(selector,"performSelector:") || !strcmp(selector,"performSelector:withObject:") ||
        !strcmp(selector,"performSelector:withObject:withObject:")) return (Method)perform_selector;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSPredicate")) {
        if (class_receiver && !strcmp(selector,"predicateWithFormat:")) return (Method)collection_predicate;
        if (class_receiver && !strcmp(selector,"predicateWithBlock:")) return (Method)collection_predicate_block;
        if (class_receiver && !strcmp(selector,"predicateWithValue:")) return (Method)collection_predicate_value;
        if (!class_receiver && !strcmp(selector,"evaluateWithObject:")) return (Method)collection_predicate_evaluate;
        if (!class_receiver && !strcmp(selector,"evaluateWithObject:substitutionVariables:")) return (Method)collection_predicate_evaluate_variables;
        if (!class_receiver && ((struct HostObject *)object)->content[1]==8) {
            if (!strcmp(selector,"predicateBlock")) return (Method)collection_predicate_block_get;
            if (!strcmp(selector,"dealloc")) return (Method)collection_predicate_block_dealloc;
        }
    }
    int array=!strcmp(receiver,"_OBJC_CLASS_$_NSArray") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableArray");
    int set=!strcmp(receiver,"_OBJC_CLASS_$_NSSet") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableSet");
    if (array || set) {
        if (class_receiver && (!strcmp(selector,"arrayWithObject:") || !strcmp(selector,"setWithObject:"))) return (Method)collection_one;
        if (class_receiver && set && !strcmp(selector,"setWithSet:")) return (Method)collection_from_set;
        if (!class_receiver) {
            if (!strcmp(selector,"hash")) return (Method)array_count;
            if (!strcmp(selector,"initWithArray:")) return (Method)collection_init_array;
            if (set && !strcmp(selector,"initWithSet:")) return (Method)collection_init_set;
            if (set && !strcmp(selector,"member:")) return (Method)collection_set_member;
            if (set && !strcmp(selector,"intersectsSet:")) return (Method)collection_sets_intersect;
            if (!strcmp(receiver,"_OBJC_CLASS_$_NSMutableSet") && !strcmp(selector,"setSet:")) return (Method)collection_set_contents;
            if (!strcmp(receiver,"_OBJC_CLASS_$_NSMutableSet") && !strcmp(selector,"intersectSet:")) return (Method)collection_set_intersection;
            if (array && !strcmp(selector,"subarrayWithRange:")) return (Method)collection_subarray;
            if (array && !strcmp(selector,"filteredArrayUsingPredicate:")) return (Method)collection_filter;
            if (array && !strcmp(selector,"enumerateObjectsUsingBlock:")) return (Method)array_enumerate_block;
            if (set && !strcmp(selector,"enumerateObjectsUsingBlock:")) return (Method)set_enumerate_block;
            if (array && !strcmp(selector,"indexOfObjectPassingTest:")) return (Method)array_index_block;
            if (array && !strcmp(selector,"indexesOfObjectsPassingTest:")) return (Method)array_indexes_block;
            if (array && !strcmp(selector,"sortedArrayUsingComparator:")) return (Method)array_sorted_block;
            if (array && !strcmp(selector,"sortedArrayUsingDescriptors:")) return (Method)array_sorted_descriptors;
            if (!strcmp(receiver,"_OBJC_CLASS_$_NSMutableArray")) {
                if (!strcmp(selector,"addObjectsFromArray:")) return (Method)collection_add_array;
                if (!strcmp(selector,"removeObjectsInArray:")) return (Method)collection_remove_array;
                if (!strcmp(selector,"setArray:")) return (Method)collection_set_contents;
                if (!strcmp(selector,"filterUsingPredicate:")) return (Method)collection_filter_mutable;
                if (!strcmp(selector,"removeObjectsInRange:")) return (Method)collection_remove_range;
                if (!strcmp(selector,"removeObjectsAtIndexes:")) return (Method)array_remove_indexes;
                if (!strcmp(selector,"replaceObjectAtIndex:withObject:")) return (Method)collection_replace;
                if (!strcmp(selector,"sortUsingComparator:")) return (Method)array_sort_block;
                if (!strcmp(selector,"sortUsingSelector:")) return (Method)array_sort_selector;
                if (!strcmp(selector,"sortUsingDescriptors:")) return (Method)array_sort_descriptors;
                if (!strcmp(selector,"removeLastObject")) return (Method)array_remove_last;
            }
        }
    }
    int mutable=!strcmp(receiver,"_OBJC_CLASS_$_NSMutableDictionary");
    if (mutable || !strcmp(receiver,"_OBJC_CLASS_$_NSDictionary")) {
        if (class_receiver) {
            if (!strcmp(selector,"dictionaryWithDictionary:")) return (Method)dictionary_from_dictionary;
            if (!strcmp(selector,"dictionaryWithCapacity:")) return (Method)empty_map;
            if (!strcmp(selector,"dictionaryWithObject:forKey:")) return (Method)dictionary_one;
        } else {
            if (!strcmp(selector,"hash")) return (Method)map_count;
            if (!strcmp(selector,"initWithDictionary:")) return (Method)dictionary_init_dictionary;
            if (!strcmp(selector,"allValues")) return (Method)dictionary_values;
            if (!strcmp(selector,"enumerateKeysAndObjectsUsingBlock:")) return (Method)dictionary_enumerate_block;
            if (!strcmp(selector,"valueForKey:")) return (Method)map_get;
            if (mutable) {
                if (!strcmp(selector,"setObject:forKeyedSubscript:") || !strcmp(selector,"setValue:forKey:")) return (Method)dictionary_subscript;
                if (!strcmp(selector,"removeObjectForKey:")) return (Method)dictionary_remove;
                if (!strcmp(selector,"removeAllObjects")) return (Method)dictionary_clear;
                if (!strcmp(selector,"addEntriesFromDictionary:")) return (Method)dictionary_merge;
            }
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSData")) {
        if (class_receiver && !strcmp(selector,"data")) return (Method)data_empty;
        if (!class_receiver) {
            if (!strcmp(selector,"subdataWithRange:")) return (Method)data_subrange;
            if (!strcmp(selector,"getBytes:range:")) return (Method)data_get_range;
            if (!strcmp(selector,"getBytes:length:")) return (Method)data_get_length;
            if (!strcmp(selector,"isEqualToData:")) return (Method)data_equal;
            if (!strcmp(selector,"enumerateByteRangesUsingBlock:")) return (Method)data_enumerate_block;
        }
    }
    return NULL;
}
