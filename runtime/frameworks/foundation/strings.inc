/* Shared NSString/NSMutableString operations. Range results use two integer
 * registers, as required by the Apple ARM64 NSRange ABI. */
struct GuestRange { size_t location,length; };
static const struct GuestRange no_range={INT64_MAX,0};

static void check_range(struct GuestRange range,size_t length,const char *operation) {
    if (range.location>length || range.length>length-range.location) stop("String range out of bounds",operation);
}

static Word text_copy(Word self,const char *selector) {
    int mutable=!strcmp(selector,"mutableCopy") || !strcmp(selector,"mutableCopyWithZone:");
    if (!mutable && read_word(self)==external_class("_OBJC_CLASS_$_NSString")) return self;
    struct TextView text=text_view(self,selector);
    Word result=text_from_units(external_class(mutable ? "_OBJC_CLASS_$_NSMutableString" : "_OBJC_CLASS_$_NSString"),text.units,text.length);
    free(text.owned);
    return result;
}

static Word text_from_string(Word cls,const char *selector,Word source) {
    struct TextView text=text_view(source,selector);
    Word result=text_from_units(cls,text.units,text.length);
    free(text.owned);
    return result;
}

static Word text_init_string(Word self,const char *selector,Word source) {
    Word result=text_from_string(read_word(self),selector,source);
    free((void *)self);
    return result;
}
static Word text_init_empty(Word self) {
    Word result=mutable_string(read_word(self));
    free((void *)self);
    return result;
}

static Word text_init_utf8(Word self,const char *selector,const char *source) {
    Word result=string_from_utf8(read_word(self),selector,source);
    free((void *)self);
    return result;
}

static Word text_from_characters(Word cls,const char *selector,const uint16_t *source,size_t length) {
    return text_from_units(cls,source,length);
}

static Word text_from_cstring(Word cls,const char *selector,const char *source,Word encoding) {
    return source ? text_decode(cls,source,strlen(source),encoding) : 0;
}

static Word text_init_bytes(Word self,const char *selector,const void *source,size_t length,Word encoding) {
    Word result=text_decode(read_word(self),source,length,encoding);
    free((void *)self);
    return result;
}

static Word text_affix(Word self,const char *selector,Word other) {
    int suffix=!strcmp(selector,"hasSuffix:");
    if (read_word(self+8)==0x7c8 && other && read_word(other+8)==0x7c8) {
        size_t a=read_word(self+24),b=read_word(other+24);
        const char *text=(void *)read_word(self+16),*part=(void *)read_word(other+16);
        return b<=a && !memcmp(text+(suffix ? a-b : 0),part,b);
    }
    struct TextView text=text_view(self,selector),part=text_view(other,selector);
    int found=part.length<=text.length && !memcmp(text.units+(suffix ? text.length-part.length : 0),part.units,part.length*2);
    free(text.owned); free(part.owned);
    return found;
}

static Word text_subrange(Word self,const char *selector,struct GuestRange range) {
    struct TextView text=text_view(self,selector);
    check_range(range,text.length,selector);
    Word result=text_from_units(external_class("_OBJC_CLASS_$_NSString"),text.units+range.location,range.length);
    free(text.owned);
    return result;
}

static Word text_suffix(Word self,const char *selector,size_t start) {
    size_t length=string_length(self,selector);
    if (start>length) stop("String index out of bounds",selector);
    return text_subrange(self,selector,(struct GuestRange){start,length-start});
}

static uint16_t text_character(Word self,const char *selector,size_t index) {
    struct TextView text=text_view(self,selector);
    if (index>=text.length) { free(text.owned); stop("String index out of bounds",selector); }
    uint16_t result=text.units[index];
    free(text.owned);
    return result;
}

static void text_get_characters(Word self,const char *selector,uint16_t *output,struct GuestRange range) {
    struct TextView text=text_view(self,selector);
    check_range(range,text.length,selector);
    if (range.length) memcpy(output,text.units+range.location,range.length*2);
    free(text.owned);
}

static jstring java_string(JNIEnv *env,struct TextView text,const char *operation) {
    if (text.length>INT32_MAX) stop("String too large for Java",operation);
    jstring result=(*env)->NewString(env,text.units,(jsize)text.length);
    if (!result) stop("Allocation failed",operation);
    return result;
}

