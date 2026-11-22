 
static unsigned char cf_string_cstring(Word,char *,intptr_t,uint32_t);
static Word cf_array_create(Word allocator,const Word *values,intptr_t count,const void *callbacks) {
    if (allocator || count<0) stop("Invalid arguments","CFArrayCreate");
    if (callbacks) stop("Unsupported callbacks","CFArrayCreate");
    return new_array(external_class("_OBJC_CLASS_$_NSArray"),"CFArrayCreate",values,(size_t)count);
}
static Word cf_data_create(Word allocator,const void *bytes,intptr_t count) {
    if (allocator || count<0) stop("Invalid arguments","CFDataCreate");
    return data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"CFDataCreate",bytes,(size_t)count);
}
static Word cf_string_create(Word allocator,const uint16_t *units,intptr_t length) {
    if (allocator || length<0) stop("Invalid arguments","CFStringCreateWithCharacters");
    return text_from_units(external_class("_OBJC_CLASS_$_NSString"),units,(size_t)length);
}
 
static Word cf_null_allocator_object;
static Word cf_null_allocator=(Word)&cf_null_allocator_object;
static Word cf_data_no_copy(Word allocator,const void *bytes,intptr_t length,Word deallocator) {
    if (allocator || length<0 || (!bytes && length) || deallocator!=cf_null_allocator) return 0;
    struct HostObject *data=calloc(1,sizeof(*data));
    if (!data) return 0;
    data->isa=external_class("_OBJC_CLASS_$_NSData"); data->content[0]=(Word)length;
    data->content[1]=(Word)bytes; data->content[3]=0x4e444154; return (Word)data;
}
static Word cf_string_no_copy(Word allocator,const char *bytes,uint32_t encoding,Word deallocator) {
    if (allocator || !bytes || deallocator!=cf_null_allocator) return 0;
    Word ns_encoding=encoding==0x08000100 ? 4 : encoding==0x0600 ? 1 : encoding==0x0201 ? 5 : 0;
    return ns_encoding ? text_decode(external_class("_OBJC_CLASS_$_NSString"),bytes,strlen(bytes),ns_encoding) : 0;
}
static Word cf_plist_create(Word allocator,struct HostObject *data,Word options,Word *format,Word *error) {
    if (error) *error=0;
    if (allocator || !data || options || data->content[0]>UINT32_MAX) return 0;
    plist_t root=NULL; plist_format_t parsed=PLIST_FORMAT_NONE;
    if (plist_from_memory((void *)data_buffer(data),(uint32_t)data->content[0],&root,&parsed)!=PLIST_ERR_SUCCESS) {
        if (error) serialization_error(error,"Invalid property list"); return 0;
    }
    Word result=plist_object(root,0); plist_free(root);
    if (format) *format=parsed==PLIST_FORMAT_BINARY ? 200 : 100;
    return result;
}
static Word cf_plist_xml(Word allocator,struct HostObject *data,Word options,Word *error_string) {
    Word error=0,result=cf_plist_create(allocator,data,options,NULL,&error);
    if (error_string) *error_string=result ? 0 : copy_string("Invalid property list");
    return result;
}
static Word cf_dictionary_get(struct GuestMap *map,Word key) { return map_get(map,"CFDictionaryGetValue",key); }
static Word cf_string_type(void) { return 1; }
static Word cf_data_type(void) { return 2; }
static double cf_absolute_time(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME,&now)) stop("Clock query failed","CFAbsoluteTimeGetCurrent");
    return (double)now.tv_sec-978307200.0+(double)now.tv_nsec*1e-9;
}
static Word cf_type(Word object) {
    if (!object) return 0;
    Word cls=read_word(object);
    if (cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) return cf_string_type();
    if (cls==external_class("_OBJC_CLASS_$_NSData") || cls==external_class("_OBJC_CLASS_$_NSMutableData")) return 2;
    if (cls==external_class("_OBJC_CLASS_$_NSDictionary") || cls==external_class("_OBJC_CLASS_$_NSMutableDictionary")) return 3;
    if (cls==external_class("_OBJC_CLASS_$_NSArray") || cls==external_class("_OBJC_CLASS_$_NSMutableArray")) return 4;
    if (object==(Word)&bool_numbers[0] || object==(Word)&bool_numbers[1]) return 5;
    if (cls==external_class("_OBJC_CLASS_$_NSNumber") || cls==decimal_number_class) return 6;
    return cls;
}
static Word cf_dynamic_symbol(const char *name) {
    if (!strcmp(name,"kCFAllocatorNull")) return (Word)&cf_null_allocator;
    if (!strcmp(name,"CFDataCreateWithBytesNoCopy")) return (Word)cf_data_no_copy;
    if (!strcmp(name,"CFPropertyListCreateWithData")) return (Word)cf_plist_create;
    if (!strcmp(name,"CFPropertyListCreateFromXMLData")) return (Word)cf_plist_xml;
    if (!strcmp(name,"CFStringCreateWithCStringNoCopy")) return (Word)cf_string_no_copy;
    if (!strcmp(name,"CFDictionaryGetValue")) return (Word)cf_dictionary_get;
    if (!strcmp(name,"CFGetTypeID")) return (Word)cf_type;
    if (!strcmp(name,"CFStringGetTypeID")) return (Word)cf_string_type;
    if (!strcmp(name,"CFDataGetTypeID")) return (Word)cf_data_type;
    if (!strcmp(name,"CFAbsoluteTimeGetCurrent")) return (Word)cf_absolute_time;
    if (!strcmp(name,"CFStringGetCString")) return (Word)cf_string_cstring;
    if (!strcmp(name,"CFRelease")) return (Word)cf_release;
    return 0;
}
static unsigned char cf_string_cstring(Word string,char *output,intptr_t capacity,uint32_t encoding) {
    if (capacity<0) return 0;
    Word ns_encoding=encoding==0x08000100 ? 4 : encoding==0x0600 ? 1 : encoding==0x0201 ? 5 : 0;
    if (!ns_encoding) stop("Unsupported encoding","CFStringGetCString");
    return (unsigned char)text_cstring(string,"CFStringGetCString",output,(size_t)capacity,ns_encoding);
}
struct UuidBytes { unsigned char bytes[16]; };
static struct CoreUuid *uuid_from_bytes(Word allocator,struct UuidBytes bytes) {
    struct CoreUuid *result=create_uuid(allocator);
    memcpy(result->bytes,bytes.bytes,16);
    return result;
}
static unsigned char cf_equal(Word first,Word second) {
    if (first==second) return 1;
    if (!first || !second) return 0;
    if (read_word(first)==(Word)&uuid_tag && read_word(second)==(Word)&uuid_tag)
        return !memcmp(((struct CoreUuid *)first)->bytes,((struct CoreUuid *)second)->bytes,16);
    if (read_word(first)==external_class("_OBJC_CLASS_$_NSData") && read_word(second)==external_class("_OBJC_CLASS_$_NSData"))
        return data_equal((void *)first,"CFEqual",(void *)second);
    return (unsigned char)same_objects(first,second);
}
static Word cf_hash(Word object) {
    if (!object) stop("Nil object","CFHash");
    Word cls=read_word(object);
    const unsigned char *bytes=NULL;
    size_t size=0;
    struct TextView text={0};
    if (cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) {
        text=text_view(object,"CFHash"); bytes=(const void *)text.units; size=text.length*2;
    } else if (cls==external_class("_OBJC_CLASS_$_NSData")) { bytes=(void *)data_buffer((void *)object); size=read_word(object+8); }
    else if (cls==(Word)&uuid_tag) { bytes=((struct CoreUuid *)object)->bytes; size=16; }
    else if (cls==external_class("_OBJC_CLASS_$_NSNumber") || cls==decimal_number_class) return numeric_integer((void *)object);
    else return object;
    Word hash=14695981039346656037UL;
    for (size_t i=0;i<size;++i) hash=(hash^bytes[i])*1099511628211UL;
    free(text.owned);
    return hash;
}
static Word cf_description(Word object) { return object_description(object,"CFCopyDescription"); }
static Word cf_set_mutable(Word allocator,intptr_t capacity,const Word *callbacks) {
    if (allocator || capacity<0) stop("Invalid arguments","CFSetCreateMutable");
    /* Object-equality, nonretaining sets bridge directly to NSMutableSet.
     * Raw-pointer or custom ownership callbacks need their own storage policy. */
    if (!callbacks || callbacks[0] || callbacks[1] || callbacks[2] ||
        (callbacks[3] && callbacks[3]!=(Word)cf_description) || callbacks[4]!=(Word)cf_equal ||
        (callbacks[5] && callbacks[5]!=(Word)cf_hash)) stop("Unsupported set callbacks","CFSetCreateMutable");
    struct Sequence *set=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSMutableSet"));
    sequence_reserve(set,(size_t)capacity,"CFSetCreateMutable"); return (Word)set;
}
static Word cf_url_percent_escape(Word allocator,Word source,Word leave,Word force,uint32_t encoding) {
    if (allocator || encoding!=0x08000100) stop("Unsupported URL escape allocator or encoding","CFURLCreateStringByAddingPercentEscapes");
    if (!source) return 0;
    const char *base=";/?:@&=+$,!~*'()#%[]",*extra=leave ? string_bytes(leave,"URL escaping") : "";
    size_t length=strlen(base)+strlen(extra)+1; char *allowed=malloc(length);
    if (!allowed) stop("Allocation failed","URL escaping");
    snprintf(allowed,length,"%s%s",base,extra);
    Word result=percent_encode(source,allowed,force ? string_bytes(force,"URL escaping") : NULL);
    free(allowed); return result;
}
static unsigned char cf_string_transform(Word string,struct GuestRange *range,Word transform,unsigned char reverse) {
    if (!string || !transform || read_word(string)!=external_class("_OBJC_CLASS_$_NSMutableString")) return 0;
    struct TextView text=text_view(string,"CFStringTransform");
    struct GuestRange selected=range ? *range : (struct GuestRange){0,text.length};
    if (selected.location>text.length || selected.length>text.length-selected.location || selected.length>INT32_MAX) {
        free(text.owned); return 0;
    }
    JNIEnv *env=attached_environment("CFStringTransform"); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"transformText","(Ljava/lang/String;Ljava/lang/String;Z)Ljava/lang/String;");
    jstring input=(*env)->NewString(env,(const jchar *)text.units+selected.location,(jsize)selected.length);
    jstring identifier=http_java_string(env,transform);
    jstring output=input && identifier && method ? (*env)->CallObjectMethod(env,activity,method,input,identifier,(jboolean)reverse) : NULL;
    unsigned char success=0;
    if (!(*env)->ExceptionCheck(env) && output) {
        size_t length=(size_t)(*env)->GetStringLength(env,output);
        Word replacement=string_from_java(env,output,"CFStringTransform");
        text_mutate_range(string,"CFStringTransform",selected,replacement);
        free((void *)replacement); if (range) range->length=length; success=1;
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (output) (*env)->DeleteLocalRef(env,output);
    if (identifier) (*env)->DeleteLocalRef(env,identifier);
    if (input) (*env)->DeleteLocalRef(env,input);
    (*env)->DeleteLocalRef(env,cls); free(text.owned); return success;
}
static Word cf_url_percent_replace(Word allocator,Word source,Word leave) {
    if (allocator || !source) return 0;
    if (!leave) return text_copy(source,"copy");
    struct TextView keep=text_view(leave,"URL percent decoding");
    struct TextView original=text_view(source,"URL percent decoding"); size_t length=0;
    unsigned char *text=(unsigned char *)text_utf8(original.units,original.length,&length); free(original.owned);
    if (!text) { free(keep.owned); return 0; }
    unsigned char *output=malloc(length+1); if (!output) { free(text); free(keep.owned); return 0; }
    size_t position=0,written=0; int valid=1;
    while (position<length) {
        if (text[position]!='%') { output[written++]=text[position++]; continue; }
        unsigned char bytes[4]; size_t count=1;
        if (length-position<3 || url_hex(text[position+1])<0 || url_hex(text[position+2])<0) { valid=0; break; }
        bytes[0]=(unsigned char)((url_hex(text[position+1])<<4)|url_hex(text[position+2]));
        if (bytes[0]>=0xc2 && bytes[0]<=0xdf) count=2;
        else if (bytes[0]>=0xe0 && bytes[0]<=0xef) count=3;
        else if (bytes[0]>=0xf0 && bytes[0]<=0xf4) count=4;
        else if (bytes[0]>=0x80) { valid=0; break; }
        if (3*count>length-position) { valid=0; break; }
        for (size_t i=1;i<count;++i) {
            size_t cursor=position+3*i;
            if (length-cursor<3 || text[cursor]!='%' || url_hex(text[cursor+1])<0 || url_hex(text[cursor+2])<0) { valid=0; break; }
            bytes[i]=(unsigned char)((url_hex(text[cursor+1])<<4)|url_hex(text[cursor+2]));
        }
        if (!valid) break;
        const unsigned char *cursor=bytes; uint32_t code=0;
        if (decode_utf8(&cursor,bytes+count,&code)!=1) { valid=0; break; }
        int escaped=0;
        for (size_t i=0;i<keep.length;++i) {
            uint32_t ch=keep.units[i];
            if (ch>=0xd800 && ch<=0xdbff && i+1<keep.length && keep.units[i+1]>=0xdc00 && keep.units[i+1]<=0xdfff)
                { ch=0x10000+((ch-0xd800)<<10)+(keep.units[i+1]-0xdc00); ++i; }
            if (ch==code) { escaped=1; break; }
        }
        size_t copy=escaped ? 3*count : count;
        memcpy(output+written,escaped ? text+position : bytes,copy); written+=copy; position+=3*count;
    }
    Word result=valid ? text_decode(external_class("_OBJC_CLASS_$_NSString"),output,written,4) : 0;
    free(output); free(text); free(keep.owned); return result;
}
static Word cf_best_string_language(Word string,struct GuestRange range) {
    if (!string) return 0;
    struct TextView text=text_view(string,"CFStringTokenizerCopyBestStringLanguage");
    if (range.location>text.length || range.length>text.length-range.location || range.length>INT32_MAX) { free(text.owned); return 0; }
    JNIEnv *env=attached_environment("CFStringTokenizerCopyBestStringLanguage"); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"textLanguage","(Ljava/lang/String;)Ljava/lang/String;");
    jstring input=(*env)->NewString(env,(const jchar *)text.units+range.location,(jsize)range.length);
    jstring output=input && method ? (*env)->CallObjectMethod(env,activity,method,input) : NULL;
    Word language=!(*env)->ExceptionCheck(env) && output ? string_from_java(env,output,"String language") : 0;
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (output) (*env)->DeleteLocalRef(env,output);
    if (input) (*env)->DeleteLocalRef(env,input);
    (*env)->DeleteLocalRef(env,cls); free(text.owned); return language;
}
static unsigned char cf_number_float(struct HostObject *number) { return number->isa==decimal_number_class || number->content[1]!=0; }
static const char *selector_from_string(Word string) { return string_bytes(string,"NSSelectorFromString"); }
static Word string_from_selector(const char *selector) { return selector ? copy_string(selector) : 0; }
static Word cf_retain(Word object) {
    if (!object) stop("Nil object","CFRetain");
    if (read_word(object)==(Word)&uuid_tag) __atomic_add_fetch(&((struct CoreUuid *)object)->references,1,__ATOMIC_RELAXED);
    else if (read_word(object)==(Word)&secure_transport_tag) secure_transport_retain(object);
    else if (read_word(object)==(Word)&trust_tag) trust_retain((void *)object);
    else if (drawing_resource_retain(object) || http_message_retain(object) || runloop_source_retain(object)) return object;
    return object;
}

