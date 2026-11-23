struct CoreHttpHeader { char *name,*value; unsigned encoding; };
struct CoreHttpMessage {
    Word tag; unsigned references,request,complete,failed;
    long status; unsigned reason_encoding;
    char *method,*target,*version,*reason,*url;
    unsigned char *bytes; size_t size,capacity,scan,header_end;
    struct CoreHttpHeader *headers; size_t count,header_capacity;
    size_t *buckets,bucket_capacity;
};
static char *core_http_text(const char *text,size_t length) {
    if (length==SIZE_MAX) stop("HTTP text too large","CFHTTPMessage");
    char *copy=malloc(length+1); if (!copy) stop("Allocation failed","CFHTTPMessage");
    memcpy(copy,text,length); copy[length]=0; return copy;
}
static struct CoreHttpMessage *core_http_check(Word object) {
    if (!object || read_word(object)!=(Word)&http_message_tag) stop("Invalid HTTP message","CFHTTPMessage");
    return (void *)object;
}
static Word core_http_new(Word allocator,unsigned char request) {
    if (allocator) return 0;
    struct CoreHttpMessage *message=calloc(1,sizeof(*message)); if (!message) return 0;
    message->tag=(Word)&http_message_tag; message->references=1; message->request=request!=0; message->reason_encoding=4;
    return (Word)message;
}
static int http_message_release(Word object) {
    if (read_word(object)!=(Word)&http_message_tag) return 0;
    struct CoreHttpMessage *m=(void *)object;
    unsigned old=__atomic_fetch_sub(&m->references,1,__ATOMIC_ACQ_REL);
    if (!old) stop("Unbalanced HTTP message release","CFRelease");
    if (old!=1) return 1;
    for (size_t i=0;i<m->count;++i) { free(m->headers[i].name); free(m->headers[i].value); }
    free(m->headers); free(m->buckets); free(m->bytes);
    free(m->method); free(m->target); free(m->version); free(m->reason); free(m->url); free(m); return 1;
}
static int http_message_retain(Word object) {
    if (read_word(object)!=(Word)&http_message_tag) return 0;
    struct CoreHttpMessage *m=(void *)object;
    unsigned old=__atomic_fetch_add(&m->references,1,__ATOMIC_RELAXED);
    if (!old || old==UINT_MAX) stop("Invalid HTTP message retain","CFRetain");
    return 1;
}
static unsigned char core_http_lower(unsigned char ch) { return ch>='A' && ch<='Z' ? (unsigned char)(ch+32) : ch; }
static size_t core_http_hash(const char *name) {
    size_t hash=14695981039346656037UL;
    for (;*name;++name) hash=(hash^core_http_lower((unsigned char)*name))*1099511628211UL;
    return hash;
}
static int core_http_name_equal(const char *a,const char *b) {
    while (*a && core_http_lower((unsigned char)*a)==core_http_lower((unsigned char)*b)) { ++a; ++b; }
    return !*a && !*b;
}
static int core_http_token(const char *text) {
    if (!*text) return 0;
    for (const unsigned char *p=(const void *)text;*p;++p)
        if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || (*p>='0' && *p<='9') || strchr("!#$%&'*+-.^_`|~",*p))) return 0;
    return 1;
}
static int core_http_value_valid(const char *text) {
    for (const unsigned char *p=(const void *)text;*p;++p) if ((*p<32 && *p!='\t') || *p==127) return 0;
    return 1;
}
static void core_http_rehash(struct CoreHttpMessage *m,size_t capacity) {
    if (capacity>SIZE_MAX/sizeof(size_t)) stop("Too many HTTP headers","CFHTTPMessage");
    size_t *buckets=calloc(capacity,sizeof(*buckets)); if (!buckets) stop("Allocation failed","HTTP header index");
    for (size_t i=0;i<m->count;++i) {
        size_t slot=core_http_hash(m->headers[i].name)&(capacity-1);
        while (buckets[slot]) slot=(slot+1)&(capacity-1);
        buckets[slot]=i+1;
    }
    free(m->buckets); m->buckets=buckets; m->bucket_capacity=capacity;
}
static size_t core_http_header_index(struct CoreHttpMessage *m,const char *name) {
    if (!m->bucket_capacity) return m->count;
    size_t slot=core_http_hash(name)&(m->bucket_capacity-1);
    while (m->buckets[slot]) {
        size_t i=m->buckets[slot]-1;
        if (core_http_name_equal(m->headers[i].name,name)) return i;
        slot=(slot+1)&(m->bucket_capacity-1);
    }
    return m->count;
}
static void core_http_header_set(struct CoreHttpMessage *m,const char *name,const char *value,unsigned encoding,int append) {
    size_t i=core_http_header_index(m,name);
    if (!value) {
        if (i==m->count) return;
        free(m->headers[i].name); free(m->headers[i].value);
        memmove(m->headers+i,m->headers+i+1,(m->count-i-1)*sizeof(*m->headers)); --m->count;
         
        core_http_rehash(m,m->bucket_capacity); return;
    }
    char *copy;
    if (append && i<m->count) {
        size_t old=strlen(m->headers[i].value),next=strlen(value),separator=append==2 ? 1 : 2;
        if (old>SIZE_MAX-separator-1 || next>SIZE_MAX-old-separator-1) stop("HTTP header too large","CFHTTPMessage");
        copy=malloc(old+next+separator+1); if (!copy) stop("Allocation failed","HTTP header");
        memcpy(copy,m->headers[i].value,old); memcpy(copy+old,append==2 ? " " : ", ",separator); memcpy(copy+old+separator,value,next+1);
    } else copy=core_http_text(value,strlen(value));
    if (i<m->count) { free(m->headers[i].value); m->headers[i].value=copy; m->headers[i].encoding=encoding; return; }
    if (!m->bucket_capacity || m->count>=m->bucket_capacity/2) {
        if (m->bucket_capacity>SIZE_MAX/2) stop("Too many HTTP headers","CFHTTPMessage");
        core_http_rehash(m,m->bucket_capacity ? m->bucket_capacity*2 : 16);
    }
    if (m->count==m->header_capacity) {
        size_t capacity=m->header_capacity ? m->header_capacity*2 : 8;
        if (capacity<m->header_capacity || capacity>SIZE_MAX/sizeof(*m->headers)) stop("Too many HTTP headers","CFHTTPMessage");
        void *headers=realloc(m->headers,capacity*sizeof(*m->headers)); if (!headers) stop("Allocation failed","HTTP headers");
        m->headers=headers; m->header_capacity=capacity;
    }
    m->headers[m->count]=(struct CoreHttpHeader){core_http_text(name,strlen(name)),copy,encoding};
    size_t slot=core_http_hash(name)&(m->bucket_capacity-1);
    while (m->buckets[slot]) slot=(slot+1)&(m->bucket_capacity-1);
    m->buckets[slot]=++m->count;
}
static int core_http_buffer(struct CoreHttpMessage *m,const void *bytes,size_t length) {
    if (length>SIZE_MAX-m->size || (length && !bytes)) return 0;
    size_t size=m->size+length;
    if (size>m->capacity) {
        size_t capacity=m->capacity<=SIZE_MAX/2 ? m->capacity*2 : size;
        if (capacity<size) capacity=size; if (capacity<512) capacity=512;
        void *next=realloc(m->bytes,capacity); if (!next) return 0;
        m->bytes=next; m->capacity=capacity;
    }
    if (length) memcpy(m->bytes+m->size,bytes,length);
    m->size=size; return 1;
}
static int core_http_version(const char *version) { return !strcmp(version,"HTTP/1.0") || !strcmp(version,"HTTP/1.1"); }
static int core_http_target_valid(const char *target) {
    if (!*target) return 0;
    for (const unsigned char *p=(const void *)target;*p;++p) if (*p<=32 || *p==127) return 0;
    return 1;
}
static int core_http_parse(struct CoreHttpMessage *m,size_t end) {
    if (memchr(m->bytes,0,end)) return 0;
    char *arena=core_http_text((const char *)m->bytes,end),*line=arena,*next=strstr(arena,"\r\n");
    int valid=0;
    if (!next) goto done;
    *next=0; next+=2;
    char *space=strchr(line,' '); if (!space) goto done; *space++=0;
    if (m->request) {
        char *version=strchr(space,' '); if (!version) goto done; *version++=0;
        if (!core_http_token(line) || !core_http_target_valid(space) || !core_http_version(version)) goto done;
        m->method=core_http_text(line,strlen(line)); m->target=core_http_text(space,strlen(space)); m->version=core_http_text(version,strlen(version));
        m->url=core_http_text(space,strlen(space));
    } else {
        if (!core_http_version(line) || strlen(space)<3 || space[0]<'1' || space[0]>'9' || space[1]<'0' || space[1]>'9' || space[2]<'0' || space[2]>'9' || (space[3] && space[3]!=' ')) goto done;
        char *reason=space+3; if (*reason==' ') ++reason;
        if (!core_http_value_valid(reason)) goto done;
        m->version=core_http_text(line,strlen(line)); m->status=(space[0]-'0')*100+(space[1]-'0')*10+space[2]-'0';
        m->reason=core_http_text(reason,strlen(reason)); m->reason_encoding=5;
    }
    size_t last=SIZE_MAX;
    while (*next) {
        line=next; next=strstr(line,"\r\n"); if (!next) goto done; *next=0; next+=2;
        if (!*line) { valid=1; break; }
         
        if (*line==' ' || *line=='\t') {
            if (m->request || last==SIZE_MAX) goto done;
            while (*line==' ' || *line=='\t') ++line;
            size_t n=strlen(line); while (n && (line[n-1]==' ' || line[n-1]=='\t')) line[--n]=0;
            if (!core_http_value_valid(line)) goto done;
            if (n) core_http_header_set(m,m->headers[last].name,line,5,2);
            continue;
        }
        char *colon=strchr(line,':'); if (!colon) goto done; *colon++=0;
        if (!core_http_token(line)) goto done;
        while (*colon==' ' || *colon=='\t') ++colon;
        size_t n=strlen(colon); while (n && (colon[n-1]==' ' || colon[n-1]=='\t')) colon[--n]=0;
        if (!core_http_value_valid(colon)) goto done;
        core_http_header_set(m,line,colon,5,1);
        last=core_http_header_index(m,line);
    }
done:
    free(arena); return valid;
}
static unsigned char core_http_append(Word object,const void *bytes,intptr_t length) {
    struct CoreHttpMessage *m=core_http_check(object);
    if (m->failed || length<0 || !core_http_buffer(m,bytes,(size_t)length)) { m->failed=1; return 0; }
    if (m->complete) return 1;
    for (size_t i=m->scan;i+3<m->size;++i) {
        if (m->bytes[i]=='\r' && m->bytes[i+1]=='\n' && m->bytes[i+2]=='\r' && m->bytes[i+3]=='\n') {
            m->header_end=i+4;
            m->complete=core_http_parse(m,m->header_end);
            if (!m->complete) m->failed=1;
            return m->complete;
        }
    }
    m->scan=m->size>3 ? m->size-3 : 0; return 1;
}
static unsigned char core_http_complete(Word object) { struct CoreHttpMessage *m=core_http_check(object); return m->complete && !m->failed; }
static unsigned char core_http_is_request(Word object) { return core_http_check(object)->request; }
static long core_http_status(Word object) { struct CoreHttpMessage *m=core_http_check(object); return m->complete && !m->failed && !m->request ? m->status : 0; }
static Word core_http_copy_headers(Word object) {
    struct CoreHttpMessage *m=core_http_check(object); if (!m->complete || m->failed) return 0;
    struct GuestMap *map=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    if (m->count) {
        if (m->count>SIZE_MAX/sizeof(*map->entries)) stop("Too many HTTP headers","CFHTTPMessageCopyAllHeaderFields");
        map->entries=calloc(m->count,sizeof(*map->entries)); if (!map->entries) stop("Allocation failed","HTTP header dictionary");
        map->count=map->capacity=m->count;
         
        for (size_t i=0;i<m->count;++i) map->entries[i]=(struct MapEntry){copy_string(m->headers[i].name),
            text_decode(external_class("_OBJC_CLASS_$_NSString"),m->headers[i].value,strlen(m->headers[i].value),m->headers[i].encoding)};
    }
    return (Word)map;
}
static Word core_http_copy_header(Word object,Word name) {
    struct CoreHttpMessage *m=core_http_check(object); if (!name || !m->complete || m->failed) return 0;
    size_t i=core_http_header_index(m,string_bytes(name,"CFHTTPMessageCopyHeaderFieldValue"));
    return i==m->count ? 0 : text_decode(external_class("_OBJC_CLASS_$_NSString"),m->headers[i].value,strlen(m->headers[i].value),m->headers[i].encoding);
}
static void core_http_set_header(Word object,Word name,Word value) {
    struct CoreHttpMessage *m=core_http_check(object);
    if (!name) stop("Nil HTTP header name","CFHTTPMessageSetHeaderFieldValue");
    const char *key=string_bytes(name,"HTTP header"),*text=value ? string_bytes(value,"HTTP header") : NULL;
    if (!core_http_token(key) || (text && !core_http_value_valid(text))) stop("Invalid HTTP header field","CFHTTPMessageSetHeaderFieldValue");
    core_http_header_set(m,key,text,4,0);
}
static Word core_http_create_request(Word allocator,Word method,struct HostObject *url,Word version) {
    if (allocator || !method || !url || !version) return 0;
    const char *verb=string_bytes(method,"HTTP request"),*v=string_bytes(version,"HTTP request");
    if (!core_http_token(verb) || !core_http_version(v)) return 0;
    struct UrlParts p=url_parts(url); size_t end=p.fragment==SIZE_MAX ? p.size : p.fragment;
    if (p.path_start>end) return 0;
    size_t length=end-p.path_start; int prefix=!length || p.text[p.path_start]!='/' ;
    if (length>SIZE_MAX-2) return 0;
    char *target=malloc(length+prefix+1); if (!target) return 0;
    if (prefix) target[0]='/'; memcpy(target+prefix,p.text+p.path_start,length); target[length+prefix]=0;
    if (!core_http_target_valid(target)) { free(target); return 0; }
    Word result=core_http_new(allocator,1); if (!result) { free(target); return 0; }
    struct CoreHttpMessage *m=(void *)result;
    m->target=target; m->method=core_http_text(verb,strlen(verb)); m->version=core_http_text(v,strlen(v)); m->url=core_http_text(p.text,p.size); m->complete=1;
    return result;
}
static Word core_http_create_response(Word allocator,long status,Word description,Word version) {
    if (allocator || status<100 || status>999 || !version) return 0;
    const char *v=string_bytes(version,"HTTP response"),*reason=description ? string_bytes(description,"HTTP response") : "";
    if (!core_http_version(v) || !core_http_value_valid(reason)) return 0;
    Word result=core_http_new(allocator,0); if (!result) return 0;
    struct CoreHttpMessage *m=(void *)result; m->status=status; m->version=core_http_text(v,strlen(v)); m->reason=core_http_text(reason,strlen(reason)); m->complete=1;
    return result;
}
static Word core_http_copy_body(Word object) {
    struct CoreHttpMessage *m=core_http_check(object); if (!m->complete || m->failed) return 0;
    size_t length=m->size-m->header_end;
    return data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"CFHTTPMessageCopyBody",length ? m->bytes+m->header_end : NULL,length);
}
static void core_http_set_body(Word object,struct HostObject *data) {
    struct CoreHttpMessage *m=core_http_check(object);
    if (!m->complete || m->failed) stop("HTTP body set before a valid header","CFHTTPMessageSetBody");
    m->size=m->header_end;
    if (data && !core_http_buffer(m,(void *)data_buffer(data),data->content[0])) stop("HTTP body too large","CFHTTPMessageSetBody");
}
static Word core_http_copy_version(Word object) { const char *s=core_http_check(object)->version; return s ? copy_string(s) : 0; }
static Word core_http_copy_method(Word object) { struct CoreHttpMessage *m=core_http_check(object); return m->request && m->method ? copy_string(m->method) : 0; }
static Word core_http_copy_url(Word object) { struct CoreHttpMessage *m=core_http_check(object); return m->request && m->url ? url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"CFHTTPMessageCopyRequestURL",copy_string(m->url)) : 0; }
static Word core_http_copy_status_line(Word object) {
    struct CoreHttpMessage *m=core_http_check(object); if (m->request || !m->complete || m->failed) return 0;
    char *text=NULL; if (asprintf(&text,"%s %ld%s%s",m->version,m->status,*m->reason ? " " : "",m->reason)<0) stop("Allocation failed","HTTP status line");
    Word result=text_decode(external_class("_OBJC_CLASS_$_NSString"),text,strlen(text),m->reason_encoding); free(text); return result;
}
static Word core_http_serialize(Word object) {
    struct CoreHttpMessage *m=core_http_check(object); if (!m->complete || m->failed) return 0;
    char *bytes=NULL; size_t length=0; FILE *stream=open_memstream(&bytes,&length); if (!stream) return 0;
    if (m->request) fprintf(stream,"%s %s %s\r\n",m->method,m->target,m->version);
    else fprintf(stream,"%s %ld%s%s\r\n",m->version,m->status,*m->reason ? " " : "",m->reason);
    for (size_t i=0;i<m->count;++i) fprintf(stream,"%s: %s\r\n",m->headers[i].name,m->headers[i].value);
    fputs("\r\n",stream);
    if (m->size>m->header_end) fwrite(m->bytes+m->header_end,1,m->size-m->header_end,stream);
    int failed=ferror(stream); if (fclose(stream)) failed=1;
    Word result=failed ? 0 : data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"CFHTTPMessageCopySerializedMessage",bytes,length);
    free(bytes); return result;
}
static Word core_http_create_copy(Word allocator,Word object) {
    if (allocator) return 0;
    struct CoreHttpMessage *source=core_http_check(object); Word result=core_http_new(0,source->request); if (!result) return 0;
    struct CoreHttpMessage *m=(void *)result;
    m->complete=source->complete; m->failed=source->failed; m->status=source->status; m->reason_encoding=source->reason_encoding; m->scan=source->scan; m->header_end=source->header_end;
    char **dest[]={&m->method,&m->target,&m->version,&m->reason,&m->url}; const char *fields[]={source->method,source->target,source->version,source->reason,source->url};
    for (unsigned i=0;i<COUNT(fields);++i) if (fields[i]) *dest[i]=core_http_text(fields[i],strlen(fields[i]));
    for (size_t i=0;i<source->count;++i) core_http_header_set(m,source->headers[i].name,source->headers[i].value,source->headers[i].encoding,0);
    if (!core_http_buffer(m,source->bytes,source->size)) { http_message_release(result); return 0; }
    return result;
}
static Word core_http_type(void) { return (Word)&http_message_tag; }
static Word proxy_settings(void) {
    JNIEnv *env=attached_environment("System proxy settings");
    static jmethodID query;
    jmethodID method=__atomic_load_n(&query,__ATOMIC_ACQUIRE);
    if(!method){jclass cls=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,cls,"systemProxySettings","()Ljava/lang/String;");(*env)->DeleteLocalRef(env,cls);__atomic_store_n(&query,method,__ATOMIC_RELEASE);}
    jstring json=method ? (*env)->CallObjectMethod(env,activity,method) : NULL;
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);if(json)(*env)->DeleteLocalRef(env,json);return 0;}
    if(!json)return 0;
    const char *bytes=(*env)->GetStringUTFChars(env,json,NULL);
    Word data=bytes ? data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"System proxy settings",bytes,strlen(bytes)) : 0;
    if(bytes)(*env)->ReleaseStringUTFChars(env,json,bytes);(*env)->DeleteLocalRef(env,json);
    return data ? json_parse(0,"System proxy settings",(void *)data,0,NULL) : 0;
}
static Word proxy_value(struct GuestMap *settings,const char *key) { return map_get(settings,"Proxy settings",copy_string(key)); }
static int proxy_enabled(struct GuestMap *settings,const char *key) { Word value=proxy_value(settings,key);return value && numeric_bool((void *)value); }
static Word proxy_descriptor(const char *type,Word host,Word port) {
    struct GuestMap *proxy=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(proxy,"Proxy settings",copy_string(type),copy_string("kCFProxyTypeKey"));
    if(host)map_set(proxy,"Proxy settings",host,copy_string("kCFProxyHostNameKey"));
    if(port)map_set(proxy,"Proxy settings",port,copy_string("kCFProxyPortNumberKey"));
    return (Word)proxy;
}
static int proxy_host_match(const char *host,const char *pattern) {
    const char *star=NULL,*retry=NULL;
    while(*host){if(*pattern=='*'){star=++pattern;retry=host;}else if(*pattern=='?' || core_http_lower((unsigned char)*host)==core_http_lower((unsigned char)*pattern)){++host;++pattern;}else if(star){pattern=star;host=++retry;}else return 0;}
    while(*pattern=='*')++pattern;return !*pattern;
}
static Word proxy_for_url(struct HostObject *url,struct GuestMap *settings) {
    if(!url || !settings)return 0;
    Word host=url_component(url,"host"),scheme=url_component(url,"scheme");if(!host || !scheme)return 0;
    const char *name=string_bytes(host,"Proxy host"),*protocol=string_bytes(scheme,"Proxy scheme");
    struct Sequence *result=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    int bypass=proxy_enabled(settings,"ExcludeSimpleHostnames") && !strchr(name,'.') && !strchr(name,':');
    struct Sequence *exceptions=(void *)proxy_value(settings,"ExceptionsList");
    if(exceptions)for(size_t i=0;i<exceptions->count && !bypass;++i)bypass=proxy_host_match(name,string_bytes(exceptions->items[i],"Proxy exception"));
    Word descriptor=0;
    if(!bypass && proxy_enabled(settings,"ProxyAutoConfigEnable")){
        Word script=proxy_value(settings,"ProxyAutoConfigJavaScript"),pac=proxy_value(settings,"ProxyAutoConfigURLString");
        if(script || pac){descriptor=proxy_descriptor(script ? "kCFProxyTypeAutoConfigurationJavaScript" : "kCFProxyTypeAutoConfigurationURL",0,0);
            map_set((void *)descriptor,"Proxy settings",script ? script : url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"Proxy PAC",pac),copy_string(script ? "kCFProxyAutoConfigurationJavaScriptKey" : "kCFProxyAutoConfigurationURLKey"));}
    }
    if(!bypass && !descriptor){
        const char *prefix=!strcasecmp(protocol,"https") ? "HTTPS" : !strcasecmp(protocol,"http") ? "HTTP" : "";
        char enabled[24],address[24],port_key[24];snprintf(enabled,sizeof(enabled),"%sEnable",prefix);snprintf(address,sizeof(address),"%sProxy",prefix);snprintf(port_key,sizeof(port_key),"%sPort",prefix);
        if(*prefix && proxy_enabled(settings,enabled)){Word address_value=proxy_value(settings,address),port=proxy_value(settings,port_key);if(address_value && port && numeric_integer((void *)port)>0 && numeric_integer((void *)port)<=65535)descriptor=proxy_descriptor("kCFProxyTypeHTTP",address_value,port);}
        if(!descriptor && proxy_enabled(settings,"SOCKSEnable")){Word address_value=proxy_value(settings,"SOCKSProxy"),port=proxy_value(settings,"SOCKSPort");if(address_value && port && numeric_integer((void *)port)>0 && numeric_integer((void *)port)<=65535)descriptor=proxy_descriptor("kCFProxyTypeSOCKS",address_value,port);}
    }
    sequence_add(result,"Proxy settings",descriptor ? descriptor : proxy_descriptor("kCFProxyTypeNone",0,0));return (Word)result;
}
static Word proxy_from_script(Word script,Word url,Word *error) {
    if(error)*error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"PAC evaluation",copy_string("kCFErrorDomainCFNetwork"),script && url ? 308 : 303,0);
    return 0;
}
static Word resolve_http_message_import(const char *name) {
#define HTTP_MESSAGE(api,fn) if (!strcmp(name,"_" #api)) return (Word)fn
    HTTP_MESSAGE(CFHTTPMessageCreateEmpty,core_http_new); HTTP_MESSAGE(CFHTTPMessageCreateRequest,core_http_create_request);
    HTTP_MESSAGE(CFHTTPMessageCreateResponse,core_http_create_response); HTTP_MESSAGE(CFHTTPMessageCreateCopy,core_http_create_copy);
    HTTP_MESSAGE(CFHTTPMessageAppendBytes,core_http_append); HTTP_MESSAGE(CFHTTPMessageIsHeaderComplete,core_http_complete);
    HTTP_MESSAGE(CFHTTPMessageIsRequest,core_http_is_request); HTTP_MESSAGE(CFHTTPMessageGetResponseStatusCode,core_http_status);
    HTTP_MESSAGE(CFHTTPMessageCopyAllHeaderFields,core_http_copy_headers); HTTP_MESSAGE(CFHTTPMessageCopyHeaderFieldValue,core_http_copy_header);
    HTTP_MESSAGE(CFHTTPMessageSetHeaderFieldValue,core_http_set_header); HTTP_MESSAGE(CFHTTPMessageCopySerializedMessage,core_http_serialize);
    HTTP_MESSAGE(CFHTTPMessageCopyBody,core_http_copy_body); HTTP_MESSAGE(CFHTTPMessageSetBody,core_http_set_body);
    HTTP_MESSAGE(CFHTTPMessageCopyVersion,core_http_copy_version); HTTP_MESSAGE(CFHTTPMessageCopyRequestMethod,core_http_copy_method);
    HTTP_MESSAGE(CFHTTPMessageCopyRequestURL,core_http_copy_url); HTTP_MESSAGE(CFHTTPMessageCopyResponseStatusLine,core_http_copy_status_line);
    HTTP_MESSAGE(CFHTTPMessageGetTypeID,core_http_type);
    HTTP_MESSAGE(CFNetworkCopySystemProxySettings,proxy_settings);HTTP_MESSAGE(CFNetworkCopyProxiesForURL,proxy_for_url);
    HTTP_MESSAGE(CFNetworkCopyProxiesForAutoConfigurationScript,proxy_from_script);
#undef HTTP_MESSAGE
    return 0;
}