static Word string_from_java(JNIEnv *env,jstring value,const char *operation) {
    if (!value || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        stop("Java string operation failed",operation);
    }
    jsize length=(*env)->GetStringLength(env,value);
    const jchar *units=(*env)->GetStringChars(env,value,NULL);
    if (!units) stop("Allocation failed",operation);
    Word result=text_from_units(external_class("_OBJC_CLASS_$_NSString"),units,(size_t)length);
    (*env)->ReleaseStringChars(env,value,units);
    return result;
}

static Word text_case(Word self,const char *selector) {
    int mode=!strcmp(selector,"lowercaseString") ? 0 : !strcmp(selector,"uppercaseString") ? 1 : 2;
    struct TextView text=text_view(self,selector);
    int ascii=1;
    for (size_t i=0;i<text.length;++i) if (!text.units[i] || text.units[i]>=128) { ascii=0; break; }
    if (ascii && mode!=2) {
        Word result=new_string(text.length);
        char *out=(void *)read_word(result+16);
        for (size_t i=0;i<text.length;++i) {
            unsigned ch=text.units[i];
            if (!mode && ch>='A' && ch<='Z') ch+='a'-'A';
            if (mode && ch>='a' && ch<='z') ch-='a'-'A';
            out[i]=(char)ch;
        }
        free(text.owned);
        return result;
    }
    JNIEnv *env=attached_environment(selector);
    jstring input=java_string(env,text,selector);
    free(text.owned);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"transformText","(Ljava/lang/String;I)Ljava/lang/String;");
    jstring value=(*env)->CallObjectMethod(env,activity,method,input,mode);
    Word result=string_from_java(env,value,selector);
    (*env)->DeleteLocalRef(env,value); (*env)->DeleteLocalRef(env,input); (*env)->DeleteLocalRef(env,type);
    return result;
}

static Word text_compare_case(Word self,const char *selector,Word other) {
    struct TextView a=text_view(self,selector),b=text_view(other,selector);
    JNIEnv *env=attached_environment(selector);
    jstring first=java_string(env,a,selector),second=java_string(env,b,selector);
    jclass type=(*env)->FindClass(env,"java/lang/String");
    jmethodID compare=(*env)->GetMethodID(env,type,"compareToIgnoreCase","(Ljava/lang/String;)I");
    jint order=(*env)->CallIntMethod(env,first,compare,second);
    free(a.owned); free(b.owned);
    (*env)->DeleteLocalRef(env,first); (*env)->DeleteLocalRef(env,second); (*env)->DeleteLocalRef(env,type);
    return (Word)(intptr_t)(order<0 ? -1 : order>0);
}

static struct GuestRange text_find_range(Word self,const char *selector,Word needle,Word options,struct GuestRange range) {
     
    if (options&~(Word)(1|2|4|8|1024)) stop("Unsupported string search options",selector);
    struct TextView text=text_view(self,selector),part=text_view(needle,selector);
    check_range(range,text.length,selector);
    struct GuestRange found=no_range;
    if (!part.length) { free(text.owned); free(part.owned); return found; }
    if ((options&1) || (options&1024)) {
        JNIEnv *env=attached_environment(selector);
        jstring a=java_string(env,text,selector),b=java_string(env,part,selector);
        jclass type=(*env)->GetObjectClass(env,activity);
        jmethodID method=(*env)->GetMethodID(env,type,"findText","(Ljava/lang/String;Ljava/lang/String;III)J");
        jlong packed=(*env)->CallLongMethod(env,activity,method,a,b,(jint)options,(jint)range.location,(jint)range.length);
        (*env)->DeleteLocalRef(env,a); (*env)->DeleteLocalRef(env,b); (*env)->DeleteLocalRef(env,type);
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Invalid string search",selector); }
        if (packed>=0) found=(struct GuestRange){(uint64_t)packed>>32,(uint32_t)packed};
    } else if (part.length<=range.length) {
        size_t first=range.location,last=first+range.length-part.length;
        size_t cursor=options&4 ? last : first;
        for (;;) {
            if (!memcmp(text.units+cursor,part.units,part.length*2)) { found=(struct GuestRange){cursor,part.length}; break; }
            if ((options&8) || ((options&4) ? cursor==first : cursor==last)) break;
            if (options&4) --cursor; else ++cursor;
        }
    }
    free(text.owned); free(part.owned);
    return found;
}

