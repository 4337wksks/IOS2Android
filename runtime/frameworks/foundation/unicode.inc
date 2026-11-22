 
struct TextView { const uint16_t *units; uint16_t *owned; size_t length; };
struct TextCache { Word object; char *bytes; struct TextCache *next; };
static struct TextCache *constant_text_cache;
static pthread_mutex_t text_cache_lock=PTHREAD_MUTEX_INITIALIZER;

static int decode_utf8(const unsigned char **position,const unsigned char *end,uint32_t *value) {
    const unsigned char *p=*position;
    if (p==end) return 0;
    uint32_t ch=*p++;
    unsigned extra=0;
    if (ch>=0xc2 && ch<=0xdf) { ch&=31; extra=1; }
    else if (ch>=0xe0 && ch<=0xef) { ch&=15; extra=2; }
    else if (ch>=0xf0 && ch<=0xf4) { ch&=7; extra=3; }
    else if (ch>=128) return -1;
    if ((size_t)(end-p)<extra) return -1;
    for (unsigned i=0;i<extra;++i) {
        if ((p[i]&0xc0)!=0x80) return -1;
        ch=(ch<<6)|(p[i]&63);
    }
    if ((extra==1 && ch<128) || (extra==2 && ch<0x800) ||
        (extra==3 && ch<0x10000) || ch>0x10ffff || (ch>=0xd800 && ch<=0xdfff)) return -1;
    *position=p+extra;
    *value=ch;
    return 1;
}
static int32_t utf8_next_safe(const uint8_t *bytes,int32_t *position,int32_t length,int32_t lead,int8_t strict) {
    if (!bytes || !position || *position<0 || (length>=0 && *position>length)) return -1;
    unsigned byte=(unsigned)lead;
    unsigned count=byte>=0xc0 && byte<=0xdf ? 1 : byte<=0xef && byte>=0xe0 ? 2 :
        byte>=0xf0 && byte<=0xf7 ? 3 : byte>=0xf8 && byte<=0xfb ? 4 : byte>=0xfc && byte<=0xfd ? 5 : 0;
    int32_t start=*position;
    unsigned available=length<0 ? count : (unsigned)(length-start)<count ? (unsigned)(length-start) : count;
    int valid=count>=1 && count<=3 && available==count;
    uint32_t code=byte&((1U<<(6-count))-1U);
    for (unsigned i=0;valid && i<count;++i) {
        uint8_t trail=bytes[start+i];
        if ((trail&0xc0)!=0x80) valid=0;
        else code=(code<<6)|(trail&0x3f);
    }
    const uint32_t minimum[]={0,0x80,0x800,0x10000};
    if (valid && (code<minimum[count] || code>0x10ffff ||
        (strict!=-2 && code>=0xd800 && code<=0xdfff) ||
        (strict>0 && ((code>=0xfdd0 && code<=0xfdef) || (code&0xffff)>=0xfffe)))) valid=0;
    if (valid) { *position=start+(int32_t)count; return (int32_t)code; }
    unsigned consumed=0;
    while (consumed<available && (bytes[start+consumed]&0xc0)==0x80) ++consumed;
    *position=start+(int32_t)consumed;
    if (strict<0) return strict==-3 ? 0xfffd : -1;
    const int32_t errors[]={0x15,0x9f,0xffff,0x10ffff,0x3ffffff,0x7fffffff};
    return errors[consumed];
}

static struct TextView text_view(Word object,const char *operation) {
    if (!object) stop("Nil string",operation);
    Word flags=read_word(object+8);
    size_t size=read_word(object+24);
    const void *data=(void *)read_word(object+16);
    if (!data && size) stop("Invalid string storage",operation);
    if (flags==0x7d0) return (struct TextView){data,NULL,size};
    if (flags!=0x7c8 || size>SIZE_MAX/2-1) stop("Unsupported string storage",operation);
    uint16_t *units=malloc((size+1)*sizeof(*units));
    if (!units) stop("Allocation failed",operation);
    const unsigned char *p=data,*end=p+size;
    size_t length=0;
    while (p<end) {
        uint32_t ch;
        if (decode_utf8(&p,end,&ch)<0) { free(units); stop("Invalid UTF-8",operation); }
        if (ch>0xffff) { ch-=0x10000; units[length++]=0xd800+(ch>>10); units[length++]=0xdc00+(ch&1023); }
        else units[length++]=(uint16_t)ch;
    }
    units[length]=0;
    return (struct TextView){units,units,length};
}

