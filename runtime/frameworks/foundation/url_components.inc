 
static Word url_query_item_init(struct HostObject *item,const char *selector,Word name,Word value) {
    if (!name) stop("Nil URL query item name",selector);
    item->content[0]=text_copy(name,"copy");
    item->content[1]=value ? text_copy(value,"copy") : 0;
    return (Word)item;
}
static Word url_query_item_create(Word cls,const char *selector,Word name,Word value) {
    return url_query_item_init((void *)host_allocate(cls),selector,name,value);
}
static Word url_query_item_property(struct HostObject *item,const char *selector) {
    return item->content[!strcmp(selector,"value")];
}
static Word url_query_item_equal(struct HostObject *item,const char *selector,Word other) {
    if (!other || read_word(other)!=item->isa) return 0;
    struct HostObject *value=(void *)other;
    return same_objects(item->content[0],value->content[0]) && same_objects(item->content[1],value->content[1]);
}
static Word url_query_item_hash(struct HostObject *item) {
    Word hash=1469598103934665603UL;
    for (unsigned field=0;field<2;++field) {
        struct TextView text=item->content[field] ? text_view(item->content[field],"hash") : (struct TextView){0};
        for (size_t i=0;i<text.length;++i) hash=(hash^text.units[i])*1099511628211UL;
        hash=(hash^(item->content[field] ? 1 : 0))*1099511628211UL;
        free(text.owned);
    }
    return hash;
}
static Word url_component_slice(const char *text,size_t length) {
    return text_decode(external_class("_OBJC_CLASS_$_NSString"),text,length,4);
}
static Word url_components_init_string(struct HostObject *object,const char *selector,Word string) {
    if (!string) return 0;
    struct HostObject url={.content={string,0}};
    struct UrlParts parts=url_parts(&url);
    size_t suffix=parts.fragment==SIZE_MAX ? parts.size : parts.fragment;
    size_t prefix=parts.query==SIZE_MAX ? suffix : parts.query;
    object->content[0]=parts.query==SIZE_MAX ? 0 : url_component_slice(parts.text+parts.query+1,suffix-parts.query-1);
    object->content[1]=url_component_slice(parts.text,prefix);
    object->content[2]=parts.fragment==SIZE_MAX ? 0 : url_component_slice(parts.text+suffix,parts.size-suffix);
    if (object->content[0]) {
        const char *query=string_bytes(object->content[0],selector);
        if (!percent_decode(query,strlen(query))) return 0;
    }
    return (Word)object;
}
static Word url_components_with_string(Word cls,const char *selector,Word string) {
    return url_components_init_string((void *)host_allocate(cls),selector,string);
}
static Word url_components_init_url(struct HostObject *object,const char *selector,Word url,unsigned char resolve) {
    if (!url) return 0;
    return url_components_init_string(object,selector,url_absolute((void *)url,selector));
}
static Word url_components_with_url(Word cls,const char *selector,Word url,unsigned char resolve) {
    return url_components_init_url((void *)host_allocate(cls),selector,url,resolve);
}
static Word url_components_copy(struct HostObject *object) {
    struct HostObject *copy=(void *)host_allocate(object->isa);
    memcpy(copy->content,object->content,sizeof(copy->content)); return (Word)copy;
}
static Word url_components_query(struct HostObject *object,const char *selector) {
    Word encoded=object->content[0];
    if (!encoded || !strcmp(selector,"percentEncodedQuery")) return encoded;
    const char *text=string_bytes(encoded,selector); return percent_decode(text,strlen(text));
}
static void url_components_set_query(struct HostObject *object,const char *selector,Word query) {
    if (!query) { object->content[0]=0; return; }
    if (!strcmp(selector,"setPercentEncodedQuery:")) {
        const char *text=string_bytes(query,selector);
        if (!percent_decode(text,strlen(text)) || strchr(text,'#')) stop("Invalid percent encoded query",selector);
        object->content[0]=text_copy(query,"copy");
    } else object->content[0]=percent_encode(query,"!$&'()*+,;=:@/?",NULL);
}
static void url_components_set_items(struct HostObject *object,const char *selector,struct Sequence *items) {
    if (!items) { object->content[0]=0; return; }
    Word query=copy_string("");
    for (size_t i=0;i<items->count;++i) {
        struct HostObject *item=(void *)items->items[i];
        if (!item || item->isa!=external_class("_OBJC_CLASS_$_NSURLQueryItem")) stop("Invalid URL query item",selector);
        if (i) query=append_text(query,selector,copy_string("&"));
        Word name=percent_encode(item->content[0],"!$'()*+,;:@/?",NULL);
        query=append_text(query,selector,name);
        if (item->content[1]) {
            query=append_text(query,selector,copy_string("="));
            query=append_text(query,selector,percent_encode(item->content[1],"!$'()*+,;:@/?",NULL));
        }
    }
    object->content[0]=query;
}
static Word url_components_items(struct HostObject *object,const char *selector) {
    if (!object->content[0]) return 0;
    const char *query=string_bytes(object->content[0],selector);
    Word array=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    if (!*query) return array;
    size_t size=strlen(query),start=0;
    for (size_t end=0;end<=size;++end) if (end==size || query[end]=='&') {
        const char *equal=memchr(query+start,'=',end-start);
        size_t name_end=equal ? (size_t)(equal-query) : end;
        Word name=percent_decode(query+start,name_end-start);
        Word value=equal ? percent_decode(equal+1,end-name_end-1) : 0;
        if (!name || (equal && !value)) stop("Invalid URL query escape",selector);
        sequence_add((void *)array,selector,url_query_item_create(external_class("_OBJC_CLASS_$_NSURLQueryItem"),selector,name,value));
        start=end+1;
    }
    return array;
}
static Word url_components_string(struct HostObject *object,const char *selector) {
    Word text=object->content[1] ? object->content[1] : copy_string("");
    if (object->content[0]) {
        text=append_text(text,selector,copy_string("?"));
        text=append_text(text,selector,object->content[0]);
    }
    if (object->content[2]) text=append_text(text,selector,object->content[2]);
    return text;
}
static Word url_components_url(struct HostObject *object,const char *selector) {
    return url_from_string(external_class("_OBJC_CLASS_$_NSURL"),selector,url_components_string(object,selector));
}
static Word url_components_part(struct HostObject *object,const char *selector) {
    struct HostObject url={.content={object->content[1] ? object->content[1] : copy_string(""),0}};
    if (!strcmp(selector,"path") || !strcmp(selector,"percentEncodedPath")) {
        struct UrlParts p=url_parts(&url); return !strcmp(selector,"path") ? percent_decode(p.text+p.path_start,p.path_end-p.path_start) : url_component_slice(p.text+p.path_start,p.path_end-p.path_start);
    }
    if (!strcmp(selector,"fragment") || !strcmp(selector,"percentEncodedFragment")) {
        if (!object->content[2]) return 0; const char *p=string_bytes(object->content[2],selector)+1;
        return !strcmp(selector,"fragment") ? percent_decode(p,strlen(p)) : copy_string(p);
    }
    if (!strcmp(selector,"port")) return url_port(&url,selector);
    return url_component(&url,selector);
}
static void url_components_set_path(struct HostObject *object,const char *selector,Word value) {
    struct HostObject url={.content={object->content[1] ? object->content[1] : copy_string(""),0}}; struct UrlParts p=url_parts(&url);
    Word path=value ? !strcmp(selector,"setPath:") ? percent_encode(value,"/!$&'()*+,;=:@",NULL) : text_copy(value,"copy") : copy_string("");
    const char *bytes=string_bytes(path,selector);
    if (!percent_decode(bytes,strlen(bytes)) || strchr(bytes,'?') || strchr(bytes,'#')) stop("Invalid URL component path",selector);
    object->content[1]=append_text(url_component_slice(p.text,p.path_start),selector,path);
}
static void url_components_set_scheme(struct HostObject *object,const char *selector,Word value) {
    const char *scheme=value ? string_bytes(value,selector) : ""; size_t length=strlen(scheme);
    for (size_t i=0;i<length;++i) { unsigned c=(unsigned char)scheme[i]; if (!((c>='A' && c<='Z') || (c>='a' && c<='z') || (i && ((c>='0' && c<='9') || strchr("+.-",c))))) stop("Invalid URL scheme",selector); }
    struct HostObject url={.content={object->content[1] ? object->content[1] : copy_string(""),0}}; struct UrlParts p=url_parts(&url);
    const char *rest=p.text+(p.scheme_end==SIZE_MAX ? 0 : p.scheme_end+1);
    object->content[1]=length ? append_text(append_text(text_copy(value,"copy"),selector,copy_string(":")),selector,copy_string(rest)) : copy_string(rest);
}
static void url_components_set_fragment(struct HostObject *object,const char *selector,Word value) {
    if (!value) { object->content[2]=0; return; }
    Word fragment=!strcmp(selector,"setFragment:") ? percent_encode(value,"/?!$&'()*+,;=:@",NULL) : text_copy(value,"copy");
    const char *bytes=string_bytes(fragment,selector); if (!percent_decode(bytes,strlen(bytes)) || strchr(bytes,'#')) stop("Invalid URL fragment",selector);
    object->content[2]=append_text(copy_string("#"),selector,fragment);
}
static Method url_components_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLQueryItem")) {
        if (class_receiver && !strcmp(selector,"queryItemWithName:value:")) return (Method)url_query_item_create;
        if (!class_receiver) {
            if (!strcmp(selector,"initWithName:value:")) return (Method)url_query_item_init;
            if (!strcmp(selector,"name") || !strcmp(selector,"value")) return (Method)url_query_item_property;
            if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:")) return (Method)identity;
            if (!strcmp(selector,"isEqual:")) return (Method)url_query_item_equal;
            if (!strcmp(selector,"hash")) return (Method)url_query_item_hash;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURLComponents")) {
        if (class_receiver) {
            if (!strcmp(selector,"componentsWithString:")) return (Method)url_components_with_string;
            if (!strcmp(selector,"componentsWithURL:resolvingAgainstBaseURL:")) return (Method)url_components_with_url;
        } else {
            if (!strcmp(selector,"scheme") || !strcmp(selector,"host") || !strcmp(selector,"port") || !strcmp(selector,"path") || !strcmp(selector,"percentEncodedPath") || !strcmp(selector,"fragment") || !strcmp(selector,"percentEncodedFragment")) return (Method)url_components_part;
            if (!strcmp(selector,"setScheme:")) return (Method)url_components_set_scheme;
            if (!strcmp(selector,"setPath:") || !strcmp(selector,"setPercentEncodedPath:")) return (Method)url_components_set_path;
            if (!strcmp(selector,"setFragment:") || !strcmp(selector,"setPercentEncodedFragment:")) return (Method)url_components_set_fragment;
            if (!strcmp(selector,"init")) return (Method)identity;
            if (!strcmp(selector,"initWithString:")) return (Method)url_components_init_string;
            if (!strcmp(selector,"initWithURL:resolvingAgainstBaseURL:")) return (Method)url_components_init_url;
            if (!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:")) return (Method)url_components_copy;
            if (!strcmp(selector,"query") || !strcmp(selector,"percentEncodedQuery")) return (Method)url_components_query;
            if (!strcmp(selector,"setQuery:") || !strcmp(selector,"setPercentEncodedQuery:")) return (Method)url_components_set_query;
            if (!strcmp(selector,"queryItems")) return (Method)url_components_items;
            if (!strcmp(selector,"setQueryItems:")) return (Method)url_components_set_items;
            if (!strcmp(selector,"string")) return (Method)url_components_string;
            if (!strcmp(selector,"URL")) return (Method)url_components_url;
        }
    }
    return NULL;
}
