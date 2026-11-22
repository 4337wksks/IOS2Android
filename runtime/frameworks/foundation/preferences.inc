 
struct PreferencesDomain { char *name; Word values; int present; struct PreferencesDomain *next; };
static struct PreferencesDomain *preference_domains;
static pthread_mutex_t preferences_mutex=PTHREAD_MUTEX_INITIALIZER;
static Word locale_languages(Word,const char *);

static Word defaults_system_value(Word key,const char *selector) {
     
    if (key && !strcmp(string_bytes(key,selector),"AppleLanguages")) {
        Word languages=locale_languages(0,selector);
#if I2A_DEVELOPMENT
        static unsigned reported;
        if (!__atomic_exchange_n(&reported,1,__ATOMIC_RELAXED))
            LOG("[DIAG-locale] Supplied system AppleLanguages from Android: count=%lu",read_word(languages+8));
#endif
        return languages;
    }
    return 0;
}

static plist_t object_property_list(Word object,Word *ancestors,unsigned depth) {
    if (!object || depth>=64) return NULL;
    for (unsigned i=0;i<depth;++i) if (ancestors[i]==object) return NULL;
    ancestors[depth]=object;
    Word cls=read_word(object);
    if (cls==external_class("_OBJC_CLASS_$_NSString") || cls==external_class("_OBJC_CLASS_$_NSMutableString")) return plist_new_string(string_bytes(object,"NSUserDefaults"));
    if (cls==external_class("_OBJC_CLASS_$_NSNumber")) {
        struct HostObject *number=(void *)object;
        if (object==(Word)&bool_numbers[0] || object==(Word)&bool_numbers[1]) return plist_new_bool(numeric_bool(number));
        return number->content[1] ? plist_new_real(numeric_double(number)) : plist_new_int((int64_t)number->content[0]);
    }
    if (cls==external_class("_OBJC_CLASS_$_NSData") || cls==external_class("_OBJC_CLASS_$_NSMutableData"))
        return plist_new_data((void *)data_buffer((void *)object),read_word(object+8));
    if (cls==external_class("_OBJC_CLASS_$_NSDate")) return property_list_new_date(date_seconds(object));
    int array=cls==external_class("_OBJC_CLASS_$_NSArray") || cls==external_class("_OBJC_CLASS_$_NSMutableArray");
    int dictionary=cls==external_class("_OBJC_CLASS_$_NSDictionary") || cls==external_class("_OBJC_CLASS_$_NSMutableDictionary");
    if (!array && !dictionary) return NULL;
    plist_t result=array ? plist_new_array() : plist_new_dict();
    size_t count=read_word(object+8);
    for (size_t i=0;i<count;++i) {
        Word value=array ? ((struct Sequence *)object)->items[i] : ((struct GuestMap *)object)->entries[i].value;
        plist_t node=object_property_list(value,ancestors,depth+1);
        if (!node) { plist_free(result); return NULL; }
        if (array) plist_array_append_item(result,node);
        else {
            Word key=((struct GuestMap *)object)->entries[i].key;
            if (read_word(key)!=external_class("_OBJC_CLASS_$_NSString") && read_word(key)!=external_class("_OBJC_CLASS_$_NSMutableString")) {
                plist_free(node); plist_free(result); return NULL;
            }
            plist_dict_set_item(result,string_bytes(key,"NSUserDefaults"),node);
        }
    }
    return result;
}
static Word preferences_path(const char *domain) {
    Word directory=append_path(directory_path(5),"Preferences",copy_string("Preferences"));
    const char *path=string_bytes(directory,"Preferences");
    if (mkdir(path,0700) && errno!=EEXIST) stop("Cannot create preferences directory",path);
    Word name=percent_encode(copy_string(domain),NULL,NULL);
    return append_path(directory,"Preferences",append_path_extension(name,"Preferences",copy_string("plist")));
}
static Word legacy_preferences(const char *domain) {
    JNIEnv *env=attached_environment("NSUserDefaults migration");
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID load=(*env)->GetMethodID(env,cls,"legacyDefaults","(Ljava/lang/String;)Ljava/lang/String;");
    jstring name=(*env)->NewStringUTF(env,domain);
    jstring encoded=(*env)->CallObjectMethod(env,activity,load,name);
    (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,cls);
    if (!encoded) return empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    Word text=string_from_java(env,encoded,"NSUserDefaults migration");
    (*env)->DeleteLocalRef(env,encoded);
    const char *json=string_bytes(text,"NSUserDefaults migration");
    plist_t node=NULL;
    Word result=0;
    if (plist_from_json(json,(uint32_t)strlen(json),&node)==PLIST_ERR_SUCCESS && plist_get_node_type(node)==PLIST_DICT) result=plist_object(node,0);
    if (node) plist_free(node);
    if (!result) result=empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    ((struct GuestMap *)result)->isa=external_class("_OBJC_CLASS_$_NSMutableDictionary");
    return result;
}
static struct PreferencesDomain *preferences_domain(const char *name) {
    for (struct PreferencesDomain *entry=preference_domains;entry;entry=entry->next) if (!strcmp(entry->name,name)) return entry;
    struct PreferencesDomain *entry=calloc(1,sizeof(*entry));
    if (!entry) stop("Allocation failed","NSUserDefaults domain");
    entry->name=strdup(name);
    Word path=preferences_path(name);
    const char *filename=string_bytes(path,"NSUserDefaults");
    entry->present=access(filename,F_OK)==0;
    entry->values=dictionary_from_file(external_class("_OBJC_CLASS_$_NSMutableDictionary"),"NSUserDefaults",path);
    if (!entry->values && entry->present) stop("Invalid preference property list",name);
    if (!entry->values) entry->values=legacy_preferences(name);
    if (map_count((void *)entry->values)) entry->present=1;
    entry->next=preference_domains; preference_domains=entry;
    return entry;
}
static const char *preferences_name(struct HostObject *object) {
    return object->content[0] ? string_bytes(object->content[0],"NSUserDefaults domain") : BUNDLE_ID;
}
static void save_preferences(struct PreferencesDomain *domain) {
    Word ancestors[64];
    plist_t root=object_property_list(domain->values,ancestors,0);
    if (!root) stop("Unsupported preference value",domain->name);
    char *bytes=NULL; uint32_t length=0;
    int success=plist_to_bin(root,&bytes,&length)==PLIST_ERR_SUCCESS;
    plist_free(root);
    if (!success) stop("Preference serialization failed",domain->name);
    Word data=data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"NSUserDefaults",bytes,length);
    plist_mem_free(bytes);
    Word path=preferences_path(domain->name);
    if (!data_write_file((void *)data,"NSUserDefaults",path,1,NULL)) stop("Preference write failed",domain->name);
    free((void *)data);
    domain->present=1;
}
static Word defaults_get(struct HostObject *object,const char *selector,Word key) {
    pthread_mutex_lock(&preferences_mutex);
    struct PreferencesDomain *domain=preferences_domain(preferences_name(object));
    Word value=map_get((void *)domain->values,selector,key);
    Word registered=object->content[1] ? map_get((void *)object->content[1],selector,key) : 0;
    pthread_mutex_unlock(&preferences_mutex);
    if (!value) value=defaults_system_value(key,selector);
    return value ? value : registered;
}
static void defaults_set(struct HostObject *object,const char *selector,Word value,Word key) {
    Word copy=0;
    if (value) {
        Word ancestors[64];
        plist_t node=object_property_list(value,ancestors,0);
        if (!node) stop("Invalid preference value",selector);
        copy=plist_object(node,0); plist_free(node);
    }
    pthread_mutex_lock(&preferences_mutex);
    struct PreferencesDomain *domain=preferences_domain(preferences_name(object));
    if (copy) map_set((void *)domain->values,selector,copy,key);
    else dictionary_remove((void *)domain->values,selector,key);
    save_preferences(domain);
    pthread_mutex_unlock(&preferences_mutex);
}
static void defaults_remove(struct HostObject *object,const char *selector,Word key) { defaults_set(object,selector,0,key); }
static void defaults_set_integer(struct HostObject *object,const char *selector,Word value,Word key) { defaults_set(object,selector,number_integer(value),key); }
static void defaults_set_bool(struct HostObject *object,const char *selector,Word value,Word key) { defaults_set(object,selector,number_bool(external_class("_OBJC_CLASS_$_NSNumber"),selector,value),key); }
static void defaults_set_double(struct HostObject *object,const char *selector,double value,Word key) { defaults_set(object,selector,real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),selector,value),key); }
static void defaults_set_float(struct HostObject *object,const char *selector,float value,Word key) { defaults_set_double(object,selector,value,key); }
static double defaults_get_double(struct HostObject *object,const char *selector,Word key) {
    Word value=defaults_get(object,selector,key);
    if (!value) return 0;
    Word cls=read_word(value);
    if (cls==external_class("_OBJC_CLASS_$_NSNumber")) return numeric_double((void *)value);
    if (cls==external_class("_OBJC_CLASS_$_NSString")) return strtod(string_bytes(value,selector),NULL);
    return 0;
}
static float defaults_get_float(struct HostObject *object,const char *selector,Word key) { return (float)defaults_get_double(object,selector,key); }
static Word defaults_get_integer(struct HostObject *object,const char *selector,Word key) {
    Word value=defaults_get(object,selector,key);
    if (!value) return 0;
    if (read_word(value)==external_class("_OBJC_CLASS_$_NSNumber")) return numeric_integer((void *)value);
    if (read_word(value)==external_class("_OBJC_CLASS_$_NSString")) return (Word)strtoll(string_bytes(value,selector),NULL,10);
    return 0;
}
static Word defaults_get_bool(struct HostObject *object,const char *selector,Word key) {
    Word value=defaults_get(object,selector,key);
    if (!value) return 0;
    if (read_word(value)==external_class("_OBJC_CLASS_$_NSNumber")) return numeric_bool((void *)value);
    if (read_word(value)==external_class("_OBJC_CLASS_$_NSString")) {
        const char *text=string_bytes(value,selector);
        return *text=='Y' || *text=='y' || *text=='T' || *text=='t' || strtol(text,NULL,10)!=0;
    }
    return 0;
}
static void defaults_register(struct HostObject *object,const char *selector,struct GuestMap *values) {
    pthread_mutex_lock(&preferences_mutex);
    if (!object->content[1]) object->content[1]=empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    dictionary_merge((void *)object->content[1],selector,values);
    pthread_mutex_unlock(&preferences_mutex);
}
static Word defaults_persistent(Word object,const char *selector,Word name) {
    pthread_mutex_lock(&preferences_mutex);
    struct PreferencesDomain *domain=preferences_domain(string_bytes(name,selector));
    Word result=domain->present ? map_copy((void *)domain->values,"copy") : 0;
    pthread_mutex_unlock(&preferences_mutex);
    return result;
}
static void defaults_set_domain(Word object,const char *selector,struct GuestMap *values,Word name) {
    pthread_mutex_lock(&preferences_mutex);
    struct PreferencesDomain *domain=preferences_domain(string_bytes(name,selector));
    domain->values=values ? map_copy(values,"mutableCopy") : empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));
    save_preferences(domain);
    pthread_mutex_unlock(&preferences_mutex);
}
static Word defaults_dictionary(struct HostObject *object,const char *selector) {
    Word language_key=copy_string("AppleLanguages"),languages=defaults_system_value(language_key,selector);
    pthread_mutex_lock(&preferences_mutex);
    struct PreferencesDomain *domain=preferences_domain(preferences_name(object));
    struct GuestMap *result=(void *)(object->content[1] ? map_copy((void *)object->content[1],"copy") : empty_map(external_class("_OBJC_CLASS_$_NSDictionary")));
    if (languages) map_set(result,selector,languages,language_key);
    dictionary_merge(result,selector,(void *)domain->values);
    pthread_mutex_unlock(&preferences_mutex);
    return (Word)result;
}
static Word defaults_suite(struct HostObject *object,const char *selector,Word suite) { object->content[0]=suite; return (Word)object; }
static Method preferences_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || class_receiver || strcmp(receiver,"_OBJC_CLASS_$_NSUserDefaults")) return NULL;
    if (!strcmp(selector,"initWithSuiteName:")) return (Method)defaults_suite;
    if (!strcmp(selector,"objectForKey:") || !strcmp(selector,"stringForKey:") || !strcmp(selector,"arrayForKey:") ||
        !strcmp(selector,"dictionaryForKey:") || !strcmp(selector,"dataForKey:")) return (Method)defaults_get;
    if (!strcmp(selector,"boolForKey:")) return (Method)defaults_get_bool;
    if (!strcmp(selector,"integerForKey:")) return (Method)defaults_get_integer;
    if (!strcmp(selector,"floatForKey:")) return (Method)defaults_get_float;
    if (!strcmp(selector,"doubleForKey:")) return (Method)defaults_get_double;
    if (!strcmp(selector,"setObject:forKey:") || !strcmp(selector,"setValue:forKey:")) return (Method)defaults_set;
    if (!strcmp(selector,"valueForKey:")) return (Method)defaults_get;
    if (!strcmp(selector,"removeObjectForKey:")) return (Method)defaults_remove;
    if (!strcmp(selector,"setBool:forKey:")) return (Method)defaults_set_bool;
    if (!strcmp(selector,"setInteger:forKey:")) return (Method)defaults_set_integer;
    if (!strcmp(selector,"setFloat:forKey:")) return (Method)defaults_set_float;
    if (!strcmp(selector,"setDouble:forKey:")) return (Method)defaults_set_double;
    if (!strcmp(selector,"registerDefaults:")) return (Method)defaults_register;
    if (!strcmp(selector,"persistentDomainForName:")) return (Method)defaults_persistent;
    if (!strcmp(selector,"setPersistentDomain:forName:")) return (Method)defaults_set_domain;
    if (!strcmp(selector,"dictionaryRepresentation")) return (Method)defaults_dictionary;
    if (!strcmp(selector,"synchronize")) return (Method)synchronized_defaults;
    return NULL;
}