struct CoreCookie { Word isa,properties,name,value,domain,path,expires; unsigned secure,http_only; Word version; };
static Word cookie_alloc(Word cls) {
    struct CoreCookie *cookie=calloc(1,sizeof(*cookie)); if (!cookie) stop("Allocation failed","NSHTTPCookie");
    cookie->isa=cls; return (Word)cookie;
}
static Word cookie_property(struct GuestMap *properties,const char *key) { return map_get(properties,"NSHTTPCookie properties",copy_string(key)); }
static unsigned cookie_property_bool(Word value) {
    if (!value) return 0;
    Word cls=read_word(value);
    if (cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) return text_boolean(value,"boolValue")!=0;
    return numeric_bool((void *)value)!=0;
}
static Word cookie_init(struct CoreCookie *cookie,const char *selector,struct GuestMap *properties) {
    if (!properties || (properties->isa!=external_class("_OBJC_CLASS_$_NSDictionary") && properties->isa!=external_class("_OBJC_CLASS_$_NSMutableDictionary"))) { free(cookie); return 0; }
    Word name=cookie_property(properties,"Name"),value=cookie_property(properties,"Value"),domain=cookie_property(properties,"Domain"),path=cookie_property(properties,"Path");
    if (!domain) { Word origin=cookie_property(properties,"OriginURL"); if (origin) domain=url_component((void *)origin,"host"); }
    if (!name || !value || !domain || !core_http_token(string_bytes(name,selector)) || !*string_bytes(domain,selector) || !core_http_value_valid(string_bytes(value,selector))) { free(cookie); return 0; }
    if (!path) path=copy_string("/");
    if (*string_bytes(path,selector)!='/') { free(cookie); return 0; }
    cookie->name=text_copy(name,"copy"); cookie->value=text_copy(value,"copy"); cookie->domain=text_copy(domain,"copy"); cookie->path=text_copy(path,"copy");
    cookie->properties=map_copy(properties,"copy"); struct GuestMap *snapshot=(void *)cookie->properties;
    map_set(snapshot,selector,cookie->name,copy_string("Name")); map_set(snapshot,selector,cookie->value,copy_string("Value"));
    map_set(snapshot,selector,cookie->domain,copy_string("Domain")); map_set(snapshot,selector,cookie->path,copy_string("Path"));
    cookie->secure=cookie_property_bool(cookie_property(properties,"Secure")); cookie->http_only=cookie_property_bool(cookie_property(properties,"HttpOnly"));
    Word version=cookie_property(properties,"Version");
    if (version) {
        Word cls=read_word(version);
        cookie->version=(cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) ? integer_from_string(version,"integerValue") : numeric_integer((void *)version);
    }
    cookie->expires=cookie_property(properties,"Expires");
    return (Word)cookie;
}
static Word cookie_from_properties(Word cls,const char *selector,struct GuestMap *properties) { return cookie_init((void *)cookie_alloc(cls),selector,properties); }
static struct HostObject shared_cookie_storage;
static pthread_mutex_t cookie_storage_mutex=PTHREAD_MUTEX_INITIALIZER;
static Word cookie_storage_shared(Word cls) {
    pthread_mutex_lock(&cookie_storage_mutex);
    if(!shared_cookie_storage.isa){shared_cookie_storage.isa=cls;shared_cookie_storage.content[0]=2;shared_cookie_storage.content[1]=1;}
    pthread_mutex_unlock(&cookie_storage_mutex);return (Word)&shared_cookie_storage;
}
static Word cookie_storage_init(Word object) {
    struct HostObject *storage=(void *)object;pthread_mutex_lock(&cookie_storage_mutex);
    if(!storage->content[1]){storage->content[0]=2;storage->content[1]=1;}
    pthread_mutex_unlock(&cookie_storage_mutex);return object;
}
static Word cookie_storage_transport(Word object,const char *operation,Word payload) {
    JNIEnv *env=attached_environment("Cookie storage");static jmethodID command;
    jmethodID method=__atomic_load_n(&command,__ATOMIC_ACQUIRE);
    if(!method){jclass cls=(*env)->GetObjectClass(env,activity);method=(*env)->GetMethodID(env,cls,"cookieCommand","(JLjava/lang/String;Ljava/lang/String;)Ljava/lang/String;");(*env)->DeleteLocalRef(env,cls);__atomic_store_n(&command,method,__ATOMIC_RELEASE);}
    Word encoded=json_encode(0,"Cookie storage",payload,0,NULL);struct HostObject *bytes=(void *)encoded;
    jstring data=http_java_string(env,text_decode(external_class("_OBJC_CLASS_$_NSString"),(void *)data_buffer(bytes),bytes->content[0],4)),action=(*env)->NewStringUTF(env,operation);
    jstring result=(*env)->CallObjectMethod(env,activity,method,(jlong)cookie_storage_init(object),action,data);
    (*env)->DeleteLocalRef(env,action);(*env)->DeleteLocalRef(env,data);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);if(result)(*env)->DeleteLocalRef(env,result);stop("Cookie storage operation failed",operation);}
    Word answer=result ? encoded_string(string_from_java(env,result,"Cookie storage"),"Cookie storage",4) : 0;
    if(result)(*env)->DeleteLocalRef(env,result);return answer ? json_parse(0,"Cookie storage",(void *)answer,0,NULL) : 0;
}
static Word cookie_storage_list(Word object,const char *selector,Word url) {
    struct GuestMap *payload=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    if(url)map_set(payload,selector,url_absolute((void *)url,selector),copy_string("url"));
    struct Sequence *properties=(void *)cookie_storage_transport(object,url ? "url" : "cookies",(Word)payload);
    if(!properties)return 0;
    struct Sequence *cookies=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    for(size_t i=0;i<properties->count;++i){struct GuestMap *map=(void *)properties->items[i];Word expires=map_get(map,selector,copy_string("Expires"));
        if(expires)map_set(map,selector,date_epoch(external_class("_OBJC_CLASS_$_NSDate"),selector,numeric_double((void *)expires)),copy_string("Expires"));
        Word cookie=cookie_from_properties(external_class("_OBJC_CLASS_$_NSHTTPCookie"),selector,map);if(cookie)sequence_add(cookies,selector,cookie);
    }
    return (Word)cookies;
}
static Word cookie_storage_all(Word object,const char *selector) { return cookie_storage_list(object,selector,0); }
static void cookie_storage_change(Word object,const char *selector,struct CoreCookie *cookie) {
    if(!cookie || cookie->isa!=external_class("_OBJC_CLASS_$_NSHTTPCookie"))stop("Invalid cookie",selector);
    struct GuestMap *payload=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    const char *keys[]={"Name","Value","Domain","Path"};Word values[]={cookie->name,cookie->value,cookie->domain,cookie->path};
    for(size_t i=0;i<COUNT(keys);++i)map_set(payload,selector,values[i],copy_string(keys[i]));
    map_set(payload,selector,number_bool(external_class("_OBJC_CLASS_$_NSNumber"),0,cookie->secure),copy_string("Secure"));
    map_set(payload,selector,number_bool(external_class("_OBJC_CLASS_$_NSNumber"),0,cookie->http_only),copy_string("HttpOnly"));
    map_set(payload,selector,number_integer(cookie->version),copy_string("Version"));
    if(cookie->expires)map_set(payload,selector,real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),0,date_seconds(cookie->expires)),copy_string("Expires"));
    cookie_storage_transport(object,!strcmp(selector,"deleteCookie:") ? "delete" : "set",(Word)payload);
}
static Word cookie_storage_policy(Word object) { cookie_storage_init(object);return __atomic_load_n(&((struct HostObject *)object)->content[0],__ATOMIC_ACQUIRE); }
static void cookie_storage_set_policy(Word object,const char *selector,Word policy) {
    if(policy>2)stop("Invalid cookie acceptance policy",selector);
    struct GuestMap *payload=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));map_set(payload,selector,number_integer(policy),copy_string("policy"));
    cookie_storage_transport(object,"policy",(Word)payload);__atomic_store_n(&((struct HostObject *)object)->content[0],policy,__ATOMIC_RELEASE);
}
static void cookie_storage_set_many(Word object,const char *selector,struct Sequence *cookies,struct HostObject *url,struct HostObject *main_url) {
    if(!cookies || !url)return;Word policy=cookie_storage_policy(object);
    if(policy==1)return;
    if(policy==2){if(!main_url)return;Word host=url_component(url,"host"),main_host=url_component(main_url,"host");if(!host || !main_host)return;
        const char *a=string_bytes(host,selector),*b=string_bytes(main_host,selector);size_t x=strlen(a),y=strlen(b);
        if(strcasecmp(a,b) && !(x>y && a[x-y-1]=='.' && !strcasecmp(a+x-y,b)))return;
    }
    for(size_t i=0;i<cookies->count;++i)cookie_storage_change(object,"setCookie:",(void *)cookies->items[i]);
}
static Method cookie_storage_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if(!receiver || strcmp(receiver,"_OBJC_CLASS_$_NSHTTPCookieStorage"))return NULL;
    if(class_receiver){if(!strcmp(selector,"sharedHTTPCookieStorage"))return (Method)cookie_storage_shared;}
    else{
        if(!strcmp(selector,"init"))return (Method)cookie_storage_init;
        if(!strcmp(selector,"cookies"))return (Method)cookie_storage_all;if(!strcmp(selector,"cookiesForURL:"))return (Method)cookie_storage_list;
        if(!strcmp(selector,"setCookie:") || !strcmp(selector,"deleteCookie:"))return (Method)cookie_storage_change;
        if(!strcmp(selector,"cookieAcceptPolicy"))return (Method)cookie_storage_policy;if(!strcmp(selector,"setCookieAcceptPolicy:"))return (Method)cookie_storage_set_policy;
        if(!strcmp(selector,"setCookies:forURL:mainDocumentURL:"))return (Method)cookie_storage_set_many;
    }
    return NULL;
}
static Word cookie_get(struct CoreCookie *cookie,const char *selector) {
    if (!strcmp(selector,"name")) return cookie->name;
    if (!strcmp(selector,"value")) return cookie->value;
    if (!strcmp(selector,"domain")) return cookie->domain;
    if (!strcmp(selector,"path")) return cookie->path;
    if (!strcmp(selector,"properties")) return cookie->properties;
    if (!strcmp(selector,"expiresDate")) return cookie->expires;
    if (!strcmp(selector,"isSecure")) return cookie->secure;
    if (!strcmp(selector,"isHTTPOnly")) return cookie->http_only;
    if (!strcmp(selector,"isSessionOnly")) return !cookie->expires;
    return cookie->version;
}
static Word cookie_request_headers(Word cls,const char *selector,struct Sequence *cookies) {
    struct GuestMap *headers=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    if (!cookies || !cookies->count) return (Word)headers;
    char *text=NULL; size_t length=0; FILE *stream=open_memstream(&text,&length); if (!stream) stop("Allocation failed",selector);
    Word mutation=cookies->mutation;
    for (size_t i=0;i<cookies->count;++i) {
        Word object=cookies->items[i]; Method name=find_implementation(object,"name"),value=find_implementation(object,"value"),version=find_implementation(object,"version");
        if (!name || !value) { fclose(stream); free(text); stop("Invalid cookie object",selector); }
        if (version && version(object,"version",0,0,0,0)) { fclose(stream); free(text); stop("RFC2109 cookie headers not supported",selector); }
        Word n=name(object,"name",0,0,0,0),v=value(object,"value",0,0,0,0);
        if (!n || !v || !core_http_token(string_bytes(n,selector)) || !core_http_value_valid(string_bytes(v,selector))) { fclose(stream); free(text); stop("Invalid cookie fields",selector); }
        if (i) fputs("; ",stream);
        fprintf(stream,"%s=%s",string_bytes(n,selector),string_bytes(v,selector));
        if (cookies->mutation!=mutation) { fclose(stream); free(text); stop("Cookies mutated during serialization",selector); }
    }
    int failed=ferror(stream); if (fclose(stream)) failed=1;
    if (failed) { free(text); stop("Cookie header serialization failed",selector); }
    map_set(headers,selector,copy_string(text),copy_string("Cookie")); free(text); return (Word)headers;
}
struct CachedResponse { Word isa,response,data,info,policy; };
struct ResponseCacheEntry { Word key,value; uint64_t hash; size_t memory,disk; struct ResponseCacheEntry *previous,*next; };
struct ResponseCache { Word object; size_t memory_capacity,disk_capacity,memory_used,disk_used; char *directory; pthread_mutex_t mutex; struct ResponseCacheEntry *first,*last; };
static Word shared_response_cache;
static pthread_mutex_t shared_cache_mutex=PTHREAD_MUTEX_INITIALIZER;
#define CACHE_LOCK(c) \
    pthread_mutex_lock(&(c)->mutex); \
    struct GuestCallBoundary cache_boundary={.previous=active_guest_call_boundary}; \
    active_guest_call_boundary=&cache_boundary; \
    if(setjmp(cache_boundary.environment)){ \
        active_guest_call_boundary=cache_boundary.previous;pthread_mutex_unlock(&(c)->mutex); \
        char failure[sizeof(status)];memcpy(failure,status,sizeof(failure));stop("URL cache operation failed",failure); \
    }
