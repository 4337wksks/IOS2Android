 

struct Request { Word isa, url, method, headers, body; double timeout; Word policy,body_stream,protocol_properties; unsigned char cookies; };
struct Session { Word isa, configuration, delegate, queue; int invalidated; Word description; };
static Word session_ensure_queue(struct Session *);
static Word default_session_configuration(Word cls) { return host_allocate(cls); }
struct SessionConfig { Word policy,headers; double request_timeout,resource_timeout; int discretionary,cellular; Word maximum_connections,cookie_storage,url_cache,credential_storage,cookie_policy; int cookies,pipelining; Word background_identifier; };
static struct SessionConfig *configuration_values(struct HostObject *object) {
    if (!object->content[0]) {
        struct SessionConfig *values=calloc(1,sizeof(*values));
        if (!values) stop("Allocation failed","NSURLSessionConfiguration");
        values->request_timeout=60; values->resource_timeout=604800; values->cellular=1;
        values->maximum_connections=6; values->cookies=1;
        object->content[0]=(Word)values;
    }
    return (void *)object->content[0];
}
static Word background_session_configuration(Word cls,const char *selector,Word identifier) {
    if (!identifier || !string_length(identifier,selector)) stop("Invalid background session identifier",selector);
    Word result=default_session_configuration(cls);
    configuration_values((void *)result)->background_identifier=text_copy(identifier,"copy");
    return result;
}
static Word configuration_identifier(struct HostObject *object) { return configuration_values(object)->background_identifier; }
static void configuration_policy(struct HostObject *object,const char *selector,Word value) { configuration_values(object)->policy=value; }
static Word configuration_get_policy(struct HostObject *object) { return configuration_values(object)->policy; }
static void configuration_discretionary(struct HostObject *object,const char *selector,Word value) { configuration_values(object)->discretionary=value!=0; }
static Word configuration_get_discretionary(struct HostObject *object) { return configuration_values(object)->discretionary; }
static void configuration_headers(struct HostObject *object,const char *selector,Word value) { configuration_values(object)->headers=value ? map_copy((void *)value,"copy") : 0; }
static Word configuration_get_headers(struct HostObject *object) { return configuration_values(object)->headers; }
static void configuration_timeout(struct HostObject *object,const char *selector,double value) {
    if (!isfinite(value) || value<0) stop("Invalid session timeout",selector);
    if (!strcmp(selector,"setTimeoutIntervalForRequest:")) configuration_values(object)->request_timeout=value;
    else configuration_values(object)->resource_timeout=value;
}
static double configuration_get_timeout(struct HostObject *object,const char *selector) {
    struct SessionConfig *values=configuration_values(object);
    return !strcmp(selector,"timeoutIntervalForRequest") ? values->request_timeout : values->resource_timeout;
}
static void configuration_integer(struct HostObject *object,const char *selector,Word value) {
    struct SessionConfig *values=configuration_values(object);
    if (!strcmp(selector,"setHTTPMaximumConnectionsPerHost:")) {
         
        values->maximum_connections=value;
    }
    else if (!strcmp(selector,"setAllowsCellularAccess:")) values->cellular=value!=0;
    else if (!strcmp(selector,"setHTTPShouldSetCookies:")) values->cookies=value!=0;
    else if (!strcmp(selector,"setHTTPShouldUsePipelining:")) values->pipelining=value!=0;
    else if (!strcmp(selector,"setHTTPCookieAcceptPolicy:")) values->cookie_policy=value;
    else if (!strcmp(selector,"setHTTPCookieStorage:")) values->cookie_storage=value;
    else if (!strcmp(selector,"setURLCache:")) values->url_cache=value;
    else values->credential_storage=value;
}
static Word configuration_get_integer(struct HostObject *object,const char *selector) {
    struct SessionConfig *values=configuration_values(object);
    if (!strcmp(selector,"HTTPMaximumConnectionsPerHost")) return values->maximum_connections;
    if (!strcmp(selector,"allowsCellularAccess")) return values->cellular;
    if (!strcmp(selector,"HTTPShouldSetCookies")) return values->cookies;
    if (!strcmp(selector,"HTTPShouldUsePipelining")) return values->pipelining;
    if (!strcmp(selector,"HTTPCookieAcceptPolicy")) return values->cookie_policy;
    if (!strcmp(selector,"HTTPCookieStorage")) return values->cookie_storage;
    if (!strcmp(selector,"URLCache")) return values->url_cache;
    return values->credential_storage;
}
static Word configuration_copy(struct HostObject *object) {
    struct HostObject *result=(void *)host_allocate(object->isa);
    *configuration_values(result)=*configuration_values(object);
    return (Word)result;
}
static Word session_create(Word cls,const char *selector,Word configuration,Word delegate,Word queue) {
    struct Session *session=calloc(1,sizeof(*session));
    if (!session) stop("Allocation failed",selector);
    session->isa=cls;
    session->configuration=configuration_copy((void *)(configuration ? configuration : default_session_configuration(external_class("_OBJC_CLASS_$_NSURLSessionConfiguration"))));
    session->delegate=delegate;
    session->queue=queue;
    return (Word)session;
}
static Word session_delegate(struct Session *session) { return session->delegate; }
static Word session_queue(struct Session *session) { return session_ensure_queue(session); }
static Word session_configuration(struct Session *session) { return session->configuration; }

