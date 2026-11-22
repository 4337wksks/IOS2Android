 
struct KeyObserver { Word object,observer,key,context,options; int active; struct KeyObserver *next; };
struct KeyChange { Word object,key,old; pthread_t thread; struct KeyChange *next; };
static pthread_mutex_t key_observers_mutex=PTHREAD_MUTEX_INITIALIZER;
static struct KeyObserver *key_observers;
static struct KeyChange *key_changes;
static Word key_value(Word object,const char *selector,Word key) {
    if (!object) return 0;
    const char *name=string_bytes(key,selector),*receiver=class_name(read_word(object));
    if (receiver && (!strcmp(receiver,"_OBJC_CLASS_$_NSDictionary") || !strcmp(receiver,"_OBJC_CLASS_$_NSMutableDictionary"))) return map_get((void *)object,selector,key);
    Method getter=find_implementation(object,name);
    char capitalized[256];
    if (!getter && *name && strlen(name)<sizeof(capitalized)-3) {
        snprintf(capitalized,sizeof(capitalized),"is%c%s",name[0]>='a' && name[0]<='z' ? name[0]-32 : name[0],name+1);
        getter=find_implementation(object,capitalized); if (getter) name=capitalized;
    }
    if (!getter) stop("Unknown key-value getter",name);
    struct MethodRecord *record=(void *)instance_method(read_word(object),name);
    char type=record && record->types ? record->types[0] : '@';
    if (!strncmp(name,"is",2) || !strcmp(name,"count") || !strcmp(name,"length") || !strcmp(name,"state")) type='q';
    if (type=='d') return real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),selector,((double (*)(Word,const char *))getter)(object,name));
    if (type=='f') return real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),selector,((float (*)(Word,const char *))getter)(object,name));
    if (strchr("cCsSiIlLqQB",type)) return number_integer(getter(object,name,0,0,0,0));
    if (type!='@' && type!='#') stop("Unsupported key-value return type",name);
    return getter(object,name,0,0,0,0);
}
static Word key_path_value(Word object,const char *selector,Word path) {
    struct Sequence *parts=(void *)split_string(path,selector,copy_string("."));
    for (size_t i=0;i<parts->count && object;++i) object=key_value(object,selector,parts->items[i]);
    return object;
}
static void key_observer_deliver(struct KeyObserver *observer,Word old,Word value,int prior) {
    if (!__atomic_load_n(&observer->active,__ATOMIC_ACQUIRE)) return;
    struct GuestMap *change=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(change,"KVO",number_integer(1),copy_string("kind"));
    Word null=null_object(external_class("_OBJC_CLASS_$_NSNull"));
    if ((observer->options&1) && !prior) map_set(change,"KVO",value ? value : null,copy_string("new"));
    if (observer->options&2) map_set(change,"KVO",old ? old : null,copy_string("old"));
    if (prior) map_set(change,"KVO",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"KVO",1),copy_string("notificationIsPrior"));
    const char *selector="observeValueForKeyPath:ofObject:change:context:";
    Method callback=find_implementation(observer->observer,selector);
    if (!callback) stop("Missing key-value observer callback",selector);
    callback(observer->observer,selector,observer->key,observer->object,(Word)change,observer->context);
}
static void key_observers_emit(Word object,Word key,Word old,Word value,int prior) {
    pthread_mutex_lock(&key_observers_mutex);
    size_t count=0;
    for (struct KeyObserver *item=key_observers;item;item=item->next) if (item->active && item->object==object && same_objects(key,item->key) && (!prior || (item->options&8))) ++count;
    struct KeyObserver **items=count ? malloc(count*sizeof(*items)) : NULL;
    if (count && !items) { pthread_mutex_unlock(&key_observers_mutex); stop("Allocation failed","KVO delivery"); }
    size_t i=0;
    for (struct KeyObserver *item=key_observers;item;item=item->next) if (item->active && item->object==object && same_objects(key,item->key) && (!prior || (item->options&8))) items[i++]=item;
    pthread_mutex_unlock(&key_observers_mutex);
    for (i=0;i<count;++i) key_observer_deliver(items[i],old,value,prior);
    free(items);
}
static void key_observer_add(Word object,const char *selector,Word observer,Word key,Word options,Word context) {
    const char *name=string_bytes(key,selector),*base=external_base(read_word(object));
    int app=object==(Word)&application && !strcmp(name,"delegate");
    int operation=base && (!strcmp(base,"_OBJC_CLASS_$_NSOperation") || !strcmp(base,"_OBJC_CLASS_$_NSBlockOperation") || !strcmp(base,"_OBJC_CLASS_$_NSInvocationOperation"));
    if (!observer || strchr(name,'.') || options>15 || (!app && !operation)) stop("Automatic KVO for this property is not implemented",name);
    struct KeyObserver *item=calloc(1,sizeof(*item)); if (!item) stop("Allocation failed",selector);
    *item=(struct KeyObserver){object,observer,text_copy(key,"copy"),context,options,1,NULL};
    pthread_mutex_lock(&key_observers_mutex); item->next=key_observers; key_observers=item; pthread_mutex_unlock(&key_observers_mutex);
    if (options&4) key_observer_deliver(item,0,key_value(object,selector,key),0);
}
static void key_observer_remove_context(Word object,const char *selector,Word observer,Word key,Word context) {
    int match_context=strstr(selector,":context:")!=NULL;
    pthread_mutex_lock(&key_observers_mutex);
    for (struct KeyObserver *item=key_observers;item;item=item->next)
        if (item->object==object && item->observer==observer && same_objects(item->key,key) && (!match_context || item->context==context)) __atomic_store_n(&item->active,0,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&key_observers_mutex);
}
static void key_observer_remove(Word object,const char *selector,Word observer,Word key) { key_observer_remove_context(object,selector,observer,key,0); }
static void key_will_change(Word object,const char *selector,Word key) {
    Word old=key_value(object,selector,key);
    struct KeyChange *change=malloc(sizeof(*change)); if (!change) stop("Allocation failed",selector);
    *change=(struct KeyChange){object,key,old,pthread_self(),NULL};
    pthread_mutex_lock(&key_observers_mutex); change->next=key_changes; key_changes=change; pthread_mutex_unlock(&key_observers_mutex);
    key_observers_emit(object,key,old,0,1);
}
static void key_did_change(Word object,const char *selector,Word key) {
    Word value=key_value(object,selector,key),old=0;
    pthread_mutex_lock(&key_observers_mutex);
    struct KeyChange **slot=&key_changes;
    while (*slot && ((*slot)->object!=object || !pthread_equal((*slot)->thread,pthread_self()) || !same_objects((*slot)->key,key))) slot=&(*slot)->next;
    if (*slot) { struct KeyChange *change=*slot; *slot=change->next; old=change->old; free(change); }
    pthread_mutex_unlock(&key_observers_mutex);
    key_observers_emit(object,key,old,value,0);
    const char *base=external_base(read_word(object));
    if (base && (!strcmp(base,"_OBJC_CLASS_$_NSOperation") || !strcmp(base,"_OBJC_CLASS_$_NSBlockOperation") || !strcmp(base,"_OBJC_CLASS_$_NSInvocationOperation"))) operation_changed(object,selector,key);
}
static Method kvo_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (class_receiver) return NULL;
    if (!strcmp(selector,"addObserver:forKeyPath:options:context:")) return (Method)key_observer_add;
    if (!strcmp(selector,"removeObserver:forKeyPath:")) return (Method)key_observer_remove;
    if (!strcmp(selector,"removeObserver:forKeyPath:context:")) return (Method)key_observer_remove_context;
    if (!strcmp(selector,"willChangeValueForKey:")) return (Method)key_will_change;
    if (!strcmp(selector,"didChangeValueForKey:")) return (Method)key_did_change;
    if (!strcmp(selector,"valueForKey:")) return (Method)key_value;
    if (!strcmp(selector,"valueForKeyPath:")) return (Method)key_path_value;
    return NULL;
}