static struct GuestRange text_find_options(Word self,const char *selector,Word needle,Word options) {
    return text_find_range(self,selector,needle,options,(struct GuestRange){0,string_length(self,selector)});
}
static struct GuestRange text_find(Word self,const char *selector,Word needle) { return text_find_options(self,selector,needle,0); }
static Word text_contains(Word self,const char *selector,Word needle) { return text_find(self,selector,needle).location!=INT64_MAX; }

static Word text_replacing_range(Word self,const char *selector,struct GuestRange range,Word replacement) {
    struct TextView text=text_view(self,selector),insert=text_view(replacement,selector);
    check_range(range,text.length,selector);
    size_t kept=text.length-range.length;
    if (insert.length>SIZE_MAX/2-kept-1) stop("String too large",selector);
    size_t length=kept+insert.length;
    uint16_t *units=malloc((length+1)*2);
    if (!units) stop("Allocation failed",selector);
    memcpy(units,text.units,range.location*2);
    memcpy(units+range.location,insert.units,insert.length*2);
    memcpy(units+range.location+insert.length,text.units+range.location+range.length,(text.length-range.location-range.length)*2);
    Word result=text_from_units(external_class("_OBJC_CLASS_$_NSString"),units,length);
    free(units); free(text.owned); free(insert.owned);
    return result;
}

static void text_mutate_range(Word self,const char *selector,struct GuestRange range,Word replacement) {
    Word result=text_replacing_range(self,selector,range,replacement);
    struct TextView text=text_view(result,selector);
    assign_text((void *)self,text.units,text.length,selector);
    free(text.owned); free((void *)result);
}
static void text_delete_range(Word self,const char *selector,struct GuestRange range) {
    Word empty=new_string(0);
    text_mutate_range(self,selector,range,empty);
    free((void *)empty);
}
static void text_set(Word self,const char *selector,Word source) {
    struct TextView text=text_view(source,selector);
    assign_text((void *)self,text.units,text.length,selector);
    free(text.owned);
}

static Word text_delete_path(Word self,const char *selector) {
    const char *path=string_bytes(self,selector);
    size_t length=strlen(path);
    while (length>1 && path[length-1]=='/') --length;
    size_t start=length;
    while (start && path[start-1]!='/') --start;
    if (!strcmp(selector,"stringByDeletingPathExtension")) {
        size_t dot=length;
        while (dot>start && path[dot-1]!='.') --dot;
        if (dot>start+1) length=dot-1;
    } else {
        length=start;
        while (length>1 && path[length-1]=='/') --length;
    }
    Word result=new_string(length);
    memcpy((void *)read_word(result+16),path,length);
    return result;
}

static Word array_join_text(struct Sequence *array,const char *selector,Word delimiter) {
    struct TextView separator=text_view(delimiter,selector);
    size_t length=0,capacity=64;
    uint16_t *units=malloc(capacity*2);
    if (!units) stop("Allocation failed",selector);
    for (size_t index=0;index<array->count;++index) {
        Word item=array->items[index];
        Method describe=find_implementation(item,"description");
        if (!describe) stop("Missing element description",selector);
        Word description=describe(item,"description",0,0,0,0);
        struct TextView part=text_view(description,selector);
        size_t gap=index ? separator.length : 0;
        if (gap>SIZE_MAX/2-length || part.length>SIZE_MAX/2-length-gap-1) stop("String too large",selector);
        size_t needed=length+gap+part.length;
        if (needed>capacity) {
            capacity=needed;
            uint16_t *grown=realloc(units,capacity*2);
            if (!grown) stop("Allocation failed",selector);
            units=grown;
        }
        if (gap) memcpy(units+length,separator.units,gap*2);
        memcpy(units+length+gap,part.units,part.length*2);
        length=needed;
        free(part.owned);
    }
    Word result=text_from_units(external_class("_OBJC_CLASS_$_NSString"),units,length);
    free(units); free(separator.owned);
    return result;
}

static Word text_byte_length(Word self,const char *selector,Word encoding) {
    Word data=encoded_string(self,selector,(unsigned)encoding);
    if (!data) return 0;
    size_t length=read_word(data+8);
    if (encoding==10 && length>=2) length-=2;
    else if (encoding==0x8c000100 && length>=4) length-=4;
    free((void *)data);
    return length;
}

