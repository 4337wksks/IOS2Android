 
struct I2ATLS {
    Word tag; unsigned references;
    void *configuration,*ssl; Word connection;
    int (*read)(Word,void *,size_t *);
    int (*write)(Word,const void *,size_t *);
    char *hostname; int state,break_auth,auth_notified,io_status;
    size_t plaintext_read,plaintext_written;
    FILE *request_capture,*response_capture;
    struct Trust *trust;
};
#define TLS_API(X) \
    X(const void *,TLS_client_method,(void)) \
    X(void *,SSL_CTX_new,(const void *)) \
    X(void,SSL_CTX_free,(void *)) \
    X(void,SSL_CTX_set_verify,(void *,int,void *)) \
    X(void,SSL_CTX_set_cert_verify_callback,(void *,int (*)(void *,void *),void *)) \
    X(void *,SSL_new,(void *)) \
    X(void,SSL_free,(void *)) \
    X(int,SSL_set_ex_data,(void *,int,void *)) \
    X(void *,SSL_get_ex_data,(const void *,int)) \
    X(int,SSL_get_ex_data_X509_STORE_CTX_idx,(void)) \
    X(void,SSL_set_bio,(void *,void *,void *)) \
    X(void,SSL_set_connect_state,(void *)) \
    X(long,SSL_ctrl,(void *,int,long,void *)) \
    X(int,SSL_do_handshake,(void *)) \
    X(int,SSL_get_error,(const void *,int)) \
    X(int,SSL_read_ex,(void *,void *,size_t,size_t *)) \
    X(int,SSL_write_ex,(void *,const void *,size_t,size_t *)) \
    X(int,SSL_shutdown,(void *)) \
    X(const char *,SSL_get_version,(const void *)) \
    X(void *,BIO_meth_new,(int,const char *)) \
    X(int,BIO_meth_set_read,(void *,int (*)(void *,char *,int))) \
    X(int,BIO_meth_set_write,(void *,int (*)(void *,const char *,int))) \
    X(int,BIO_meth_set_ctrl,(void *,long (*)(void *,int,long,void *))) \
    X(int,BIO_meth_set_create,(void *,int (*)(void *))) \
    X(int,BIO_meth_set_destroy,(void *,int (*)(void *))) \
    X(void *,BIO_new,(const void *)) \
    X(int,BIO_free,(void *)) \
    X(void,BIO_set_data,(void *,void *)) \
    X(void *,BIO_get_data,(void *)) \
    X(void,BIO_set_init,(void *,int)) \
    X(void,BIO_set_flags,(void *,int)) \
    X(void,BIO_clear_flags,(void *,int)) \
    X(void *,X509_STORE_CTX_get0_cert,(const void *)) \
    X(void *,X509_STORE_CTX_get0_untrusted,(const void *)) \
    X(void *,X509_STORE_CTX_get_ex_data,(const void *,int)) \
    X(void,X509_STORE_CTX_set_error,(void *,int)) \
    X(int,OPENSSL_sk_num,(const void *)) \
    X(void *,OPENSSL_sk_value,(const void *,int)) \
    X(int,i2d_X509,(const void *,unsigned char **)) \
    X(int,X509_check_host,(void *,const char *,size_t,unsigned int,char **)) \
    X(int,X509_check_ip_asc,(void *,const char *,unsigned int)) \
    X(void,ERR_clear_error,(void)) \
    X(unsigned long,ERR_get_error,(void)) \
    X(void,ERR_error_string_n,(unsigned long,char *,size_t))
