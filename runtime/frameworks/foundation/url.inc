 
struct UrlParts { const char *text; size_t size,scheme_end,authority_start,authority_end,path_start,path_end,query,fragment; };
static struct UrlParts url_parts(struct HostObject *url) {
    const char *text=string_bytes(url->content[0],"NSURL");
    size_t size=strlen(text);
    struct UrlParts parts={text,size,SIZE_MAX,SIZE_MAX,SIZE_MAX,0,size,SIZE_MAX,SIZE_MAX};
    if (url->content[1]) return parts;
    for (size_t i=0;i<size;++i) {
        if (text[i]=='#') { parts.fragment=i; break; }
        if (text[i]=='?' && parts.query==SIZE_MAX) parts.query=i;
    }
    if (parts.fragment!=SIZE_MAX) parts.path_end=parts.fragment;
    if (parts.query!=SIZE_MAX) parts.path_end=parts.query;
    const char *colon=memchr(text,':',parts.path_end);
    if (colon) {
        size_t length=(size_t)(colon-text);
        int valid=length>0;
        for (size_t i=0;i<length;++i) {
            unsigned ch=(unsigned char)text[i];
            if (!((ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || (i && ((ch>='0' && ch<='9') || strchr("+.-",ch))))) valid=0;
        }
        if (valid) { parts.scheme_end=length; parts.path_start=length+1; }
    }
    if (parts.path_start+2<=parts.path_end && text[parts.path_start]=='/' && text[parts.path_start+1]=='/') {
        parts.authority_start=parts.path_start+2;
        size_t end=parts.authority_start;
        while (end<parts.path_end && text[end]!='/') ++end;
        parts.authority_end=end; parts.path_start=end;
    }
    return parts;
}
static int url_hex(unsigned char ch) { return ch>='0' && ch<='9' ? ch-'0' : ch>='a' && ch<='f' ? ch-'a'+10 : ch>='A' && ch<='F' ? ch-'A'+10 : -1; }
static Word percent_decode(const char *bytes,size_t length) {
    char *decoded=malloc(length+1);
    if (!decoded) stop("Allocation failed","URL decoding");
    size_t count=0;
    for (size_t i=0;i<length;++i) {
        if (bytes[i]=='%') {
            if (i+2>=length || url_hex(bytes[i+1])<0 || url_hex(bytes[i+2])<0) { free(decoded); return 0; }
            decoded[count++]=(char)((url_hex(bytes[i+1])<<4)|url_hex(bytes[i+2])); i+=2;
        } else decoded[count++]=bytes[i];
    }
    Word result=text_decode(external_class("_OBJC_CLASS_$_NSString"),decoded,count,4);
    free(decoded);
    return result;
}
static Word percent_encode(Word source,const char *allowed,const char *forced) {
    const unsigned char *bytes=(const void *)string_bytes(source,"URL encoding");
    size_t length=strlen((const char *)bytes);
    if (length>(SIZE_MAX-1)/3) stop("URL too large","URL encoding");
    char *encoded=malloc(length*3+1),*out=encoded;
    if (!encoded) stop("Allocation failed","URL encoding");
    static const char digits[]="0123456789ABCDEF";
    for (size_t i=0;i<length;++i) {
        unsigned ch=bytes[i];
        int safe=(ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') || strchr("-._~",ch) || (allowed && strchr(allowed,ch));
        if (safe && !(forced && strchr(forced,ch))) *out++=(char)ch;
        else { *out++='%'; *out++=digits[ch>>4]; *out++=digits[ch&15]; }
    }
    *out=0;
    Word result=copy_string(encoded);
    free(encoded);
    return result;
}
static Word url_from_string(Word cls,const char *selector,Word string) {
    if (!string) return 0;
    const char *text=string_bytes(string,selector);
    if (!strncasecmp(text,"file://",7)) {
        const char *path=text+7;
        if (!strncasecmp(path,"localhost/",10)) path+=9;
        if (*path!='/') return 0;
        Word decoded=percent_decode(path,strlen(path));
        return decoded ? file_url(decoded) : 0;
    }
    struct HostObject *url=(void *)host_allocate(cls);
    url->content[0]=string;
    return (Word)url;
}
static Word url_init_string(Word object,const char *selector,Word string) {
    Word result=url_from_string(read_word(object),selector,string); free((void *)object); return result;
}
static Word parsed_url_path(struct HostObject *url) {
    if (url->content[1]) return url->content[0];
    struct UrlParts parts=url_parts(url);
    return percent_decode(parts.text+parts.path_start,parts.path_end-parts.path_start);
}
static Word url_is_file(struct HostObject *url) { return url->content[1]!=0; }
static Word url_absolute(struct HostObject *url,const char *selector) {
    if (!url->content[1]) return url->content[0];
    Word escaped=percent_encode(url->content[0],"/!$&'()*+,;=:@",NULL);
    const char *path=string_bytes(escaped,selector);
    return append_text(copy_string(path[0]=='/' ? "file://" : "file:///"),selector,escaped);
}
static Word url_component(struct HostObject *url,const char *selector) {
    if (url->content[1]) {
        if (!strcmp(selector,"scheme")) return copy_string("file");
        return 0;
    }
    struct UrlParts p=url_parts(url);
    size_t start=0,end=0;
    if (!strcmp(selector,"scheme")) {
        if (p.scheme_end==SIZE_MAX) return 0;
        end=p.scheme_end;
    } else if (!strcmp(selector,"query")) {
        if (p.query==SIZE_MAX) return 0;
        start=p.query+1; end=p.fragment==SIZE_MAX ? p.size : p.fragment;
    } else if (!strcmp(selector,"fragment")) {
        if (p.fragment==SIZE_MAX) return 0;
        start=p.fragment+1; end=p.size;
    } else if (!strcmp(selector,"host")) {
        if (p.authority_start==SIZE_MAX) return 0;
        start=p.authority_start; end=p.authority_end;
        for (size_t i=start;i<end;++i) if (p.text[i]=='@') start=i+1;
        if (start<end && p.text[start]=='[') {
            size_t bracket=++start;
            while (bracket<end && p.text[bracket]!=']') ++bracket;
            end=bracket;
        } else for (size_t i=start;i<end;++i) if (p.text[i]==':') { end=i; break; }
    }
    Word result=new_string(end-start);
    memcpy((void *)read_word(result+16),p.text+start,end-start);
    return result;
}
static Word file_url_directory(Word cls,const char *selector,Word path,unsigned char directory) {
    const char *text=string_bytes(path,selector);
    size_t length=strlen(text);
    if (directory && (!length || text[length-1]!='/')) path=append_text(path,selector,copy_string("/"));
    return file_url(path);
}
static Word url_port(struct HostObject *url,const char *selector) {
    if (url->content[1]) return 0;
    struct UrlParts p=url_parts(url);
    if (p.authority_start==SIZE_MAX) return 0;
    size_t start=p.authority_start,end=p.authority_end;
    for (size_t i=start;i<end;++i) if (p.text[i]=='@') start=i+1;
    if (start<end && p.text[start]=='[') {
        while (start<end && p.text[start]!=']') ++start;
        if (start<end) ++start;
    } else while (start<end && p.text[start]!=':') ++start;
    if (start==end || p.text[start++]!=':' || start==end) return 0;
    unsigned port=0;
    for (;start<end;++start) {
        unsigned digit=(unsigned char)p.text[start]-'0';
        if (digit>9 || port>(65535-digit)/10) return 0;
        port=port*10+digit;
    }
    return number_integer(port);
}
static Word file_url_components(Word cls,const char *selector,struct Sequence *components) {
    Word path=copy_string("");
    for (size_t i=0;i<components->count;++i) path=i ? append_path(path,selector,components->items[i]) : components->items[i];
    return file_url(path);
}
static Word url_replace_path(struct HostObject *url,Word path) {
    if (url->content[1]) return file_url(path);
    struct UrlParts p=url_parts(url);
    const char *middle=string_bytes(path,"URL path");
    char *value=NULL;
    if (asprintf(&value,"%.*s%s%s",(int)p.path_start,p.text,middle,p.text+p.path_end)<0) stop("Allocation failed","URL path");
    Word result=url_from_string(url->isa,"URL path",copy_string(value));
    free(value);
    return result;
}
static Word url_append_path(struct HostObject *url,const char *selector,Word component) {
    Word path;
    if (url->content[1]) path=append_path(url->content[0],selector,component);
    else {
        struct UrlParts p=url_parts(url);
        Word base=new_string(p.path_end-p.path_start);
        memcpy((void *)read_word(base+16),p.text+p.path_start,p.path_end-p.path_start);
        path=append_path(base,selector,percent_encode(component,"/!$&'()*+,;=:@",NULL));
        if (p.authority_start!=SIZE_MAX && *string_bytes(path,selector)!='/') path=append_text(copy_string("/"),selector,path);
    }
    return url_replace_path(url,path);
}
static Word url_append_directory(struct HostObject *url,const char *selector,Word component,unsigned char directory) {
    Word result=url_append_path(url,selector,component);
    if (directory) {
        struct HostObject *value=(void *)result;
        Word path=parsed_url_path(value);
        const char *bytes=string_bytes(path,selector);
        size_t length=strlen(bytes);
        if (length && bytes[length-1]!='/') result=url_replace_path(value,append_text(path,selector,copy_string("/")));
    }
    return result;
}
static Word url_append_extension(struct HostObject *url,const char *selector,Word extension) {
    Word path=append_path_extension(parsed_url_path(url),selector,extension);
    if (!path) return 0;
    return url_replace_path(url,url->content[1] ? path : percent_encode(path,"/!$&'()*+,;=:@",NULL));
}
static Word url_delete_path(struct HostObject *url,const char *selector) {
    Word path=text_delete_path(parsed_url_path(url),!strcmp(selector,"URLByDeletingPathExtension") ? "stringByDeletingPathExtension" : "stringByDeletingLastPathComponent");
    return url_replace_path(url,url->content[1] ? path : percent_encode(path,"/!$&'()*+,;=:@",NULL));
}
static Word url_path_part(struct HostObject *url,const char *selector) {
    Word path=parsed_url_path(url);
    return !strcmp(selector,"pathExtension") ? path_extension(path,selector) : path_basename(path,selector);
}
static Word string_percent_encode(Word self,const char *selector,Word encoding) {
    if (encoding!=4) stop("Unsupported URL text encoding",selector);
    return percent_encode(self,";/?:@&=+$,!~*'()#%",NULL);
}
static Word string_percent_decode(Word self,const char *selector,Word encoding) {
    if (encoding!=4) stop("Unsupported URL text encoding",selector);
    const char *text=string_bytes(self,selector);
    return percent_decode(text,strlen(text));
}
static Method url_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSURL")) {
        if (class_receiver) {
            if (!strcmp(selector,"URLWithString:")) return (Method)url_from_string;
            if (!strcmp(selector,"fileURLWithPath:isDirectory:")) return (Method)file_url_directory;
            if (!strcmp(selector,"fileURLWithPathComponents:")) return (Method)file_url_components;
        } else {
            if (!strcmp(selector,"initWithString:")) return (Method)url_init_string;
            if (!strcmp(selector,"path")) return (Method)parsed_url_path;
            if (!strcmp(selector,"absoluteString") || !strcmp(selector,"description")) return (Method)url_absolute;
            if (!strcmp(selector,"absoluteURL")) return (Method)identity;
            if (!strcmp(selector,"isFileURL")) return (Method)url_is_file;
            if (!strcmp(selector,"port")) return (Method)url_port;
            if (!strcmp(selector,"scheme") || !strcmp(selector,"host") || !strcmp(selector,"query") || !strcmp(selector,"fragment")) return (Method)url_component;
            if (!strcmp(selector,"URLByDeletingPathExtension") || !strcmp(selector,"URLByDeletingLastPathComponent")) return (Method)url_delete_path;
            if (!strcmp(selector,"URLByAppendingPathComponent:")) return (Method)url_append_path;
            if (!strcmp(selector,"URLByAppendingPathComponent:isDirectory:")) return (Method)url_append_directory;
            if (!strcmp(selector,"URLByAppendingPathExtension:")) return (Method)url_append_extension;
            if (!strcmp(selector,"lastPathComponent") || !strcmp(selector,"pathExtension")) return (Method)url_path_part;
        }
    }
    if (!class_receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSString") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableString"))) {
        if (!strcmp(selector,"stringByAddingPercentEscapesUsingEncoding:")) return (Method)string_percent_encode;
        if (!strcmp(selector,"stringByReplacingPercentEscapesUsingEncoding:")) return (Method)string_percent_decode;
    }
    return NULL;
}