static Word text_cstring(Word self,const char *selector,char *output,size_t capacity,Word encoding) {
    Word data=encoded_string(self,selector,(unsigned)encoding);
    if (!data) return 0;
    size_t length=read_word(data+8);
    int result=capacity>length;
    if (result) { memcpy(output,(void *)data_buffer((void *)data),length); output[length]=0; }
    free((void *)data);
    return result;
}

static Word text_encoded_pointer(Word self,const char *selector,Word encoding) {
    if (encoding==4) return (Word)string_bytes(self,selector);
    if (encoding!=1) stop("Unsupported C string encoding",selector);
    const unsigned char *text=(void *)string_bytes(self,selector);
    for (const unsigned char *p=text;*p;++p) if (*p>=128) return 0;
    return (Word)text;
}

static Method strings_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSArray") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableArray")) &&
        !strcmp(selector,"componentsJoinedByString:")) return (Method)array_join_text;
    int mutable=!strcmp(receiver,"_OBJC_CLASS_$_NSMutableString");
    if (!mutable && strcmp(receiver,"_OBJC_CLASS_$_NSString")) return NULL;
    if (class_receiver) {
        if (!strcmp(selector,"string") || !strcmp(selector,"stringWithCapacity:")) return (Method)mutable_string;
        if (!strcmp(selector,"stringWithString:")) return (Method)text_from_string;
        if (!strcmp(selector,"stringWithUTF8String:")) return (Method)string_from_utf8;
        if (!strcmp(selector,"stringWithCharacters:length:")) return (Method)text_from_characters;
        if (!strcmp(selector,"stringWithCString:encoding:")) return (Method)text_from_cstring;
    } else {
        if (!strcmp(selector,"init")) return (Method)text_init_empty;
        if (!strcmp(selector,"description")) return (Method)identity;
        if (!strcmp(selector,"hasPrefix:") || !strcmp(selector,"hasSuffix:")) return (Method)text_affix;
        if (!strcmp(selector,"containsString:")) return (Method)text_contains;
        if (!strcmp(selector,"initWithString:")) return (Method)text_init_string;
        if (!strcmp(selector,"initWithUTF8String:")) return (Method)text_init_utf8;
        if (!strcmp(selector,"initWithBytes:length:encoding:")) return (Method)text_init_bytes;
        if (!strcmp(selector,"copy") || !strcmp(selector,"mutableCopy") || !strcmp(selector,"copyWithZone:") || !strcmp(selector,"mutableCopyWithZone:")) return (Method)text_copy;
        if (!strcmp(selector,"substringWithRange:")) return (Method)text_subrange;
        if (!strcmp(selector,"substringFromIndex:")) return (Method)text_suffix;
        if (!strcmp(selector,"characterAtIndex:")) return (Method)text_character;
        if (!strcmp(selector,"getCharacters:range:")) return (Method)text_get_characters;
        if (!strcmp(selector,"rangeOfString:")) return (Method)text_find;
        if (!strcmp(selector,"rangeOfString:options:")) return (Method)text_find_options;
        if (!strcmp(selector,"rangeOfString:options:range:")) return (Method)text_find_range;
        if (!strcmp(selector,"stringByReplacingCharactersInRange:withString:")) return (Method)text_replacing_range;
        if (!strcmp(selector,"stringByDeletingPathExtension") || !strcmp(selector,"stringByDeletingLastPathComponent")) return (Method)text_delete_path;
        if (!strcmp(selector,"lowercaseString") || !strcmp(selector,"uppercaseString") || !strcmp(selector,"capitalizedString")) return (Method)text_case;
        if (!strcmp(selector,"caseInsensitiveCompare:")) return (Method)text_compare_case;
        if (!strcmp(selector,"lengthOfBytesUsingEncoding:") || !strcmp(selector,"maximumLengthOfBytesUsingEncoding:")) return (Method)text_byte_length;
        if (!strcmp(selector,"getCString:maxLength:encoding:")) return (Method)text_cstring;
        if (!strcmp(selector,"cStringUsingEncoding:")) return (Method)text_encoded_pointer;
        if (mutable) {
            if (!strcmp(selector,"replaceCharactersInRange:withString:")) return (Method)text_mutate_range;
            if (!strcmp(selector,"deleteCharactersInRange:")) return (Method)text_delete_range;
            if (!strcmp(selector,"setString:")) return (Method)text_set;
        }
    }
    return NULL;
}
