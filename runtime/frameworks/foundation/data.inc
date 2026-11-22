 
static Word data_allocate(Word cls,const char *selector,size_t capacity) {
    if (capacity>SIZE_MAX-sizeof(struct HostObject)) stop("Data too large",selector);
    struct HostObject *object=calloc(1,sizeof(*object)+capacity);
    if (!object) stop("Allocation failed",selector);
    object->isa=cls;
    if (cls==external_class("_OBJC_CLASS_$_NSMutableData")) {
        object->content[1]=(Word)(object+1); object->content[2]=capacity; object->content[3]=0x4d444154;
    }
    if (!strcmp(selector,"dataWithLength:") || !strcmp(selector,"initWithLength:")) object->content[0]=capacity;
    return (Word)object;
}
static Word data_init_capacity(Word self,const char *selector,size_t capacity) {
    Word result=data_allocate(read_word(self),selector,capacity);
    free((void *)self);
    return result;
}
static Word data_init_empty(Word self,const char *selector) { return data_init_capacity(self,"dataWithCapacity:",0); }
static Word data_decode_base64(Word cls,const char *selector,Word source,Word options) {
    if (!source || (options&~1UL)) return 0;
    struct TextView text=text_view(source,selector); unsigned char *output=text.length ? malloc(text.length) : NULL;
    if (text.length && !output) { free(text.owned); stop("Allocation failed",selector); }
    size_t count=0; unsigned value=0,bits=0,digits=0,padding=0; int valid=1;
    for (size_t i=0;i<text.length;++i) {
        unsigned c=text.units[i],n=64;
        if (c>='A' && c<='Z') n=c-'A'; else if (c>='a' && c<='z') n=c-'a'+26; else if (c>='0' && c<='9') n=c-'0'+52; else if (c=='+') n=62; else if (c=='/') n=63;
        if (c=='=') { ++padding; if (padding>2) { valid=0; break; } continue; }
        if (n==64) { if (options&1) continue; valid=0; break; }
        if (padding) { valid=0; break; }
        value=(value<<6)|n; bits+=6; ++digits;
        if (bits>=8) { bits-=8; output[count++]=(unsigned char)(value>>bits); }
    }
    if ((digits+padding)%4 || (padding==1 && digits%4!=3) || (padding==2 && digits%4!=2)) valid=0;
    Word result=valid ? data_with_bytes(cls,selector,output,count) : 0; free(output); free(text.owned); return result;
}
static Word data_init_base64(Word object,const char *selector,Word source,Word options) { Word result=data_decode_base64(read_word(object),selector,source,options); free((void *)object); return result; }
static Word data_copy(Word self,const char *selector) {
    int mutable=!strcmp(selector,"mutableCopy");
    Word cls=external_class(mutable ? "_OBJC_CLASS_$_NSMutableData" : "_OBJC_CLASS_$_NSData");
    if (!mutable && read_word(self)==cls) return self;
    return data_with_bytes(cls,selector,(void *)data_buffer((void *)self),read_word(self+8));
}
static void data_reserve(struct HostObject *data,size_t capacity,const char *selector) {
    if (data->isa!=external_class("_OBJC_CLASS_$_NSMutableData")) stop("Mutating immutable data",selector);
    if (data->content[3]!=0x4d444154) {
        data->content[1]=(Word)(data+1); data->content[2]=data->content[0]; data->content[3]=0x4d444154;
    }
    if (capacity<=data->content[2]) return;
    size_t next=data->content[2] <= SIZE_MAX/2 ? data->content[2]*2 : capacity;
    if (next<capacity) next=capacity;
    if (next<64) next=64;
    void *old=(void *)data->content[1];
    void *bytes=old==(void *)(data+1) ? malloc(next) : realloc(old,next);
    if (!bytes) stop("Allocation failed",selector);
    if (old==(void *)(data+1) && data->content[0]) memcpy(bytes,old,data->content[0]);
    data->content[1]=(Word)bytes; data->content[2]=next;
}
static void data_set_length(struct HostObject *data,const char *selector,size_t length) {
    size_t old=data->content[0];
    data_reserve(data,length,selector);
    if (length>old) memset((char *)data_buffer(data)+old,0,length-old);
    data->content[0]=length;
}
static void data_append_bytes(struct HostObject *data,const char *selector,const void *source,size_t length) {
    if (length>SIZE_MAX-data->content[0] || (!source && length)) stop("Invalid appended data",selector);
    size_t old=data->content[0];
    Word buffer=data_buffer(data),from=(Word)source;
    int own=from>=buffer && from-buffer<=old && length<=old-(from-buffer);
    size_t offset=own ? from-buffer : 0;
    data_reserve(data,old+length,selector);
    if (own) source=(void *)(data_buffer(data)+offset);
    if (length) memmove((char *)data_buffer(data)+old,source,length);
    data->content[0]=old+length;
}
static void data_append(struct HostObject *data,const char *selector,struct HostObject *source) {
    if (source) data_append_bytes(data,selector,(void *)data_buffer(source),source->content[0]);
}
static void data_replace(struct HostObject *data,const char *selector,struct GuestRange range,const void *source) {
    check_range(range,data->content[0],selector);
    data_reserve(data,data->content[0],selector);
    if (source) memmove((char *)data_buffer(data)+range.location,source,range.length);
    else memset((char *)data_buffer(data)+range.location,0,range.length);
}
static Word data_file_options(Word cls,const char *selector,Word filename,Word options,Word *error) {
    if (options&~(Word)3) return file_failure(error,string_bytes(filename,selector),ENOTSUP);
    Word result=data_from_file(cls,selector,filename);
    return result ? result : file_failure(error,string_bytes(filename,selector),errno ? errno : EIO);
}
static Word connection_read_url(Word,struct HostObject *,Word *);
static Word data_url(Word cls,const char *selector,struct HostObject *url) {
    if (!url) return 0;
    if (!url->content[1]) return connection_read_url(cls,url,NULL);
    const char *path=string_bytes(url_path(url),selector);
    if (strstr(path,"://") && strncmp(path,"file://",7)) stop("URL transport not implemented",selector);
    return data_from_file(cls,selector,copy_string(!strncmp(path,"file://",7) ? path+7 : path));
}
static Word data_url_options(Word cls,const char *selector,struct HostObject *url,Word options,Word *error) {
    if (error) *error=0;
    if (url && !url->content[1]) return connection_read_url(cls,url,error);
    if (options&~(Word)3) return file_failure(error,string_bytes(url_path(url),selector),ENOTSUP);
    Word result=data_url(cls,selector,url);
    return result ? result : file_failure(error,string_bytes(url_path(url),selector),errno ? errno : EIO);
}
static Word data_init_url(Word self,const char *selector,struct HostObject *url) {
    Word result=data_url(read_word(self),selector,url); free((void *)self); return result;
}
static Word data_init_url_options(Word self,const char *selector,struct HostObject *url,Word options,Word *error) {
    Word result=data_url_options(read_word(self),selector,url,options,error); free((void *)self); return result;
}
static Word data_write_file(struct HostObject *data,const char *selector,Word filename,Word options,Word *error) {
    const char *path=string_bytes(filename,selector);
    if ((options&~(Word)3) || !writable_sandbox_path(path)) return file_failure(error,path,options&~(Word)3 ? ENOTSUP : errno);
    char *temporary=NULL;
    int fd;
    if (options&1) {
        if (asprintf(&temporary,"%s.XXXXXX",path)<0) stop("Allocation failed",selector);
        fd=mkstemp(temporary);
    } else fd=open(path,O_WRONLY|O_CREAT|O_CLOEXEC|((options&2) ? O_EXCL : O_TRUNC),0600);
    if (fd<0) { int code=errno; free(temporary); return file_failure(error,path,code); }
    size_t done=0,length=data->content[0];
    int code=0;
    while (done<length) {
        ssize_t count=write(fd,(char *)data_buffer(data)+done,length-done);
        if (count<0 && errno==EINTR) continue;
        if (count<=0) { code=count<0 ? errno : EIO; break; }
        done+=(size_t)count;
    }
    if (!code && fsync(fd)) code=errno;
    if (close(fd) && !code) code=errno;
    if (!code && temporary) {
        if ((options&2) ? link(temporary,path) : rename(temporary,path)) code=errno;
    }
    if (temporary) { unlink(temporary); free(temporary); }
    return code ? file_failure(error,path,code) : 1;
}
static Word data_write_atomic(struct HostObject *data,const char *selector,Word filename,unsigned char atomic) {
    return data_write_file(data,selector,filename,atomic ? 1 : 0,NULL);
}
static Word data_write_url(struct HostObject *data,const char *selector,struct HostObject *url,unsigned char atomic) {
    return data_write_atomic(data,selector,url_path(url),atomic);
}
static Word data_write_url_options(struct HostObject *data,const char *selector,struct HostObject *url,Word options,Word *error) {
    return data_write_file(data,selector,url_path(url),options,error);
}
static Method data_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    int mutable=receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSMutableData");
    if (!receiver || (!mutable && strcmp(receiver,"_OBJC_CLASS_$_NSData"))) return NULL;
    if (class_receiver) {
        if (!strcmp(selector,"data")) return (Method)data_empty;
        if (!strcmp(selector,"dataWithBytes:length:")) return (Method)data_with_bytes;
        if (!strcmp(selector,"dataWithContentsOfFile:")) return (Method)data_from_file;
        if (!strcmp(selector,"dataWithContentsOfFile:options:error:")) return (Method)data_file_options;
        if (!strcmp(selector,"dataWithContentsOfURL:")) return (Method)data_url;
        if (!strcmp(selector,"dataWithContentsOfURL:options:error:")) return (Method)data_url_options;
        if (mutable && (!strcmp(selector,"dataWithCapacity:") || !strcmp(selector,"dataWithLength:"))) return (Method)data_allocate;
    } else {
        if (!strcmp(selector,"initWithBase64EncodedString:options:")) return (Method)data_init_base64;
        if (!strcmp(selector,"init")) return (Method)data_init_empty;
        if (!strcmp(selector,"initWithBytes:length:")) return (Method)data_init_bytes;
        if (!strcmp(selector,"initWithContentsOfURL:")) return (Method)data_init_url;
        if (!strcmp(selector,"initWithContentsOfURL:options:error:")) return (Method)data_init_url_options;
        if (!strcmp(selector,"length")) return (Method)array_count;
        if (!strcmp(selector,"bytes")) return (Method)data_buffer;
        if (!strcmp(selector,"copy") || !strcmp(selector,"mutableCopy")) return (Method)data_copy;
        if (!strcmp(selector,"getBytes:range:")) return (Method)data_get_range;
        if (!strcmp(selector,"getBytes:length:")) return (Method)data_get_length;
        if (!strcmp(selector,"subdataWithRange:")) return (Method)data_subrange;
        if (!strcmp(selector,"enumerateByteRangesUsingBlock:")) return (Method)data_enumerate_block;
        if (!strcmp(selector,"writeToFile:options:error:")) return (Method)data_write_file;
        if (!strcmp(selector,"writeToFile:atomically:")) return (Method)data_write_atomic;
        if (!strcmp(selector,"writeToURL:atomically:")) return (Method)data_write_url;
        if (!strcmp(selector,"writeToURL:options:error:")) return (Method)data_write_url_options;
        if (mutable) {
            if (!strcmp(selector,"initWithCapacity:") || !strcmp(selector,"initWithLength:")) return (Method)data_init_capacity;
            if (!strcmp(selector,"mutableBytes")) return (Method)data_buffer;
            if (!strcmp(selector,"setLength:")) return (Method)data_set_length;
            if (!strcmp(selector,"appendBytes:length:")) return (Method)data_append_bytes;
            if (!strcmp(selector,"appendData:")) return (Method)data_append;
            if (!strcmp(selector,"replaceBytesInRange:withBytes:")) return (Method)data_replace;
        }
    }
    return NULL;
}
