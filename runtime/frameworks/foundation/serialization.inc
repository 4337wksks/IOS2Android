 
static Word null_object(Word cls) { null_value.isa=cls; return (Word)&null_value; }
static Word serialization_error(Word *error,const char *description) {
    if (error) {
        struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
        map_set(info,"JSON error",copy_string(description),copy_string("NSLocalizedDescription"));
        *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"JSON error",copy_string("NSCocoaErrorDomain"),3840,(Word)info);
    }
    return 0;
}
static void json_mutability(Word object,Word options) {
    Word cls=read_word(object);
    if (cls==external_class("_OBJC_CLASS_$_NSArray")) {
        struct Sequence *array=(void *)object;
        if (options&1) array->isa=external_class("_OBJC_CLASS_$_NSMutableArray");
        for (size_t i=0;i<array->count;++i) json_mutability(array->items[i],options);
    } else if (cls==external_class("_OBJC_CLASS_$_NSDictionary")) {
        struct GuestMap *map=(void *)object;
        if (options&1) map->isa=external_class("_OBJC_CLASS_$_NSMutableDictionary");
        for (size_t i=0;i<map->count;++i) json_mutability(map->entries[i].value,options);
    } else if (cls==external_class("_OBJC_CLASS_$_NSString") && (options&2) && !in_image(object))
        ((struct HostObject *)object)->isa=external_class("_OBJC_CLASS_$_NSMutableString");
}
static Word json_parse(Word cls,const char *selector,struct HostObject *data,Word options,Word *error) {
    if (!data || data->content[0]>UINT32_MAX || (options&~(Word)7)) return serialization_error(error,"Invalid JSON input or options");
    plist_t root=NULL;
    if (plist_from_json((void *)data_buffer(data),(uint32_t)data->content[0],&root)!=PLIST_ERR_SUCCESS)
        return serialization_error(error,"Invalid JSON document");
    plist_type type=plist_get_node_type(root);
    if (!(options&4) && type!=PLIST_DICT && type!=PLIST_ARRAY) { plist_free(root); return serialization_error(error,"JSON root must be an object or array"); }
    Word result=plist_object(root,0);
    plist_free(root);
    if (options&3) json_mutability(result,options);
    return result;
}
static void json_indent(FILE *stream,unsigned depth) { fputc('\n',stream); for (unsigned i=0;i<depth*2;++i) fputc(' ',stream); }
static void json_string(FILE *stream,Word string) {
    struct TextView text=text_view(string,"JSON string");
    fputc('"',stream);
    for (size_t i=0;i<text.length;++i) {
        unsigned ch=text.units[i];
        if (ch=='"' || ch=='\\') { fputc('\\',stream); fputc((int)ch,stream); }
        else if (ch>=32 && ch<127) fputc((int)ch,stream);
        else fprintf(stream,"\\u%04x",ch);
    }
    fputc('"',stream);
    free(text.owned);
}
static int json_emit(FILE *stream,Word object,Word options,Word *path,unsigned depth) {
    if (!object || depth>=128) return 0;
    for (unsigned i=0;i<depth;++i) if (path[i]==object) return 0;
    path[depth]=object;
    Word cls=read_word(object);
    if (object==(Word)&null_value) { fputs("null",stream); return 1; }
    if (cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) { json_string(stream,object); return 1; }
    if (cls==decimal_number_class) {
        struct DecimalValue value=decimal_load((void *)object);
        if (value.nan) return 0;
        char text[192]; decimal_format(value,text); fputs(text,stream); return 1;
    }
    if (cls==external_class("_OBJC_CLASS_$_NSNumber")) {
        struct HostObject *number=(void *)object;
        if (object==(Word)&bool_numbers[0] || object==(Word)&bool_numbers[1]) fputs(numeric_bool(number) ? "true" : "false",stream);
        else if (number->content[1]) {
            double value=numeric_double(number);
            if (!isfinite(value)) return 0;
            fprintf(stream,"%.17g",value);
        } else fprintf(stream,"%lld",(long long)number->content[0]);
        return 1;
    }
    int array=cls==external_class("_OBJC_CLASS_$_NSArray") || cls==external_class("_OBJC_CLASS_$_NSMutableArray");
    int map=cls==external_class("_OBJC_CLASS_$_NSDictionary") || cls==external_class("_OBJC_CLASS_$_NSMutableDictionary");
    if (!array && !map) return 0;
    size_t count=read_word(object+8);
    fputc(array ? '[' : '{',stream);
    for (size_t i=0;i<count;++i) {
        if (i) fputc(',',stream);
        if (options&1) json_indent(stream,depth+1);
        Word child;
        if (array) child=((struct Sequence *)object)->items[i];
        else {
            struct MapEntry entry=((struct GuestMap *)object)->entries[i];
            Word key_class=read_word(entry.key);
            if (key_class!=external_class("_OBJC_CLASS_$_NSString") && key_class!=external_class("_OBJC_CLASS_$_NSMutableString")) return 0;
            json_string(stream,entry.key); fputc(':',stream); if (options&1) fputc(' ',stream);
            child=entry.value;
        }
        if (!json_emit(stream,child,options,path,depth+1)) return 0;
    }
    if (count && (options&1)) json_indent(stream,depth);
    fputc(array ? ']' : '}',stream);
    return 1;
}
static int json_validate(Word object,Word *path,unsigned depth) {
    if (!object || depth>=128) return 0;
    for (unsigned i=0;i<depth;++i) if (path[i]==object) return 0;
    path[depth]=object; Word cls=read_word(object);
    if (object==(Word)&null_value || cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) return 1;
    if (cls==decimal_number_class) return !decimal_load((void *)object).nan;
    if (cls==external_class("_OBJC_CLASS_$_NSNumber")) return !((struct HostObject *)object)->content[1] || isfinite(numeric_double((void *)object));
    int array=cls==external_class("_OBJC_CLASS_$_NSArray") || cls==external_class("_OBJC_CLASS_$_NSMutableArray");
    int map=cls==external_class("_OBJC_CLASS_$_NSDictionary") || cls==external_class("_OBJC_CLASS_$_NSMutableDictionary");
    if (!array && !map) return 0;
    size_t count=read_word(object+8);
    for (size_t i=0;i<count;++i) {
        Word child;
        if (array) child=((struct Sequence *)object)->items[i];
        else {
            struct MapEntry entry=((struct GuestMap *)object)->entries[i]; Word key=read_word(entry.key);
            if (key!=external_class("_OBJC_CLASS_$_NSString") && key!=external_class("_OBJC_CLASS_$_NSMutableString")) return 0;
            child=entry.value;
        }
        if (!json_validate(child,path,depth+1)) return 0;
    }
    return 1;
}
static unsigned char json_is_valid(Word cls,const char *selector,Word object) {
    if (!object) return 0;
    Word type=read_word(object);
    if (type!=external_class("_OBJC_CLASS_$_NSArray") && type!=external_class("_OBJC_CLASS_$_NSMutableArray") &&
        type!=external_class("_OBJC_CLASS_$_NSDictionary") && type!=external_class("_OBJC_CLASS_$_NSMutableDictionary")) return 0;
    Word ancestors[128]; return (unsigned char)json_validate(object,ancestors,0);
}
static Word json_encode(Word cls,const char *selector,Word object,Word options,Word *error) {
    if (options&~(Word)5) return serialization_error(error,"Unsupported JSON writing options");
    if (!object) return serialization_error(error,"Nil JSON root");
    Word type=read_word(object);
    if (!(options&4) && type!=external_class("_OBJC_CLASS_$_NSArray") && type!=external_class("_OBJC_CLASS_$_NSMutableArray") &&
        type!=external_class("_OBJC_CLASS_$_NSDictionary") && type!=external_class("_OBJC_CLASS_$_NSMutableDictionary"))
        return serialization_error(error,"JSON root must be an object or array");
    char *bytes=NULL; size_t size=0;
    FILE *stream=open_memstream(&bytes,&size);
    if (!stream) stop("Allocation failed",selector);
    Word ancestors[128];
    int valid=json_emit(stream,object,options,ancestors,0);
    if (fclose(stream)) valid=0;
    Word result=valid ? data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),selector,bytes,size) : serialization_error(error,"Invalid or cyclic JSON object");
    free(bytes);
    return result;
}
static Method serialization_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSNull")) {
        if (class_receiver && !strcmp(selector,"null")) return (Method)null_object;
        if (!class_receiver && !strcmp(selector,"copy")) return (Method)identity;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSJSONSerialization") && class_receiver) {
        if (!strcmp(selector,"isValidJSONObject:")) return (Method)json_is_valid;
        if (!strcmp(selector,"JSONObjectWithData:options:error:")) return (Method)json_parse;
        if (!strcmp(selector,"dataWithJSONObject:options:error:")) return (Method)json_encode;
    }
    return NULL;
}
