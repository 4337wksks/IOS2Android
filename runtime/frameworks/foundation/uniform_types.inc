 
struct CoreUniformType { const char *uti,*mime,*extension,*aliases; };
static const struct CoreUniformType core_uniform_types[]={
    {"public.jpeg","image/jpeg","jpg","jpeg,jpe"},{"public.png","image/png","png",""},
    {"com.compuserve.gif","image/gif","gif",""},{"public.tiff","image/tiff","tif","tiff"},
    {"com.microsoft.bmp","image/bmp","bmp",""},{"com.microsoft.ico","image/x-icon","ico",""},
    {"public.svg-image","image/svg+xml","svg",""},{"com.adobe.pdf","application/pdf","pdf",""},
    {"public.plain-text","text/plain","txt","text"},{"public.html","text/html","html","htm"},
    {"public.xml","application/xml","xml",""},{"public.json","application/json","json",""},
    {"public.zip-archive","application/zip","zip",""},{"public.mpeg-4","video/mp4","mp4",""},
    {"com.apple.quicktime-movie","video/quicktime","mov","qt"},{"public.mp3","audio/mpeg","mp3",""},
    {"public.mpeg-4-audio","audio/mp4","m4a",""},{"com.microsoft.waveform-audio","audio/x-wav","wav","wave"},
    {"public.data","application/octet-stream","",""}
};
static int core_uniform_tag_class(Word tag) {
    if (!tag) return 0;
    const char *text=string_bytes(tag,"UTType tag class");
    return !strcmp(text,"public.filename-extension") ? 1 : !strcmp(text,"public.mime-type") ? 2 : 0;
}
static int core_uniform_extension(const struct CoreUniformType *type,const char *tag) {
    if (!strcasecmp(type->extension,tag)) return 1;
    size_t length=strlen(tag); const char *p=type->aliases;
    while (*p) {
        const char *end=strchr(p,','); size_t n=end ? (size_t)(end-p) : strlen(p);
        if (n==length && !strncasecmp(p,tag,n)) return 1;
        if (!end) break; p=end+1;
    }
    return 0;
}
static Word core_uniform_preferred(Word tag_class,Word tag,Word conforming) {
    int kind=core_uniform_tag_class(tag_class);
     
    if (!kind || !tag || conforming) return 0;
    const char *text=string_bytes(tag,"UTTypeCreatePreferredIdentifierForTag");
    if (!*text) return 0;
    for (unsigned i=0;i<COUNT(core_uniform_types);++i)
        if ((kind==1 && core_uniform_extension(core_uniform_types+i,text)) || (kind==2 && !strcasecmp(core_uniform_types[i].mime,text))) return copy_string(core_uniform_types[i].uti);
    const char *prefix=kind==1 ? GUEST_UTI_NAMESPACE "ext." : GUEST_UTI_NAMESPACE "mime.";
    size_t n=strlen(text),p=strlen(prefix); if (n>(SIZE_MAX-p-1)/2) return 0;
    char *encoded=malloc(p+2*n+1); if (!encoded) return 0;
    memcpy(encoded,prefix,p); static const char hex[]="0123456789abcdef";
    for (size_t i=0;i<n;++i) { unsigned char c=(unsigned char)text[i]; encoded[p+2*i]=hex[c>>4]; encoded[p+2*i+1]=hex[c&15]; }
    encoded[p+2*n]=0; Word result=copy_string(encoded); free(encoded); return result;
}
static Word core_uniform_copy_tag(Word uti,Word tag_class) {
    int kind=core_uniform_tag_class(tag_class); if (!kind || !uti) return 0;
    const char *text=string_bytes(uti,"UTTypeCopyPreferredTagWithClass");
    for (unsigned i=0;i<COUNT(core_uniform_types);++i) if (!strcasecmp(core_uniform_types[i].uti,text)) {
        const char *tag=kind==1 ? core_uniform_types[i].extension : core_uniform_types[i].mime;
        return *tag ? copy_string(tag) : 0;
    }
    const char *prefix=kind==1 ? GUEST_UTI_NAMESPACE "ext." : GUEST_UTI_NAMESPACE "mime."; size_t p=strlen(prefix);
    if (strncmp(text,prefix,p)) return 0;
    text+=p; size_t length=strlen(text); if (!length || (length&1)) return 0;
    char *decoded=malloc(length/2+1); if (!decoded) return 0;
    for (size_t i=0;i<length;i+=2) {
        int a=url_hex((unsigned char)text[i]),b=url_hex((unsigned char)text[i+1]);
        if (a<0 || b<0 || !(a|b)) { free(decoded); return 0; }
        decoded[i/2]=(char)((a<<4)|b);
    }
    decoded[length/2]=0; Word result=text_decode(external_class("_OBJC_CLASS_$_NSString"),decoded,length/2,4); free(decoded); return result;
}
static Word resolve_uniform_type_import(const char *name) {
    if (!strcmp(name,"_UTTypeCreatePreferredIdentifierForTag")) return (Word)core_uniform_preferred;
    if (!strcmp(name,"_UTTypeCopyPreferredTagWithClass")) return (Word)core_uniform_copy_tag;
    return 0;
}