static Word resolve_core_import(const char *name) {
    if (!strcmp(name,"_CFAutorelease")) return (Word)core_autorelease;
    if (!strcmp(name,"__NSDictionaryOfVariableBindings")) return (Word)darwin_variable_bindings;
    if (!strcmp(name,"_system")) return (Word)system;
    Word dynamic=cf_dynamic_symbol(name+1);
    if (dynamic) return dynamic;
    if (!strcmp(name,"_CFRetain")) return (Word)cf_retain;
    if (!strcmp(name,"_CFEqual")) return (Word)cf_equal;
    if (!strcmp(name,"_CFHash")) return (Word)cf_hash;
    if (!strcmp(name,"_CFCopyDescription")) return (Word)cf_description;
    if (!strcmp(name,"_CFArrayCreate")) return (Word)cf_array_create;
    if (!strcmp(name,"_CFSetCreateMutable")) return (Word)cf_set_mutable;
    if (!strcmp(name,"_CFURLCreateStringByAddingPercentEscapes")) return (Word)cf_url_percent_escape;
    if (!strcmp(name,"_CFURLCreateStringByReplacingPercentEscapes")) return (Word)cf_url_percent_replace;
    if (!strcmp(name,"_CFStringTransform")) return (Word)cf_string_transform;
    if (!strcmp(name,"_CFStringTokenizerCopyBestStringLanguage")) return (Word)cf_best_string_language;
    if (!strcmp(name,"_CFArrayGetCount")) return (Word)array_count;
    if (!strcmp(name,"_CFDataCreate")) return (Word)cf_data_create;
    if (!strcmp(name,"_CFDataGetBytePtr")) return (Word)data_buffer;
    if (!strcmp(name,"_CFDataGetLength")) return (Word)array_count;
    if (!strcmp(name,"_CFStringCreateWithCharacters")) return (Word)cf_string_create;
    if (!strcmp(name,"_CFStringGetCString")) return (Word)cf_string_cstring;
    if (!strcmp(name,"_CFNumberIsFloatType")) return (Word)cf_number_float;
    if (!strcmp(name,"_CFUUIDCreateFromUUIDBytes")) return (Word)uuid_from_bytes;
    if (!strcmp(name,"_NSSelectorFromString")) return (Word)selector_from_string;
    if (!strcmp(name,"_NSStringFromSelector")) return (Word)string_from_selector;
    if (!strcmp(name,"_utf8_nextCharSafeBody")) return (Word)utf8_next_safe;
    return 0;
}