#define TLS_FIELD(result,name,args) result (*name)args;
static struct { TLS_API(TLS_FIELD) void *bio_method; } i2a_tls_api;
#undef TLS_FIELD
static pthread_once_t i2a_tls_once=PTHREAD_ONCE_INIT;
static int i2a_tls_bio_create(void *bio) { i2a_tls_api.BIO_set_init(bio,1); return 1; }
static int i2a_tls_bio_destroy(void *bio) { if (bio) i2a_tls_api.BIO_set_data(bio,NULL); return 1; }
static long i2a_tls_bio_control(void *bio,int command,long argument,void *pointer) {
     
    return command==11 ? 1 : 0;
}
static int i2a_tls_bio_read(void *bio,char *buffer,int capacity) {
    struct I2ATLS *context=i2a_tls_api.BIO_get_data(bio);
    i2a_tls_api.BIO_clear_flags(bio,1|2|8);
    if (!context || !context->read || capacity<=0) return 0;
    size_t count=(size_t)capacity;
    int status=context->read(context->connection,buffer,&count);
    if (count>(size_t)capacity) { context->io_status=-9802; return -1; }
    context->io_status=status;
    if (count) return (int)count;
    if (!status || status==-9803) { i2a_tls_api.BIO_set_flags(bio,1|8); return -1; }
    return status==-9805 ? 0 : -1;
}
static int i2a_tls_bio_write(void *bio,const char *buffer,int capacity) {
    struct I2ATLS *context=i2a_tls_api.BIO_get_data(bio);
    i2a_tls_api.BIO_clear_flags(bio,1|2|8);
    if (!context || !context->write || capacity<=0) return 0;
    size_t count=(size_t)capacity;
    int status=context->write(context->connection,buffer,&count);
    if (count>(size_t)capacity) { context->io_status=-9802; return -1; }
    context->io_status=status;
    if (count) return (int)count;
    if (!status || status==-9803) { i2a_tls_api.BIO_set_flags(bio,2|8); return -1; }
    return -1;
}
static int i2a_tls_verify(void *,void *);
static void i2a_tls_initialize(void) {
    void *crypto=dlopen("libi2a_crypto.so",RTLD_NOW|RTLD_GLOBAL);
    if (!crypto) stop("TLS crypto library load failed",dlerror());
    void *ssl=dlopen("libi2a_ssl.so",RTLD_NOW|RTLD_LOCAL);
    if (!ssl) stop("TLS library load failed",dlerror());
#define TLS_LOAD(result,name,args) i2a_tls_api.name=dlsym(ssl,#name); if (!i2a_tls_api.name) i2a_tls_api.name=dlsym(crypto,#name); if (!i2a_tls_api.name) stop("OpenSSL API unavailable",#name);
    TLS_API(TLS_LOAD)
#undef TLS_LOAD
    i2a_tls_api.bio_method=i2a_tls_api.BIO_meth_new(0x400|0x80,"I2A original SecureTransport IO");
    if (!i2a_tls_api.bio_method || !i2a_tls_api.BIO_meth_set_read(i2a_tls_api.bio_method,i2a_tls_bio_read) ||
        !i2a_tls_api.BIO_meth_set_write(i2a_tls_api.bio_method,i2a_tls_bio_write) ||
        !i2a_tls_api.BIO_meth_set_ctrl(i2a_tls_api.bio_method,i2a_tls_bio_control) ||
        !i2a_tls_api.BIO_meth_set_create(i2a_tls_api.bio_method,i2a_tls_bio_create) ||
        !i2a_tls_api.BIO_meth_set_destroy(i2a_tls_api.bio_method,i2a_tls_bio_destroy)) stop("TLS BIO initialization failed","OpenSSL");
    LOG("SecureTransport bridge ready: encrypted OpenSSL TLS with Android peer verification");
}
static int i2a_tls_valid(struct I2ATLS *context) { return context && context->tag==(Word)&secure_transport_tag; }
static int i2a_tls_custom_ca_bypass(struct I2ATLS *context) {
    return runtime_extension.use_platform_trust && runtime_extension.use_platform_trust(context->hostname);
}
static void secure_transport_retain(Word object) { __atomic_add_fetch(&((struct I2ATLS *)object)->references,1,__ATOMIC_RELAXED); }
static void secure_transport_release(Word object) {
    struct I2ATLS *context=(void *)object;
    if (__atomic_sub_fetch(&context->references,1,__ATOMIC_ACQ_REL)) return;
    if (context->ssl) i2a_tls_api.SSL_free(context->ssl);
    if (context->configuration) i2a_tls_api.SSL_CTX_free(context->configuration);
    if (context->trust) trust_release((Word)context->trust);
    if (context->request_capture) fclose(context->request_capture);
    if (context->response_capture) fclose(context->response_capture);
    free(context->hostname); free(context);
}
static Word i2a_ssl_create(Word allocator,int side,int type) {
    if (allocator || side!=1 || type!=0) return 0;
    pthread_once(&i2a_tls_once,i2a_tls_initialize);
    struct I2ATLS *context=calloc(1,sizeof(*context)); if (!context) return 0;
    context->tag=(Word)&secure_transport_tag; context->references=1;
    context->configuration=i2a_tls_api.SSL_CTX_new(i2a_tls_api.TLS_client_method());
    if (!context->configuration) { secure_transport_release((Word)context); return 0; }
    i2a_tls_api.SSL_CTX_set_verify(context->configuration,1,NULL);
    i2a_tls_api.SSL_CTX_set_cert_verify_callback(context->configuration,i2a_tls_verify,context);
    context->ssl=i2a_tls_api.SSL_new(context->configuration);
    void *bio=context->ssl ? i2a_tls_api.BIO_new(i2a_tls_api.bio_method) : NULL;
    if (!bio) { secure_transport_release((Word)context); return 0; }
    i2a_tls_api.BIO_set_data(bio,context);
    i2a_tls_api.SSL_set_bio(context->ssl,bio,bio);
    i2a_tls_api.SSL_set_ex_data(context->ssl,0,context);
    i2a_tls_api.SSL_set_connect_state(context->ssl);
    i2a_tls_api.SSL_ctrl(context->ssl,33,3,NULL);  
    if (!i2a_tls_api.SSL_ctrl(context->ssl,123,0x303,NULL)) { secure_transport_release((Word)context); return 0; }
    return (Word)context;
}
static int i2a_ssl_io(struct I2ATLS *context,int (*read)(Word,void *,size_t *),int (*write)(Word,const void *,size_t *)) {
    if (!i2a_tls_valid(context) || context->state || !read || !write) return -50;
    context->read=read; context->write=write; return 0;
}
static int i2a_ssl_connection(struct I2ATLS *context,Word connection) {
    if (!i2a_tls_valid(context) || context->state) return -50;
    context->connection=connection; return 0;
}
static int i2a_ssl_hostname(struct I2ATLS *context,const char *hostname,size_t size) {
    if (!i2a_tls_valid(context) || context->state || !hostname || !size || size>253 || memchr(hostname,0,size)) return -50;
    char *copy=strndup(hostname,size); if (!copy) return -108;
    if (!i2a_tls_api.SSL_ctrl(context->ssl,55,0,copy)) { free(copy); return -50; }
    free(context->hostname); context->hostname=copy;
    LOG("TLS peer configured: %s customCA=%s",copy,i2a_tls_custom_ca_bypass(context) ? "Android-system" : "guest-requested-policy");
    return 0;
}
static int i2a_ssl_minimum(struct I2ATLS *context,int protocol) {
    if (!i2a_tls_valid(context) || context->state) return -50;
    int version=protocol==8 ? 0x303 : protocol==7 ? 0x302 : protocol==4 ? 0x301 : 0;
    return version && i2a_tls_api.SSL_ctrl(context->ssl,123,version,NULL) ? 0 : -50;
}
static int i2a_ssl_option(struct I2ATLS *context,int option,unsigned char enabled) {
    if (!i2a_tls_valid(context) || context->state || option!=0) return -50;
    context->break_auth=enabled!=0; return 0;
}
static struct Trust *i2a_tls_peer_trust(void *store) {
    void *leaf=i2a_tls_api.X509_STORE_CTX_get0_cert(store),*stack=i2a_tls_api.X509_STORE_CTX_get0_untrusted(store);
    if (!leaf) return NULL;
    int count=stack ? i2a_tls_api.OPENSSL_sk_num(stack) : 0,extra=1;
    if (count<0 || count>32) return NULL;
    for (int i=0;i<count;++i) if (i2a_tls_api.OPENSSL_sk_value(stack,i)==leaf) extra=0;
    JNIEnv *env=attached_environment("TLS peer certificates");
    jclass cls=(*env)->FindClass(env,"java/security/cert/X509Certificate");
    jobjectArray chain=(*env)->NewObjectArray(env,count+extra,cls,NULL); (*env)->DeleteLocalRef(env,cls);
    if (!chain) return NULL;
    int output=0,failed=0;
     