static Word text_from_units(Word cls,const uint16_t *units,size_t length) {
    if (length>SIZE_MAX/2-sizeof(struct HostObject) || (!units && length)) stop("Invalid UTF-16 length","NSString");
    int ascii=1;
    for (size_t i=0;i<length;++i) if (!units[i] || units[i]>=128) { ascii=0; break; }
    if (ascii) {
        Word result=new_string(length);
        ((struct HostObject *)result)->isa=cls;
        char *out=(void *)read_word(result+16);
        for (size_t i=0;i<length;++i) out[i]=(char)units[i];
        return result;
    }
    struct HostObject *result=calloc(1,sizeof(*result)+(length+1)*sizeof(uint16_t));
    if (!result) stop("Allocation failed","NSString");
    result->isa=cls; result->content[0]=0x7d0; result->content[1]=(Word)(result+1); result->content[2]=length;
    if (length) memcpy(result+1,units,length*sizeof(uint16_t));
    return (Word)result;
}

static char *text_utf8(const uint16_t *units,size_t length,size_t *byte_count) {
    if (length>(SIZE_MAX-1)/3) return NULL;
    char *out=malloc(length*3+1);
    if (!out) stop("Allocation failed","UTF-8");
    size_t count=0;
    for (size_t i=0;i<length;++i) {
        uint32_t ch=units[i];
        if (ch>=0xd800 && ch<=0xdbff && i+1<length && units[i+1]>=0xdc00 && units[i+1]<=0xdfff) {
            ch=0x10000+((ch-0xd800)<<10)+(units[++i]-0xdc00);
        } else if (ch>=0xd800 && ch<=0xdfff) { free(out); return NULL; }
        if (ch<128) out[count++]=(char)ch;
        else if (ch<0x800) { out[count++]=(char)(0xc0|(ch>>6)); out[count++]=(char)(0x80|(ch&63)); }
        else if (ch<0x10000) {
            out[count++]=(char)(0xe0|(ch>>12)); out[count++]=(char)(0x80|((ch>>6)&63)); out[count++]=(char)(0x80|(ch&63));
        } else {
            out[count++]=(char)(0xf0|(ch>>18)); out[count++]=(char)(0x80|((ch>>12)&63));
            out[count++]=(char)(0x80|((ch>>6)&63)); out[count++]=(char)(0x80|(ch&63));
        }
    }
    out[count]=0;
    *byte_count=count;
    return out;
}

static const char *string_bytes(Word object,const char *operation) {
    if (!object) stop("Nil string",operation);
    if (read_word(object+8)==0x7c8) {
        const char *bytes=(void *)read_word(object+16);
        if (!bytes || strlen(bytes)!=read_word(object+24)) stop("Unsupported C string contents",operation);
        return bytes;
    }
    if (read_word(object+8)!=0x7d0) stop("Unsupported string encoding",operation);
    pthread_mutex_lock(&text_cache_lock);
    if (in_image(object)) {
        for (struct TextCache *entry=constant_text_cache;entry;entry=entry->next) {
            if (entry->object==object) { pthread_mutex_unlock(&text_cache_lock); return entry->bytes; }
        }
    } else if (read_word(object+32)) {
        const char *bytes=(void *)read_word(object+32);
        pthread_mutex_unlock(&text_cache_lock);
        return bytes;
    }
    pthread_mutex_unlock(&text_cache_lock);
    size_t count;
    char *bytes=text_utf8((void *)read_word(object+16),read_word(object+24),&count);
    if (!bytes) stop("String cannot be encoded as UTF-8",operation);
    if (memchr(bytes,0,count)) { free(bytes); stop("Embedded NUL in C string",operation); }
    pthread_mutex_lock(&text_cache_lock);
    if (in_image(object)) {
        struct TextCache *entry=malloc(sizeof(*entry));
        if (!entry) { pthread_mutex_unlock(&text_cache_lock); free(bytes); stop("Allocation failed",operation); }
        *entry=(struct TextCache){object,bytes,constant_text_cache}; constant_text_cache=entry;
    } else {
        char *existing=(void *)read_word(object+32);
        if (existing) { free(bytes); bytes=existing; }
        else ((struct HostObject *)object)->content[3]=(Word)bytes;
    }
    pthread_mutex_unlock(&text_cache_lock);
    return bytes;
}