#define CACHE_UNLOCK(c) active_guest_call_boundary=cache_boundary.previous;pthread_mutex_unlock(&(c)->mutex)
static Word cached_response_init(Word object,const char *selector,Word response,Word data,Word info,Word policy) {
    if(!response || !data || policy>2)stop("Invalid cached response",selector);
    struct CachedResponse *r=(void *)object;r->response=cf_retain(response);r->data=data_copy(data,"copy");r->info=info ? map_copy((void *)info,"copy") : 0;r->policy=policy;return object;
}
static Word cached_response_init_plain(Word object,const char *selector,Word response,Word data) { return cached_response_init(object,selector,response,data,0,0); }
static Word cached_response_property(struct CachedResponse *r,const char *selector) { return !strcmp(selector,"response") ? r->response : !strcmp(selector,"data") ? r->data : !strcmp(selector,"userInfo") ? r->info : r->policy; }
static Word cache_request_key(Word object) {
    struct Request *r=(void *)object;if(!r || !r->url || r->body_stream)return 0;
    struct GuestMap *key=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(key,"Cache request",url_absolute((void *)r->url,"Cache request"),copy_string("url"));map_set(key,"Cache request",r->method,copy_string("method"));
    if(r->headers)map_set(key,"Cache request",r->headers,copy_string("headers"));if(r->body)map_set(key,"Cache request",r->body,copy_string("body"));
    return property_list_encode(0,"Cache request",(Word)key,200,0,NULL);
}
static uint64_t cache_key_hash(Word object) { struct HostObject *data=(void *)object;const unsigned char *p=(void *)data_buffer(data);uint64_t hash=14695981039346656037ULL;for(size_t i=0;i<data->content[0];++i)hash=(hash^p[i])*1099511628211ULL;return hash; }
static int cache_key_equal(Word a,Word b) { return a && b && read_word(a+8)==read_word(b+8) && !memcmp((void *)data_buffer((void *)a),(void *)data_buffer((void *)b),read_word(a+8)); }
static Word cache_filename(struct ResponseCache *cache,uint64_t hash) { char file[32];snprintf(file,sizeof(file),"%016llx.cache",(unsigned long long)hash);return append_path(copy_string(cache->directory),"URLCache",copy_string(file)); }
static struct ResponseCache *response_cache(Word object) { struct HostObject *o=(void *)object;if(!o || !o->content[0])stop("Uninitialized URL cache","NSURLCache");return (void *)o->content[0]; }
static void cache_unlink_entry(struct ResponseCache *c,struct ResponseCacheEntry *e) {
    if(e->previous)e->previous->next=e->next;else c->first=e->next;if(e->next)e->next->previous=e->previous;else c->last=e->previous;
    c->memory_used-=e->memory;c->disk_used-=e->disk;if(e->disk)unlink(string_bytes(cache_filename(c,e->hash),"URLCache eviction"));
    if(e->key)cf_release(e->key);if(e->value)cf_release(e->value);free(e);
}
static void cache_touch(struct ResponseCache *c,struct ResponseCacheEntry *e) {
    if(c->first==e)return;if(e->previous)e->previous->next=e->next;else c->first=e->next;if(e->next)e->next->previous=e->previous;else c->last=e->previous;
    e->previous=NULL;e->next=c->first;if(c->first)c->first->previous=e;else c->last=e;c->first=e;
}
static void cache_trim(struct ResponseCache *c) {
    for(struct ResponseCacheEntry *e=c->last,*previous;e && (c->memory_used>c->memory_capacity || c->disk_used>c->disk_capacity);e=previous) {
        previous=e->previous;
        if(c->memory_used>c->memory_capacity && e->memory){c->memory_used-=e->memory;e->memory=0;Word value=e->value;e->value=0;if(value)cf_release(value);}
        if(c->disk_used>c->disk_capacity && e->disk){unlink(string_bytes(cache_filename(c,e->hash),"URLCache eviction"));c->disk_used-=e->disk;e->disk=0;}
        if(!e->memory && !e->disk)cache_unlink_entry(c,e);
    }
}
static Word cache_decode_response(Word encoded) {
    struct GuestMap *map=(void *)property_list_decode(0,"URLCache response",(void *)encoded,0,NULL,NULL);if(!map)return 0;
    Word url=map_get(map,"URLCache",copy_string("url")),data=map_get(map,"URLCache",copy_string("data"));if(!url || !data)return 0;
    struct HttpResponse *response=calloc(1,sizeof(*response));if(!response)stop("Allocation failed","URLCache response");
    response->isa=external_class("_OBJC_CLASS_$_NSHTTPURLResponse");response->url=url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"URLCache",url);
    response->headers=map_get(map,"URLCache",copy_string("headers"));response->mime=map_get(map,"URLCache",copy_string("mime"));response->encoding=map_get(map,"URLCache",copy_string("encoding"));
    response->status=numeric_integer((void *)map_get(map,"URLCache",copy_string("status")));response->length=numeric_integer((void *)map_get(map,"URLCache",copy_string("length")));
    return cached_response_init(host_allocate(external_class("_OBJC_CLASS_$_NSCachedURLResponse")),"URLCache",(Word)response,data,map_get(map,"URLCache",copy_string("info")),0);
}
static Word cache_encode_response(struct CachedResponse *r) {
    struct HttpResponse *response=(void *)r->response;struct GuestMap *map=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(map,"URLCache",url_absolute((void *)response->url,"URLCache"),copy_string("url"));map_set(map,"URLCache",r->data,copy_string("data"));
    map_set(map,"URLCache",number_integer(response->status),copy_string("status"));map_set(map,"URLCache",number_integer(response->length),copy_string("length"));
    if(response->headers)map_set(map,"URLCache",response->headers,copy_string("headers"));if(response->mime)map_set(map,"URLCache",response->mime,copy_string("mime"));if(response->encoding)map_set(map,"URLCache",response->encoding,copy_string("encoding"));if(r->info)map_set(map,"URLCache",r->info,copy_string("info"));
    return property_list_encode(0,"URLCache response",(Word)map,200,0,NULL);
}
static Word response_cache_init(Word object,const char *selector,size_t memory,size_t disk,Word path) {
    struct ResponseCache *c=calloc(1,sizeof(*c));if(!c)stop("Allocation failed",selector);c->object=object;c->memory_capacity=memory;c->disk_capacity=disk;pthread_mutex_init(&c->mutex,NULL);
    Word logical=path ? path : append_path(directory_path(13),selector,copy_string("URLCache"));const char *text=string_bytes(logical,selector);
    if(*text!='/')logical=append_path(directory_path(13),selector,logical);
    c->directory=native_path(string_bytes(logical,selector));if(!c->directory || !writable_sandbox_path(c->directory)){free(c->directory);pthread_mutex_destroy(&c->mutex);free(c);stop("Invalid cache directory",selector);}
    ((struct HostObject *)object)->content[0]=(Word)c;
    if(disk) {
        Word location=copy_string(c->directory);if(!manager_create_path((Word)&file_manager,selector,location,1,0,NULL))stop("Cannot create cache directory",selector);
        DIR *folder=opendir(c->directory);if(folder){struct dirent *item;while((item=readdir(folder))){if(strlen(item->d_name)!=22 || strcmp(item->d_name+16,".cache"))continue;char *end;uint64_t hash=strtoull(item->d_name,&end,16);if(end!=item->d_name+16)continue;
            Word file=cache_filename(c,hash);struct stat st;if(stat(string_bytes(file,selector),&st) || !S_ISREG(st.st_mode) || st.st_size<=0)continue;
            struct ResponseCacheEntry *e=calloc(1,sizeof(*e));if(!e)stop("Allocation failed",selector);e->hash=hash;e->disk=st.st_size;e->previous=c->last;if(c->last)c->last->next=e;else c->first=e;c->last=e;c->disk_used+=e->disk;
        }closedir(folder);}
        cache_trim(c);
    }
    return object;
}
static Word response_cache_shared(Word cls,const char *selector) {
    pthread_mutex_lock(&shared_cache_mutex);Word object=shared_response_cache;pthread_mutex_unlock(&shared_cache_mutex);if(object)return object;
    Word created=response_cache_init(host_allocate(cls),selector,4*1024*1024,20*1024*1024,0);
    pthread_mutex_lock(&shared_cache_mutex);if(!shared_response_cache)shared_response_cache=created;object=shared_response_cache;pthread_mutex_unlock(&shared_cache_mutex);return object;
}
static void response_cache_set_shared(Word cls,const char *selector,Word cache) { pthread_mutex_lock(&shared_cache_mutex);shared_response_cache=cache;pthread_mutex_unlock(&shared_cache_mutex); }
static Word response_cache_get(Word object,const char *selector,Word request) {
    Word key=cache_request_key(request);if(!key)return 0;uint64_t hash=cache_key_hash(key);struct ResponseCache *c=response_cache(object);Word result=0;
    CACHE_LOCK(c);
    for(struct ResponseCacheEntry *e=c->first;e;e=e->next)if(e->hash==hash) {
        Word record=0;if(!e->value && e->disk){Word bytes=data_from_file(external_class("_OBJC_CLASS_$_NSData"),selector,cache_filename(c,hash));record=bytes ? property_list_decode(0,selector,(void *)bytes,0,NULL,NULL) : 0;if(record && !e->key)e->key=map_get((void *)record,selector,copy_string("key"));}
        if(!cache_key_equal(e->key,key))continue;
        result=e->value;if(!result && record)result=cache_decode_response(map_get((void *)record,selector,copy_string("value")));
        if(result){cf_retain(result);cache_touch(c,e);size_t amount=read_word(((struct CachedResponse *)result)->data+8);if(amount<SIZE_MAX-sizeof(struct CachedResponse))amount+=sizeof(struct CachedResponse);else amount=SIZE_MAX;if(!e->value && amount<=c->memory_capacity && amount<=SIZE_MAX-c->memory_used){e->value=cf_retain(result);e->memory=amount;c->memory_used+=amount;cache_trim(c);}}break;
    }
    CACHE_UNLOCK(c);cf_release(key);return result;
}
static void response_cache_store(Word object,const char *selector,Word value,Word request) {
    if(!value)return;Word key=cache_request_key(request);if(!key)return;struct CachedResponse *r=(void *)value;struct ResponseCache *c=response_cache(object);uint64_t hash=cache_key_hash(key);size_t amount=read_word(r->data+8);if(amount<SIZE_MAX-sizeof(*r))amount+=sizeof(*r);else amount=SIZE_MAX;
    Word encoded=r->policy==0 && c->disk_capacity ? cache_encode_response(r) : 0;
    CACHE_LOCK(c);
    for(struct ResponseCacheEntry *e=c->first,*next;e;e=next){next=e->next;if(e->hash==hash)cache_unlink_entry(c,e);}
    if(r->policy==2){CACHE_UNLOCK(c);cf_release(key);return;}
    struct ResponseCacheEntry *e=calloc(1,sizeof(*e));if(!e)stop("Allocation failed",selector);e->key=key;e->hash=hash;
    if(amount<=c->memory_capacity && amount<=SIZE_MAX-c->memory_used){e->value=value;e->memory=amount;c->memory_used+=amount;cf_retain(value);}
    if(encoded){struct GuestMap *record=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));map_set(record,selector,key,copy_string("key"));map_set(record,selector,encoded,copy_string("value"));Word bytes=property_list_encode(0,selector,(Word)record,200,0,NULL);
        if(bytes && read_word(bytes+8)<=c->disk_capacity && data_write_file((void *)bytes,selector,cache_filename(c,hash),1,NULL)){e->disk=read_word(bytes+8);c->disk_used+=e->disk;}}
    e->next=c->first;if(c->first)c->first->previous=e;else c->last=e;c->first=e;
    if(!e->memory && !e->disk)cache_unlink_entry(c,e);
    cache_trim(c);
    CACHE_UNLOCK(c);
}
static void response_cache_remove(Word object,const char *selector,Word request) {
    struct ResponseCache *c=response_cache(object);Word key=request ? cache_request_key(request) : 0;uint64_t hash=key ? cache_key_hash(key) : 0;
    CACHE_LOCK(c);for(struct ResponseCacheEntry *e=c->first,*next;e;e=next){next=e->next;if(!request || (key && e->hash==hash))cache_unlink_entry(c,e);}CACHE_UNLOCK(c);if(key)cf_release(key);
}
static void response_cache_clear(Word object,const char *selector) { response_cache_remove(object,selector,0); }
static Word response_cache_capacity(Word object,const char *selector) { struct ResponseCache *c=response_cache(object);pthread_mutex_lock(&c->mutex);Word value=!strcmp(selector,"memoryCapacity") ? c->memory_capacity : !strcmp(selector,"diskCapacity") ? c->disk_capacity : !strcmp(selector,"currentMemoryUsage") ? c->memory_used : c->disk_used;pthread_mutex_unlock(&c->mutex);return value; }
static void response_cache_set_capacity(Word object,const char *selector,size_t size) { struct ResponseCache *c=response_cache(object);CACHE_LOCK(c);if(!strcmp(selector,"setMemoryCapacity:"))c->memory_capacity=size;else c->disk_capacity=size;cache_trim(c);CACHE_UNLOCK(c); }
#undef CACHE_LOCK
#undef CACHE_UNLOCK
static Method response_cache_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if(!receiver)return NULL;
    if(!strcmp(receiver,"_OBJC_CLASS_$_NSCachedURLResponse") && !class_receiver){if(!strcmp(selector,"initWithResponse:data:userInfo:storagePolicy:"))return (Method)cached_response_init;if(!strcmp(selector,"initWithResponse:data:"))return (Method)cached_response_init_plain;
        if(!strcmp(selector,"response") || !strcmp(selector,"data") || !strcmp(selector,"userInfo") || !strcmp(selector,"storagePolicy"))return (Method)cached_response_property;if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)identity;}
    if(!strcmp(receiver,"_OBJC_CLASS_$_NSURLCache")) {
        if(class_receiver){if(!strcmp(selector,"sharedURLCache"))return (Method)response_cache_shared;if(!strcmp(selector,"setSharedURLCache:"))return (Method)response_cache_set_shared;}
        else {if(!strcmp(selector,"initWithMemoryCapacity:diskCapacity:diskPath:"))return (Method)response_cache_init;if(!strcmp(selector,"cachedResponseForRequest:"))return (Method)response_cache_get;if(!strcmp(selector,"storeCachedResponse:forRequest:"))return (Method)response_cache_store;
            if(!strcmp(selector,"removeCachedResponseForRequest:"))return (Method)response_cache_remove;if(!strcmp(selector,"removeAllCachedResponses"))return (Method)response_cache_clear;
            if(!strcmp(selector,"memoryCapacity") || !strcmp(selector,"diskCapacity") || !strcmp(selector,"currentMemoryUsage") || !strcmp(selector,"currentDiskUsage"))return (Method)response_cache_capacity;
            if(!strcmp(selector,"setMemoryCapacity:") || !strcmp(selector,"setDiskCapacity:"))return (Method)response_cache_set_capacity;}
    }
    return NULL;
}
static Method cookie_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || strcmp(receiver,"_OBJC_CLASS_$_NSHTTPCookie")) return NULL;
    if (class_receiver) {
        if (!strcmp(selector,"alloc") || !strcmp(selector,"allocWithZone:")) return (Method)cookie_alloc;
        if (!strcmp(selector,"cookieWithProperties:")) return (Method)cookie_from_properties;
        if (!strcmp(selector,"requestHeaderFieldsWithCookies:")) return (Method)cookie_request_headers;
    } else {
        if (!strcmp(selector,"initWithProperties:")) return (Method)cookie_init;
        if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:")) return (Method)identity;
        if (!strcmp(selector,"name") || !strcmp(selector,"value") || !strcmp(selector,"domain") || !strcmp(selector,"path") || !strcmp(selector,"properties") || !strcmp(selector,"expiresDate") || !strcmp(selector,"isSecure") || !strcmp(selector,"isHTTPOnly") || !strcmp(selector,"isSessionOnly") || !strcmp(selector,"version")) return (Method)cookie_get;
    }
    return NULL;
}