    for (int i=-1;i<count;++i) {
        void *certificate=i<0 ? leaf : i2a_tls_api.OPENSSL_sk_value(stack,i);
        if (i>=0 && certificate==leaf) continue;
        int length=i2a_tls_api.i2d_X509(certificate,NULL);
        if (length<=0 || length>1024*1024) { failed=1; break; }
        unsigned char *bytes=malloc((size_t)length),*cursor=bytes;
        if (!bytes) { failed=1; break; }
        int written=i2a_tls_api.i2d_X509(certificate,&cursor);
        jbyteArray encoded=(*env)->NewByteArray(env,length);
        jobject parsed=NULL;
        if (encoded && written==length) {
            (*env)->SetByteArrayRegion(env,encoded,0,length,(jbyte *)bytes);
            parsed=(*env)->CallObjectMethod(env,activity,certificate_method,encoded);
        }
        free(bytes);
        if (parsed && !(*env)->ExceptionCheck(env)) (*env)->SetObjectArrayElement(env,chain,output++,parsed);
        else failed=1;
        if (parsed) (*env)->DeleteLocalRef(env,parsed);
        if (encoded) (*env)->DeleteLocalRef(env,encoded);
        if (failed) break;
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); failed=1; }
    struct Trust *trust=failed ? NULL : calloc(1,sizeof(*trust));
    if (trust) { trust->tag=(Word)&trust_tag; trust->managed_references=1; trust->chain=(*env)->NewGlobalRef(env,chain); }
    (*env)->DeleteLocalRef(env,chain); return trust;
}
static int i2a_tls_verify(void *store,void *argument) {
    struct I2ATLS *context=argument;
    if (!context->trust) context->trust=i2a_tls_peer_trust(store);
    if (!context->trust || !context->hostname) { i2a_tls_api.X509_STORE_CTX_set_error(store,50); return 0; }
    if (context->break_auth && !i2a_tls_custom_ca_bypass(context) && !context->auth_notified) {
        context->auth_notified=1;
        i2a_tls_api.X509_STORE_CTX_set_error(store,0);
        return i2a_tls_api.SSL_ctrl(context->ssl,136,0,NULL)>0;
    }
    uint32_t result=0;
    int status=trust_evaluate(context->trust,&result);
    void *leaf=i2a_tls_api.X509_STORE_CTX_get0_cert(store);
    unsigned char address[16];
    int ip=inet_pton(AF_INET,context->hostname,address)==1 || inet_pton(AF_INET6,context->hostname,address)==1;
    int hostname=ip ? i2a_tls_api.X509_check_ip_asc(leaf,context->hostname,0) : i2a_tls_api.X509_check_host(leaf,context->hostname,0,0,NULL);
    int accepted=!status && (result==1 || result==4) && hostname==1;
    i2a_tls_api.X509_STORE_CTX_set_error(store,accepted ? 0 : 50);
    LOG("TLS peer verified: %s chain=%s hostname=%s policy=%s",context->hostname,(!status && (result==1 || result==4)) ? "accepted" : "rejected",hostname==1 ? "accepted" : "rejected",i2a_tls_custom_ca_bypass(context) ? "Android-system" : "guest-requested-policy");
    return accepted;
}
static int i2a_tls_status(struct I2ATLS *context,int result,const char *operation) {
    int error=i2a_tls_api.SSL_get_error(context->ssl,result);
    if (error==2 || error==3) return -9803;
    if (error==12) return -9841;  
    if (error==6) { context->state=3; return -9805; }
    context->state=4;
    char message[192]; unsigned long code=i2a_tls_api.ERR_get_error(); i2a_tls_api.ERR_error_string_n(code,message,sizeof(message));
    LOG("TLS %s failed: peer=%s SSLerror=%d IOstatus=%d detail=%s",operation,context->hostname ? context->hostname : "unset",error,context->io_status,message);
    return context->io_status && context->io_status!=-9803 ? context->io_status : -9806;
}
static int i2a_ssl_handshake(struct I2ATLS *context) {
    if (!i2a_tls_valid(context) || !context->read || !context->write || !context->hostname) return -50;
    if (context->state==2) return 0;
    if (context->state>=3) return -9806;
    context->state=1; context->io_status=0; i2a_tls_api.ERR_clear_error();
    int result=i2a_tls_api.SSL_do_handshake(context->ssl);
    if (result!=1) return i2a_tls_status(context,result,"handshake");
    context->state=2;
    LOG("TLS handshake completed: %s protocol=%s",context->hostname,i2a_tls_api.SSL_get_version(context->ssl)); return 0;
}
static int i2a_ssl_state(struct I2ATLS *context,int *state) {
    if (!i2a_tls_valid(context) || !state) return -50;
    *state=context->state; return 0;
}
static int i2a_ssl_trust(struct I2ATLS *context,struct Trust **trust) {
    if (!i2a_tls_valid(context) || !trust) return -50;
    *trust=context->trust; if (!*trust) return -9808;
    trust_retain(*trust); return 0;
}
static int i2a_ssl_read(struct I2ATLS *context,void *buffer,size_t size,size_t *processed) {
    if (!i2a_tls_valid(context) || !processed || (!buffer && size)) return -50;
    *processed=0; if (!size) return 0;
    if (context->state!=2) return context->state==3 ? -9805 : -9806;
    context->io_status=0; i2a_tls_api.ERR_clear_error();
    int result=i2a_tls_api.SSL_read_ex(context->ssl,buffer,size,processed);
    if (*processed && network_diagnostics_requested && i2a_tls_custom_ca_bypass(context) && context->plaintext_read<1024*1024) {
        if (!context->response_capture) { char path[PATH_MAX]; snprintf(path,sizeof(path),"%s/tls-response-%d-%p.bin",tls_diagnostics_directory,getpid(),context); context->response_capture=fopen(path,"wb"); }
        if (context->response_capture) { fwrite(buffer,1,*processed,context->response_capture); fflush(context->response_capture); }
    }
    if (*processed && !context->plaintext_read) {
        LOG("TLS application response received: %s bytes=%zu",context->hostname,*processed);
        if (*processed>=12 && !memcmp(buffer,"HTTP/",5)) {
            char line[128]; size_t length=*processed<sizeof(line)-1 ? *processed : sizeof(line)-1;
            memcpy(line,buffer,length); line[length]=0; int code=0;
            if (sscanf(line,"HTTP/%*u.%*u %d",&code)==1) LOG("TLS HTTP response status: %s status=%d",context->hostname,code);
        }
    }
    context->plaintext_read+=*processed;
    return result==1 ? (*processed<size ? -9803 : 0) : i2a_tls_status(context,result,"read");
}
static int i2a_ssl_write(struct I2ATLS *context,const void *buffer,size_t size,size_t *processed) {
    if (!i2a_tls_valid(context) || !processed || (!buffer && size)) return -50;
    *processed=0; if (!size) return 0;
    if (context->state!=2) return -9806;
    while (*processed<size) {
        size_t count=0; context->io_status=0; i2a_tls_api.ERR_clear_error();
        int result=i2a_tls_api.SSL_write_ex(context->ssl,(const char *)buffer+*processed,size-*processed,&count);
        if (count && network_diagnostics_requested && i2a_tls_custom_ca_bypass(context) && context->plaintext_written<1024*1024) {
            if (!context->request_capture) { char path[PATH_MAX]; snprintf(path,sizeof(path),"%s/tls-request-%d-%p.bin",tls_diagnostics_directory,getpid(),context); context->request_capture=fopen(path,"wb"); }
            if (context->request_capture) { fwrite((const char *)buffer+*processed,1,count,context->request_capture); fflush(context->request_capture); }
        }
        *processed+=count;
        if (count && !context->plaintext_written) LOG("TLS application request sent: %s bytes=%zu",context->hostname,count);
        context->plaintext_written+=count;
        if (result!=1) return i2a_tls_status(context,result,"write");
        if (!count) return -9803;
    }
    return 0;
}
static int i2a_ssl_close(struct I2ATLS *context) {
    if (!i2a_tls_valid(context)) return -50;
    if (context->state==3) return 0;
    if (context->state!=2) { context->state=3; return 0; }
    context->io_status=0; i2a_tls_api.ERR_clear_error();
    int result=i2a_tls_api.SSL_shutdown(context->ssl);
    if (result>=0) { context->state=3; return 0; }
    return i2a_tls_status(context,result,"close");
}
static Word resolve_secure_transport_import(const char *name) {
    if (!strcmp(name,"_SSLCreateContext")) return (Word)i2a_ssl_create;
    if (!strcmp(name,"_SSLSetIOFuncs")) return (Word)i2a_ssl_io;
    if (!strcmp(name,"_SSLSetConnection")) return (Word)i2a_ssl_connection;
    if (!strcmp(name,"_SSLSetPeerDomainName")) return (Word)i2a_ssl_hostname;
    if (!strcmp(name,"_SSLSetProtocolVersionMin")) return (Word)i2a_ssl_minimum;
    if (!strcmp(name,"_SSLSetSessionOption")) return (Word)i2a_ssl_option;
    if (!strcmp(name,"_SSLHandshake")) return (Word)i2a_ssl_handshake;
    if (!strcmp(name,"_SSLGetSessionState")) return (Word)i2a_ssl_state;
    if (!strcmp(name,"_SSLCopyPeerTrust")) return (Word)i2a_ssl_trust;
    if (!strcmp(name,"_SSLRead")) return (Word)i2a_ssl_read;
    if (!strcmp(name,"_SSLWrite")) return (Word)i2a_ssl_write;
    if (!strcmp(name,"_SSLClose")) return (Word)i2a_ssl_close;
    return 0;
}
#undef TLS_API
