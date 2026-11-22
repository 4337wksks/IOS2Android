static Word uuid_init(Word object) {
    struct CoreUuid *value=create_uuid(0);
    memcpy(((struct HostObject *)object)->content,value->bytes,16);
    cf_release((Word)value);
    return object;
}
static Word uuid_factory(Word cls) { return uuid_init(host_allocate(cls)); }
static struct HostObject advertising_manager,advertising_identifier;
static pthread_once_t advertising_once=PTHREAD_ONCE_INIT;
static void advertising_initialize(void) {
    advertising_manager.isa=external_class("_OBJC_CLASS_$_ASIdentifierManager");
    advertising_identifier.isa=external_class("_OBJC_CLASS_$_NSUUID");
}
static Word advertising_shared_manager(void) { pthread_once(&advertising_once,advertising_initialize); return (Word)&advertising_manager; }
static Word advertising_id(void) { pthread_once(&advertising_once,advertising_initialize); return (Word)&advertising_identifier; }
static Word advertising_tracking_enabled(void) { return 0; }
static Word device_vendor_identifier(void) {
    static Word identifier;
    if (identifier) return identifier;
    JNIEnv *env=attached_environment("identifierForVendor");
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"vendorIdentifier","()[B");
    jbyteArray bytes=(*env)->CallObjectMethod(env,activity,method);
    (*env)->DeleteLocalRef(env,type);
    if (!bytes || (*env)->GetArrayLength(env,bytes)!=16) stop("Android scoped identifier unavailable","identifierForVendor");
    struct HostObject *uuid=(void *)host_allocate(external_class("_OBJC_CLASS_$_NSUUID"));
    (*env)->GetByteArrayRegion(env,bytes,0,16,(jbyte *)uuid->content); (*env)->DeleteLocalRef(env,bytes);
    identifier=(Word)uuid;
    return identifier;
}
static Word uuid_text(struct HostObject *object) {
    struct CoreUuid value={.tag=(Word)&uuid_tag};
    memcpy(value.bytes,object->content,16);
    return uuid_string(0,&value);
}
static void uuid_bytes(struct HostObject *object,const char *selector,void *output) { memcpy(output,object->content,16); }
static Word uuid_init_bytes(struct HostObject *object,const char *selector,const void *bytes) { memcpy(object->content,bytes,16); return (Word)object; }
static Word process_unique(Word self,const char *selector) {
    struct CoreUuid *value=create_uuid(0);
    Word result=uuid_string(0,value);
    cf_release((Word)value);
    return result;
}
static Word processor_count(void) { return (Word)sysconf(_SC_NPROCESSORS_ONLN); }
static Word physical_memory(void) { return (Word)sysconf(_SC_PHYS_PAGES)*(Word)sysconf(_SC_PAGESIZE); }
static Word environment_dictionary(void) {
    extern char **environ;
    struct GuestMap *result=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    for (char **entry=environ;entry && *entry;++entry) {
        const char *separator=strchr(*entry,'=');
        if (!separator) continue;
        Word key=new_string((size_t)(separator-*entry));
        memcpy((void *)read_word(key+16),*entry,(size_t)(separator-*entry));
        map_set(result,"environment",copy_string(separator+1),key);
    }
    return (Word)result;
}
struct BoxedValue { Word isa; const char *type; size_t size; unsigned char bytes[32]; };
static Word value_pointer(Word cls,const char *selector,void *pointer) {
    struct BoxedValue *value=calloc(1,sizeof(*value));
    if (!value) stop("Allocation failed",selector);
    value->isa=cls; value->type="^v"; value->size=sizeof(pointer); memcpy(value->bytes,&pointer,sizeof(pointer));
    return (Word)value;
}
static Word value_range(Word cls,const char *selector,struct GuestRange range) {
    struct BoxedValue *value=calloc(1,sizeof(*value));
    if (!value) stop("Allocation failed",selector);
    value->isa=cls; value->type="{_NSRange=QQ}"; value->size=sizeof(range); memcpy(value->bytes,&range,sizeof(range));
    return (Word)value;
}
static void value_get(struct BoxedValue *value,const char *selector,void *output) { memcpy(output,value->bytes,value->size); }
static Word value_pointer_get(struct BoxedValue *value) { Word result; memcpy(&result,value->bytes,sizeof(result)); return result; }
static struct GuestRange value_range_get(struct BoxedValue *value) { struct GuestRange result; memcpy(&result,value->bytes,sizeof(result)); return result; }
static const char *value_type(struct BoxedValue *value) { return value->type; }
static Word date_distant(Word cls,const char *selector) { return date_epoch(cls,selector,!strcmp(selector,"distantPast") ? -62135769600.0 : 64092211200.0); }
static double date_since_now(Word self) {
    struct timespec now; clock_gettime(CLOCK_REALTIME,&now);
    return date_seconds(self)-now.tv_sec-now.tv_nsec*1e-9;
}
static Method metadata_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_ASIdentifierManager")) {
        if (class_receiver && !strcmp(selector,"sharedManager")) return (Method)advertising_shared_manager;
        if (!class_receiver && !strcmp(selector,"advertisingIdentifier")) return (Method)advertising_id;
        if (!class_receiver && !strcmp(selector,"isAdvertisingTrackingEnabled")) return (Method)advertising_tracking_enabled;
    }
    if (!class_receiver && !strcmp(receiver,"_OBJC_CLASS_$_UIDevice") && !strcmp(selector,"identifierForVendor")) return (Method)device_vendor_identifier;
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSUUID")) {
        if (class_receiver && !strcmp(selector,"UUID")) return (Method)uuid_factory;
        if (!class_receiver) {
            if (!strcmp(selector,"init")) return (Method)uuid_init;
            if (!strcmp(selector,"UUIDString")) return (Method)uuid_text;
            if (!strcmp(selector,"getUUIDBytes:")) return (Method)uuid_bytes;
            if (!strcmp(selector,"initWithUUIDBytes:")) return (Method)uuid_init_bytes;
            if (!strcmp(selector,"copy")) return (Method)identity;
        }
    }
    if (object==(Word)&process_info) {
        if (!strcmp(selector,"processorCount") || !strcmp(selector,"activeProcessorCount")) return (Method)processor_count;
        if (!strcmp(selector,"physicalMemory")) return (Method)physical_memory;
        if (!strcmp(selector,"globallyUniqueString")) return (Method)process_unique;
        if (!strcmp(selector,"environment")) return (Method)environment_dictionary;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSValue")) {
        if (class_receiver && !strcmp(selector,"valueWithPointer:")) return (Method)value_pointer;
        if (class_receiver && !strcmp(selector,"valueWithRange:")) return (Method)value_range;
        if (!class_receiver) {
            if (!strcmp(selector,"getValue:")) return (Method)value_get;
            if (!strcmp(selector,"pointerValue")) return (Method)value_pointer_get;
            if (!strcmp(selector,"rangeValue")) return (Method)value_range_get;
            if (!strcmp(selector,"objCType")) return (Method)value_type;
            if (!strcmp(selector,"copy")) return (Method)identity;
        }
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_NSDate")) {
        if (class_receiver && (!strcmp(selector,"distantPast") || !strcmp(selector,"distantFuture"))) return (Method)date_distant;
        if (!class_receiver && !strcmp(selector,"timeIntervalSinceNow")) return (Method)date_since_now;
    }
    return NULL;
}