static Word text_decode(Word cls,const void *data,size_t size,Word encoding) {
    if (!data && size) return 0;
    if (size>(SIZE_MAX-1)/2) return 0;
    const unsigned char *bytes=data;
    uint16_t *units=malloc((size+1)*sizeof(*units));
    if (!units) stop("Allocation failed","Text decode");
    size_t count=0;
    int valid=1;
    if (encoding==4) {
        const unsigned char *p=bytes,*end=bytes+size;
        while (p<end) {
            uint32_t ch;
            if (decode_utf8(&p,end,&ch)<0) { valid=0; break; }
            if (ch>0xffff) { ch-=0x10000; units[count++]=0xd800+(ch>>10); units[count++]=0xdc00+(ch&1023); }
            else units[count++]=(uint16_t)ch;
        }
    } else if (encoding==1 || encoding==5) {
        for (size_t i=0;i<size;++i) { units[count++]=bytes[i]; if (encoding==1 && bytes[i]>=128) valid=0; }
    } else if (encoding==10 || encoding==0x90000100 || encoding==0x94000100) {
        int big=encoding==0x90000100;
        size_t start=0;
        if (encoding==10 && size>=2) {
            if (bytes[0]==0xfe && bytes[1]==0xff) { big=1; start=2; }
            else if (bytes[0]==0xff && bytes[1]==0xfe) start=2;
        }
        if (size%2) valid=0;
        else for (size_t i=start;i<size;i+=2) units[count++]=big ? ((uint16_t)bytes[i]<<8)|bytes[i+1] : ((uint16_t)bytes[i+1]<<8)|bytes[i];
    } else if (encoding==0x8c000100 || encoding==0x98000100 || encoding==0x9c000100) {
        int big=encoding==0x98000100; size_t start=0;
        if (encoding==0x8c000100 && size>=4) {
            if (!memcmp(bytes,"\0\0\xfe\xff",4)) { big=1; start=4; }
            else if (!memcmp(bytes,"\xff\xfe\0\0",4)) start=4;
        }
        if (size%4) valid=0;
        else for (size_t i=start;i<size;i+=4) {
            uint32_t scalar=big ? ((uint32_t)bytes[i]<<24)|((uint32_t)bytes[i+1]<<16)|((uint32_t)bytes[i+2]<<8)|bytes[i+3] :
                ((uint32_t)bytes[i+3]<<24)|((uint32_t)bytes[i+2]<<16)|((uint32_t)bytes[i+1]<<8)|bytes[i];
            if (scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff)) { valid=0; break; }
            if (scalar>0xffff) { scalar-=0x10000; units[count++]=0xd800+(scalar>>10); units[count++]=0xdc00+(scalar&1023); }
            else units[count++]=(uint16_t)scalar;
        }
    } else { free(units); stop("Unsupported text encoding","Text decode"); }
    Word result=valid ? text_from_units(cls,units,count) : 0;
    free(units);
    return result;
}

static void assign_text(struct HostObject *object,const uint16_t *units,size_t length,const char *operation) {
    if (in_image((Word)object) || object->isa!=external_class("_OBJC_CLASS_$_NSMutableString")) stop("Mutating immutable string",operation);
    Word replacement=text_from_units(object->isa,units,length);
    struct HostObject *copy=(void *)replacement;
    size_t bytes=(copy->content[2]+1)*(copy->content[0]==0x7d0 ? 2 : 1);
    void *storage=malloc(bytes);
    if (!storage) stop("Allocation failed",operation);
    memcpy(storage,(void *)copy->content[1],bytes);
    if (object->content[1] && object->content[1]!=(Word)(object+1)) free((void *)object->content[1]);
    if (object->content[3]) free((void *)object->content[3]);
    object->content[0]=copy->content[0]; object->content[1]=(Word)storage; object->content[2]=copy->content[2]; object->content[3]=0;
    free(copy);
}