static Word network_url(Word cls,const char *selector,Word string) {
    if (!string) return 0;
    string_bytes(string,selector);
    return file_url(string);
}

static Word file_url_factory(Word cls,const char *selector,Word string) { return file_url(string); }
static Word url_absolute_string(struct HostObject *url) { return url->content[0]; }

static Word request_new(Word cls,const char *selector,Word url,Word policy,double timeout) {
    if (!url) return 0;
    struct Request *request=calloc(1,sizeof(*request));
    if (!request) stop("Allocation failed",selector);
    request->isa=cls;
    request->url=url;
    request->policy=policy;
    request->timeout=timeout;
    request->cookies=1;
    request->method=copy_string("GET");
    request->headers=empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    return (Word)request;
}

static Word request_basic(Word cls,const char *selector,Word url) { return request_new(cls,selector,url,0,60); }
static Word request_init_empty(Word self,const char *selector) {
    struct Request *request=calloc(1,sizeof(*request));
    if (!request) stop("Allocation failed",selector);
    request->isa=read_word(self); request->timeout=60; request->method=copy_string("GET");
    request->cookies=1;
    request->headers=empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    free((void *)self); return (Word)request;
}
static Word request_url(struct Request *request) { return request->url; }
static Word request_headers(struct Request *request) { return request->headers; }
static Word request_method(struct Request *request) { return request->method; }
static Word request_body(struct Request *request) { return request->body; }
static Word request_body_stream(struct Request *request) { return request->body_stream; }
static void request_set_body_stream(struct Request *request,const char *selector,Word stream) { request->body_stream=stream; request->body=0; }
static Word request_policy(struct Request *request) { return request->policy; }
static void request_set_policy(struct Request *request,const char *selector,Word policy) { request->policy=policy; }
static double request_timeout(struct Request *request) { return request->timeout; }
static unsigned char request_cookies(struct Request *request) { return request->cookies; }
static void request_set_cookies(struct Request *request,const char *selector,unsigned char value) { request->cookies=value!=0; }
static void request_set_method(struct Request *request,const char *selector,Word value) { request->method=value; }
static void request_set_body(struct Request *request,const char *selector,Word value) { request->body=value; request->body_stream=0; }
static void request_set_url(struct Request *request,const char *selector,Word value) { request->url=value; }
static void request_set_timeout(struct Request *request,const char *selector,double value) { request->timeout=value; }
static void request_set_headers(struct Request *request,const char *selector,Word value) { request->headers=value ? map_copy((void *)value,"mutableCopy") : empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary")); }

static void request_header(struct Request *request,const char *selector,Word value,Word name) {
    struct GuestMap *headers=(void *)request->headers;
    const char *wanted=string_bytes(name,selector);
    for (size_t i=0;i<headers->count;++i) if (!strcasecmp(string_bytes(headers->entries[i].key,selector),wanted)) {
        name=headers->entries[i].key;
        if (value && !strcmp(selector,"addValue:forHTTPHeaderField:")) value=append_text(append_text(headers->entries[i].value,selector,copy_string(",")),selector,value);
        break;
    }
    dictionary_subscript(headers,selector,value,name);
}
static Word request_header_value(struct Request *request,const char *selector,Word name) {
    struct GuestMap *headers=(void *)request->headers;
    for (size_t i=0;i<headers->count;++i) if (!strcasecmp(string_bytes(headers->entries[i].key,selector),string_bytes(name,selector))) return headers->entries[i].value;
    return 0;
}
static Word request_init_url(Word self,const char *selector,Word url,Word policy,double timeout) {
    Word result=request_new(read_word(self),selector,url,policy,timeout);
    free((void *)self);
    return result;
}
static Word request_init_basic(Word self,const char *selector,Word url) { return request_init_url(self,selector,url,0,60); }

static Word request_copy(struct Request *request,const char *selector) {
    struct Request *copy=malloc(sizeof(*copy));
    if (!copy) stop("Allocation failed",selector);
    *copy=*request;
    copy->isa=external_class(!strcmp(selector,"mutableCopy") || !strcmp(selector,"mutableCopyWithZone:") ? "_OBJC_CLASS_$_NSMutableURLRequest" : "_OBJC_CLASS_$_NSURLRequest");
    copy->headers=map_copy((void *)request->headers,"mutableCopy");
    if (request->protocol_properties) copy->protocol_properties=map_copy((void *)request->protocol_properties,"mutableCopy");
    return (Word)copy;
}

static Word protocol_request_property(Word cls,const char *selector,Word key,struct Request *request) {
    return request && request->protocol_properties ? map_get((void *)request->protocol_properties,selector,key) : 0;
}
static void protocol_set_request_property(Word cls,const char *selector,Word value,Word key,struct Request *request) {
    if (!request || !key) stop("Invalid protocol request property",selector);
    if (!request->protocol_properties) request->protocol_properties=empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    dictionary_subscript((void *)request->protocol_properties,selector,value,key);
}
static void protocol_remove_request_property(Word cls,const char *selector,Word key,struct Request *request) {
    if (request && request->protocol_properties) dictionary_subscript((void *)request->protocol_properties,selector,0,key);
}

static Method network_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_NSURLProtocol")) {
        if (!strcmp(selector,"propertyForKey:inRequest:")) return (Method)protocol_request_property;
        if (!strcmp(selector,"setProperty:forKey:inRequest:")) return (Method)protocol_set_request_property;
        if (!strcmp(selector,"removePropertyForKey:inRequest:")) return (Method)protocol_remove_request_property;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLSessionConfiguration")) {
        if (class_receiver && (!strcmp(selector,"defaultSessionConfiguration") || !strcmp(selector,"ephemeralSessionConfiguration"))) return (Method)default_session_configuration;
        if (class_receiver && (!strcmp(selector,"backgroundSessionConfigurationWithIdentifier:") || !strcmp(selector,"backgroundSessionConfiguration:"))) return (Method)background_session_configuration;
        if (!class_receiver) {
            if (!strcmp(selector,"identifier")) return (Method)configuration_identifier;
            if (!strcmp(selector,"copy")) return (Method)configuration_copy;
            if (!strcmp(selector,"setRequestCachePolicy:")) return (Method)configuration_policy;
            if (!strcmp(selector,"requestCachePolicy")) return (Method)configuration_get_policy;
            if (!strcmp(selector,"setDiscretionary:")) return (Method)configuration_discretionary;
            if (!strcmp(selector,"isDiscretionary")) return (Method)configuration_get_discretionary;
            if (!strcmp(selector,"setHTTPAdditionalHeaders:")) return (Method)configuration_headers;
            if (!strcmp(selector,"HTTPAdditionalHeaders")) return (Method)configuration_get_headers;
            if (!strcmp(selector,"setTimeoutIntervalForRequest:") || !strcmp(selector,"setTimeoutIntervalForResource:")) return (Method)configuration_timeout;
            if (!strcmp(selector,"timeoutIntervalForRequest") || !strcmp(selector,"timeoutIntervalForResource")) return (Method)configuration_get_timeout;
            if (!strcmp(selector,"setHTTPMaximumConnectionsPerHost:") || !strcmp(selector,"setAllowsCellularAccess:") || !strcmp(selector,"setHTTPShouldSetCookies:") ||
                !strcmp(selector,"setHTTPShouldUsePipelining:") || !strcmp(selector,"setHTTPCookieAcceptPolicy:") || !strcmp(selector,"setHTTPCookieStorage:") ||
                !strcmp(selector,"setURLCache:") || !strcmp(selector,"setURLCredentialStorage:")) return (Method)configuration_integer;
            if (!strcmp(selector,"HTTPMaximumConnectionsPerHost") || !strcmp(selector,"allowsCellularAccess") || !strcmp(selector,"HTTPShouldSetCookies") ||
                !strcmp(selector,"HTTPShouldUsePipelining") || !strcmp(selector,"HTTPCookieAcceptPolicy") || !strcmp(selector,"HTTPCookieStorage") ||
                !strcmp(selector,"URLCache") || !strcmp(selector,"URLCredentialStorage")) return (Method)configuration_get_integer;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLSession")) {
        if (class_receiver && !strcmp(selector,"sessionWithConfiguration:delegate:delegateQueue:")) return (Method)session_create;
        if (!class_receiver && !strcmp(selector,"delegate")) return (Method)session_delegate;
        if (!class_receiver && !strcmp(selector,"delegateQueue")) return (Method)session_queue;
        if (!class_receiver && !strcmp(selector,"configuration")) return (Method)session_configuration;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURL")) {
        if (class_receiver && !strcmp(selector,"URLWithString:")) return (Method)network_url;
        if (class_receiver && !strcmp(selector,"fileURLWithPath:")) return (Method)file_url_factory;
        if (!class_receiver && (!strcmp(selector,"absoluteString") || !strcmp(selector,"description"))) return (Method)url_absolute_string;
        if (!class_receiver && !strcmp(selector,"copy")) return (Method)identity;
    }
    int mutable=!strcmp(receiver,"_OBJC_CLASS_$_NSMutableURLRequest");
    if (mutable || !strcmp(receiver,"_OBJC_CLASS_$_NSURLRequest")) {
        if (class_receiver) {
            if (!strcmp(selector,"requestWithURL:cachePolicy:timeoutInterval:")) return (Method)request_new;
            if (!strcmp(selector,"requestWithURL:")) return (Method)request_basic;
        } else {
            if (!strcmp(selector,"init")) return (Method)request_init_empty;
            if (!strcmp(selector,"initWithURL:cachePolicy:timeoutInterval:")) return (Method)request_init_url;
            if (!strcmp(selector,"initWithURL:")) return (Method)request_init_basic;
            if (!strcmp(selector,"copy") || !strcmp(selector,"mutableCopy") || !strcmp(selector,"copyWithZone:") || !strcmp(selector,"mutableCopyWithZone:")) return (Method)request_copy;
            if (!strcmp(selector,"URL")) return (Method)request_url;
            if (!strcmp(selector,"allHTTPHeaderFields")) return (Method)request_headers;
            if (!strcmp(selector,"HTTPMethod")) return (Method)request_method;
            if (!strcmp(selector,"HTTPBody")) return (Method)request_body;
            if (!strcmp(selector,"HTTPBodyStream")) return (Method)request_body_stream;
            if (!strcmp(selector,"cachePolicy")) return (Method)request_policy;
            if (!strcmp(selector,"valueForHTTPHeaderField:")) return (Method)request_header_value;
            if (!strcmp(selector,"timeoutInterval")) return (Method)request_timeout;
            if (!strcmp(selector,"HTTPShouldHandleCookies")) return (Method)request_cookies;
            if (mutable) {
                if (!strcmp(selector,"setHTTPShouldHandleCookies:")) return (Method)request_set_cookies;
                if (!strcmp(selector,"setValue:forHTTPHeaderField:") || !strcmp(selector,"addValue:forHTTPHeaderField:")) return (Method)request_header;
                if (!strcmp(selector,"setCachePolicy:")) return (Method)request_set_policy;
                if (!strcmp(selector,"setHTTPBodyStream:")) return (Method)request_set_body_stream;
                if (!strcmp(selector,"setHTTPMethod:")) return (Method)request_set_method;
                if (!strcmp(selector,"setHTTPBody:")) return (Method)request_set_body;
                if (!strcmp(selector,"setURL:")) return (Method)request_set_url;
                if (!strcmp(selector,"setTimeoutInterval:")) return (Method)request_set_timeout;
                if (!strcmp(selector,"setAllHTTPHeaderFields:")) return (Method)request_set_headers;
            }
        }
    }
    return NULL;
}
