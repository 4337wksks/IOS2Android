struct LocationValue {
    Word isa;
    struct GuestPoint coordinate;
    double altitude,horizontal_accuracy,vertical_accuracy,course,speed,course_accuracy,speed_accuracy;
    Word timestamp,source;
};
struct LocationEvent { Word value; unsigned kind,generation; struct LocationEvent *next; };
struct LocationManagerState {
    Word object,delegate,cached_delegate,location,loop,work;
    Method callbacks[5];
    double accuracy,distance;
    unsigned generation,active,once,callback_epoch;
    struct LocationEvent *first,*last;
    pthread_mutex_t mutex;
    struct LocationManagerState *next;
};
struct GeocoderState { Word object,block; unsigned generation; pthread_mutex_t mutex; struct GeocoderState *next; };
struct LocationPlacemark { Word isa,fields,location,postal; };
static struct LocationManagerState *location_managers;
static struct GeocoderState *geocoders;
static pthread_mutex_t location_states_mutex=PTHREAD_MUTEX_INITIALIZER;
static jmethodID location_host_method(JNIEnv *env,const char *name,const char *signature){
    jclass cls=(*env)->GetObjectClass(env,activity);jmethodID method=(*env)->GetMethodID(env,cls,name,signature);(*env)->DeleteLocalRef(env,cls);return method;
}
static Word location_authorization(void) {
    JNIEnv *env=attached_environment("CLLocationManager authorization");static jmethodID query;if(!query)query=location_host_method(env,"locationAuthorization","()I");return (Word)(*env)->CallIntMethod(env,activity,query);
}
static Word location_enabled(void) {
    JNIEnv *env=attached_environment("CLLocationManager availability");static jmethodID query;if(!query)query=location_host_method(env,"locationEnabled","()Z");return (*env)->CallBooleanMethod(env,activity,query);
}
static Word location_initialize_full(Word object,const char *selector,struct GuestPoint coordinate,double altitude,double horizontal,double vertical,double course,double course_accuracy,double speed,double speed_accuracy,Word timestamp){
    struct LocationValue *value=(void *)object;
    *value=(struct LocationValue){read_word(object),coordinate,altitude,horizontal,vertical,course,speed,course_accuracy,speed_accuracy,timestamp ? timestamp : new_date(external_class("_OBJC_CLASS_$_NSDate")),0};return object;
}
static Word location_initialize(Word object,const char *selector){return location_initialize_full(object,selector,(struct GuestPoint){0},0,0,-1,-1,-1,-1,-1,0);}
static Word location_initialize_coordinates(Word object,const char *selector,double latitude,double longitude){return location_initialize_full(object,selector,(struct GuestPoint){latitude,longitude},0,0,-1,-1,-1,-1,-1,0);}
static Word location_initialize_accuracy(Word object,const char *selector,struct GuestPoint coordinate,double altitude,double horizontal,double vertical,Word timestamp){return location_initialize_full(object,selector,coordinate,altitude,horizontal,vertical,-1,-1,-1,-1,timestamp);}
static Word location_initialize_motion(Word object,const char *selector,struct GuestPoint coordinate,double altitude,double horizontal,double vertical,double course,double speed,Word timestamp){return location_initialize_full(object,selector,coordinate,altitude,horizontal,vertical,course,-1,speed,-1,timestamp);}
static struct GuestPoint location_coordinate(struct LocationValue *object){return object->coordinate;}
static double location_measurement(struct LocationValue *object,const char *selector){
    if(!strcmp(selector,"altitude"))return object->altitude;if(!strcmp(selector,"horizontalAccuracy"))return object->horizontal_accuracy;
    if(!strcmp(selector,"verticalAccuracy"))return object->vertical_accuracy;if(!strcmp(selector,"course"))return object->course;if(!strcmp(selector,"speed"))return object->speed;
    return !strcmp(selector,"courseAccuracy") ? object->course_accuracy : object->speed_accuracy;
}
static Word location_reference(struct LocationValue *object,const char *selector){return !strcmp(selector,"timestamp") ? object->timestamp : object->source;}
static double location_distance(struct LocationValue *object,const char *selector,struct LocationValue *other){
    if(!other)return 0;JNIEnv *env=attached_environment(selector);static jmethodID measure;if(!measure)measure=location_host_method(env,"locationDistance","(DDDD)D");
    return (*env)->CallDoubleMethod(env,activity,measure,object->coordinate.x,object->coordinate.y,other->coordinate.x,other->coordinate.y);
}
static Word location_from_packet(const double values[11]){
    Word object=host_allocate(external_class("_OBJC_CLASS_$_CLLocation"));struct HostObject *date=(void *)new_date(external_class("_OBJC_CLASS_$_NSDate"));memcpy(date->content,values,sizeof(double));
    location_initialize_full(object,"Android location",(struct GuestPoint){values[1],values[2]},values[3],values[4],values[5],values[6],values[8],values[7],values[9],(Word)date);
    struct HostObject *source=(void *)host_allocate(external_class("_OBJC_CLASS_$_CLLocationSourceInformation"));source->content[1]=values[10]!=0;((struct LocationValue *)object)->source=(Word)source;return object;
}
static Word location_source_property(struct HostObject *object,const char *selector){return object->content[!strcmp(selector,"isSimulatedBySoftware")];}
static Word location_source_init(struct HostObject *object,const char *selector,unsigned char software,unsigned char accessory){object->content[0]=accessory!=0;object->content[1]=software!=0;return (Word)object;}
static struct LocationManagerState *location_manager_state(Word object){
    pthread_mutex_lock(&location_states_mutex);struct LocationManagerState *state=location_managers;while(state && state->object!=object)state=state->next;
    if(!state){state=calloc(1,sizeof(*state));if(!state){pthread_mutex_unlock(&location_states_mutex);stop("Allocation failed","CLLocationManager");}state->object=object;state->accuracy=-1;state->distance=-1;state->loop=core_current_runloop();pthread_mutex_init(&state->mutex,NULL);state->next=location_managers;location_managers=state;}
    pthread_mutex_unlock(&location_states_mutex);return state;
}
static Word location_manager_initialize(Word object,const char *selector){location_manager_state(object);return object;}
static void location_set_delegate(Word manager,const char *selector,Word delegate){__atomic_store_n(&location_manager_state(manager)->delegate,delegate,__ATOMIC_RELEASE);}
static Word location_delegate(Word manager){return __atomic_load_n(&location_manager_state(manager)->delegate,__ATOMIC_ACQUIRE);}
static void location_manager_deliver(void *context){
    struct LocationManagerState *state=context;
    const char *selectors[]={"locationManager:didUpdateLocations:","locationManager:didUpdateToLocation:fromLocation:","locationManager:didFailWithError:","locationManagerDidChangeAuthorization:","locationManager:didChangeAuthorizationStatus:"};
    for(;;){
        pthread_mutex_lock(&state->mutex);struct LocationEvent *event=state->first;if(event){state->first=event->next;if(!state->first)state->last=NULL;}pthread_mutex_unlock(&state->mutex);if(!event)break;
        unsigned kind=event->kind,generation=event->generation;Word value=event->value;free(event);
        Word delegate=__atomic_load_n(&state->delegate,__ATOMIC_ACQUIRE);unsigned epoch=__atomic_load_n(&method_cache_epoch,__ATOMIC_ACQUIRE);
        if(delegate!=state->cached_delegate || epoch!=state->callback_epoch){for(unsigned i=0;i<COUNT(selectors);++i)state->callbacks[i]=find_implementation(delegate,selectors[i]);state->cached_delegate=delegate;state->callback_epoch=epoch;}
        if(kind==2){if(state->callbacks[3])state->callbacks[3](delegate,selectors[3],state->object,0,0,0);else if(state->callbacks[4])state->callbacks[4](delegate,selectors[4],state->object,value,0,0);continue;}
        if(!__atomic_load_n(&state->active,__ATOMIC_ACQUIRE) || __atomic_load_n(&state->generation,__ATOMIC_ACQUIRE)!=generation)continue;
        if(kind==1){__atomic_store_n(&state->active,0,__ATOMIC_RELEASE);if(state->callbacks[2])state->callbacks[2](delegate,selectors[2],state->object,value,0,0);continue;}
        Word previous=__atomic_exchange_n(&state->location,value,__ATOMIC_ACQ_REL);if(state->once)__atomic_store_n(&state->active,0,__ATOMIC_RELEASE);
        if(state->callbacks[0]){Word locations=new_array(external_class("_OBJC_CLASS_$_NSArray"),"Location callback",&value,1);state->callbacks[0](delegate,selectors[0],state->object,locations,0,0);}
        else if(state->callbacks[1])state->callbacks[1](delegate,selectors[1],state->object,value,previous,0);
    }
}
static void location_manager_cleanup(void *context){
    struct LocationManagerState *state=context;__atomic_store_n(&state->active,0,__ATOMIC_RELEASE);pthread_mutex_lock(&state->mutex);struct LocationEvent *event=state->first;state->first=state->last=NULL;pthread_mutex_unlock(&state->mutex);
    while(event){struct LocationEvent *next=event->next;free(event);event=next;}
}
static void location_enqueue(struct LocationManagerState *state,unsigned kind,unsigned generation,Word value){
    struct LocationEvent *event=calloc(1,sizeof(*event));if(!event)stop("Allocation failed","Location callback");*event=(struct LocationEvent){value,kind,generation,NULL};
    if(!state->work)state->work=loop_function_create(state->loop,location_manager_deliver,state,location_manager_cleanup);
    pthread_mutex_lock(&state->mutex);if(state->last)state->last->next=event;else state->first=event;state->last=event;pthread_mutex_unlock(&state->mutex);loop_function_signal(state->work);
}
static void location_stop(Word object,const char *selector){
    struct LocationManagerState *state=location_manager_state(object);__atomic_store_n(&state->active,0,__ATOMIC_RELEASE);__atomic_add_fetch(&state->generation,1,__ATOMIC_ACQ_REL);
    JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=location_host_method(env,"stopLocation","(J)V");(*env)->CallVoidMethod(env,activity,method,(jlong)object);
}
static void location_start(Word object,const char *selector){
    struct LocationManagerState *state=location_manager_state(object);unsigned once=!strcmp(selector,"requestLocation");if(!once && __atomic_load_n(&state->active,__ATOMIC_ACQUIRE) && !state->once)return;
    state->once=once;unsigned generation=__atomic_add_fetch(&state->generation,1,__ATOMIC_ACQ_REL);__atomic_store_n(&state->active,1,__ATOMIC_RELEASE);
    JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=location_host_method(env,"startLocation","(JIDDZ)V");(*env)->CallVoidMethod(env,activity,method,(jlong)object,(jint)generation,state->accuracy,state->distance,(jboolean)once);
}
static double location_manager_measurement(Word object,const char *selector){struct LocationManagerState *state=location_manager_state(object);return !strcmp(selector,"desiredAccuracy") ? state->accuracy : state->distance;}
static void location_manager_set_measurement(Word object,const char *selector,double value){
    if(!isfinite(value))stop("Invalid location request parameter",selector);struct LocationManagerState *state=location_manager_state(object);double *slot=!strcmp(selector,"setDesiredAccuracy:") ? &state->accuracy : &state->distance;if(*slot==value)return;*slot=value;
    if(__atomic_load_n(&state->active,__ATOMIC_ACQUIRE)){unsigned once=state->once;location_stop(object,selector);location_start(object,once ? "requestLocation" : "startUpdatingLocation");}
}
static Word location_current(Word object,const char *selector){
    struct LocationManagerState *state=location_manager_state(object);Word known=__atomic_load_n(&state->location,__ATOMIC_ACQUIRE);if(known)return known;
    JNIEnv *env=attached_environment(selector);static jmethodID query;if(!query)query=location_host_method(env,"lastLocation","()[D");jdoubleArray packet=(*env)->CallObjectMethod(env,activity,query);if(!packet)return 0;
    double values[11];Word result=0;if((*env)->GetArrayLength(env,packet)==COUNT(values)){(*env)->GetDoubleArrayRegion(env,packet,0,COUNT(values),values);result=location_from_packet(values);__atomic_store_n(&state->location,result,__ATOMIC_RELEASE);}(*env)->DeleteLocalRef(env,packet);return result;
}
static void location_request_authorization(Word object,const char *selector){location_manager_state(object);JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=location_host_method(env,"requestLocationAuthorization","(J)V");(*env)->CallVoidMethod(env,activity,method,(jlong)object);}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_deliverLocation)(JNIEnv *env,jobject instance,jlong token,jint generation,jdoubleArray packet,jint code,jstring message){
    may_unwind=1;int failed=setjmp(boundary);
    if(!failed){struct LocationManagerState *state=location_manager_state((Word)token);if(__atomic_load_n(&state->active,__ATOMIC_ACQUIRE) && __atomic_load_n(&state->generation,__ATOMIC_ACQUIRE)==(unsigned)generation){
        if(packet){double values[11];if((*env)->GetArrayLength(env,packet)!=COUNT(values))stop("Invalid location packet","Android location");(*env)->GetDoubleArrayRegion(env,packet,0,COUNT(values),values);location_enqueue(state,0,(unsigned)generation,location_from_packet(values));}
        else{Word detail=message ? string_from_java(env,message,"Location error") : copy_string("The location provider could not provide a position.");location_enqueue(state,1,(unsigned)generation,unsupported_service_error("kCLErrorDomain",code,string_bytes(detail,"Location error")));}
    }}may_unwind=0;return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_deliverLocationAuthorization)(JNIEnv *env,jobject instance,jlong token,jint authorization){
    may_unwind=1;int failed=setjmp(boundary);if(!failed)location_enqueue(location_manager_state((Word)token),2,0,(Word)authorization);may_unwind=0;return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static struct GeocoderState *geocoder_state(Word object){
    pthread_mutex_lock(&location_states_mutex);struct GeocoderState *state=geocoders;while(state && state->object!=object)state=state->next;
    if(!state){state=calloc(1,sizeof(*state));if(!state){pthread_mutex_unlock(&location_states_mutex);stop("Allocation failed","CLGeocoder");}state->object=object;pthread_mutex_init(&state->mutex,NULL);state->next=geocoders;geocoders=state;}pthread_mutex_unlock(&location_states_mutex);return state;
}
struct GeocodeFailure { Word block,error; };
static void geocode_failure_deliver(void *context){struct GeocodeFailure *failure=context;struct Block_layout *block=(void *)failure->block;Word error=failure->error;free(failure);((void (*)(void *,Word,Word))block->invoke)(block,0,error);_Block_release(block);}
static void geocode_fail(Word block,int code,const char *message){if(!block)return;struct GeocodeFailure *failure=calloc(1,sizeof(*failure));if(!failure)stop("Allocation failed","Geocoding completion");failure->block=(Word)_Block_copy((void *)block);failure->error=unsupported_service_error("kCLErrorDomain",code,message);schedule_main_function(geocode_failure_deliver,failure);}
static Word geocoder_active(Word object,const char *selector){struct GeocoderState *state=geocoder_state(object);pthread_mutex_lock(&state->mutex);Word active=state->block!=0;pthread_mutex_unlock(&state->mutex);return active;}
static void geocoder_cancel(Word object,const char *selector){
    struct GeocoderState *state=geocoder_state(object);pthread_mutex_lock(&state->mutex);Word block=state->block;state->block=0;++state->generation;pthread_mutex_unlock(&state->mutex);if(!block)return;
    JNIEnv *env=attached_environment(selector);static jmethodID cancel;if(!cancel)cancel=location_host_method(env,"cancelGeocode","(J)V");(*env)->CallVoidMethod(env,activity,cancel,(jlong)object);geocode_fail(block,10,"The geocoding request was cancelled.");_Block_release((void *)block);
}
static void geocoder_submit(Word object,const char *selector,Word query,struct LocationValue *position,Word region,Word locale,Word completion){
    if(!completion)return;if(!query && !position){geocode_fail(completion,8,"A location or address is required.");return;}
    struct GeocoderState *state=geocoder_state(object);pthread_mutex_lock(&state->mutex);if(state->block){pthread_mutex_unlock(&state->mutex);geocode_fail(completion,2,"A geocoding request is already in progress.");return;}
    unsigned generation=++state->generation;state->block=(Word)_Block_copy((void *)completion);pthread_mutex_unlock(&state->mutex);
    JNIEnv *env=attached_environment(selector);static jmethodID request;if(!request)request=location_host_method(env,"geocode","(JILjava/lang/String;DD[DLjava/lang/String;)V");jstring address=query ? http_java_string(env,query) : NULL,language=NULL;jdoubleArray bounds=NULL;
    if(locale){Method identifier=find_implementation(locale,"localeIdentifier");if(identifier)language=http_java_string(env,identifier(locale,"localeIdentifier",0,0,0,0));}
    if(region){Method center=find_implementation(region,"center"),radius=find_implementation(region,"radius");if(center && radius){
        struct GuestPoint point=((struct GuestPoint (*)(Word,const char *))center)(region,"center");double meters=((double (*)(Word,const char *))radius)(region,"radius");double latitude=meters/111320.0,longitude=latitude/fmax(0.000001,fabs(cos(point.x*M_PI/180.0)));
        double limits[]={fmax(-90,point.x-latitude),fmax(-180,point.y-longitude),fmin(90,point.x+latitude),fmin(180,point.y+longitude)};bounds=(*env)->NewDoubleArray(env,4);(*env)->SetDoubleArrayRegion(env,bounds,0,4,limits);
    }else{pthread_mutex_lock(&state->mutex);Word block=state->block;state->block=0;pthread_mutex_unlock(&state->mutex);geocode_fail(block,8,"The geocoding region has no supported geographic bounds.");_Block_release((void *)block);if(address)(*env)->DeleteLocalRef(env,address);if(language)(*env)->DeleteLocalRef(env,language);return;}}
    (*env)->CallVoidMethod(env,activity,request,(jlong)object,(jint)generation,address,position ? position->coordinate.x : 0,position ? position->coordinate.y : 0,bounds,language);
    if(address)(*env)->DeleteLocalRef(env,address);if(language)(*env)->DeleteLocalRef(env,language);if(bounds)(*env)->DeleteLocalRef(env,bounds);
}
static void geocoder_reverse(Word object,const char *selector,struct LocationValue *position,Word completion){geocoder_submit(object,selector,0,position,0,0,completion);}
static void geocoder_reverse_locale(Word object,const char *selector,struct LocationValue *position,Word locale,Word completion){geocoder_submit(object,selector,0,position,0,locale,completion);}
static void geocoder_address(Word object,const char *selector,Word address,Word completion){geocoder_submit(object,selector,address,NULL,0,0,completion);}
static void geocoder_address_region(Word object,const char *selector,Word address,Word region,Word completion){geocoder_submit(object,selector,address,NULL,region,0,completion);}
static void geocoder_address_region_locale(Word object,const char *selector,Word address,Word region,Word locale,Word completion){geocoder_submit(object,selector,address,NULL,region,locale,completion);}
static Word placemark_field(struct LocationPlacemark *object,const char *selector){return object->fields ? map_get((void *)object->fields,selector,copy_string(selector)) : 0;}
static Word placemark_reference(struct LocationPlacemark *object,const char *selector){return object->location;}
static Word placemark_dictionary(struct LocationPlacemark *object,const char *selector){
    struct GuestMap *address=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));const char *fields[]={"name","thoroughfare","locality","administrativeArea","postalCode","country","ISOcountryCode"},*keys[]={"Name","Street","City","State","ZIP","Country","CountryCode"};
    for(unsigned i=0;i<COUNT(fields);++i){Word value=placemark_field(object,fields[i]);if(value)map_set(address,selector,value,copy_string(keys[i]));}return (Word)address;
}
static Word placemark_initialize(struct LocationPlacemark *object,const char *selector,struct LocationPlacemark *other){if(other){object->fields=other->fields;object->location=other->location;object->postal=other->postal;}return (Word)object;}
static Word placemark_postal(struct LocationPlacemark *object,const char *selector){
    if(object->postal)return object->postal;struct HostObject *postal=(void *)host_allocate(external_class("_OBJC_CLASS_$_CNPostalAddress"));struct GuestMap *fields=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    const char *original[]={"thoroughfare","locality","subLocality","administrativeArea","subAdministrativeArea","postalCode","country","ISOcountryCode"},*names[]={"street","city","subLocality","state","subAdministrativeArea","postalCode","country","ISOCountryCode"};
    for(unsigned i=0;i<COUNT(original);++i){Word value=placemark_field(object,original[i]);if(value)map_set(fields,selector,value,copy_string(names[i]));}
    Word street=placemark_field(object,"thoroughfare"),number=placemark_field(object,"subThoroughfare");if(number && street){Word line=mutable_string(external_class("_OBJC_CLASS_$_NSMutableString"));append_string((void *)line,selector,number);append_string((void *)line,selector,copy_string(" "));append_string((void *)line,selector,street);map_set(fields,selector,line,copy_string("street"));}
    postal->content[0]=(Word)fields;object->postal=(Word)postal;return object->postal;
}
static Word postal_field(struct HostObject *object,const char *selector){Word value=object->content[0] ? map_get((void *)object->content[0],selector,copy_string(selector)) : 0;return value ? value : copy_string("");}
static Word postal_copy(struct HostObject *object,const char *selector){struct HostObject *copy=(void *)host_allocate(external_class(!strncmp(selector,"mutable",7) ? "_OBJC_CLASS_$_CNMutablePostalAddress" : "_OBJC_CLASS_$_CNPostalAddress"));copy->content[0]=object->content[0] ? map_copy((void *)object->content[0],"copy") : empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));return (Word)copy;}
static void postal_set_field(struct HostObject *object,const char *selector,Word value){
    if(!object->content[0])object->content[0]=empty_map(external_class("_OBJC_CLASS_$_NSMutableDictionary"));size_t length=strlen(selector);char name[64];if(length<5 || length-4>=sizeof(name))stop("Invalid postal address property",selector);
    memcpy(name,selector+3,length-4);name[length-4]=0;if(strcmp(name,"ISOCountryCode"))name[0]=(char)(name[0]+'a'-'A');map_set((void *)object->content[0],selector,value ? text_copy(value,"copy") : copy_string(""),copy_string(name));
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completeGeocode)(JNIEnv *env,jobject instance,jlong token,jint generation,jstring places,jint code,jstring message){
    may_unwind=1;volatile Word owned_block=0;int failed=setjmp(boundary);
    if(!failed){struct GeocoderState *state=geocoder_state((Word)token);pthread_mutex_lock(&state->mutex);if(state->generation==(unsigned)generation){owned_block=state->block;state->block=0;}pthread_mutex_unlock(&state->mutex);
        if(owned_block){Word result=0,error=0;
            if(code){Word detail=message ? string_from_java(env,message,"Geocoding error") : copy_string("The geocoding request could not be completed.");error=unsupported_service_error("kCLErrorDomain",code,string_bytes(detail,"Geocoding error"));}
            else if(places){Word encoded=string_from_java(env,places,"Geocoding result");const char *text=string_bytes(encoded,"Geocoding result");plist_t root=NULL;if(plist_from_json(text,strlen(text),&root)!=PLIST_ERR_SUCCESS)stop("Invalid geocoding result","CoreLocation");Word data=plist_object(root,0);plist_free(root);struct Sequence *items=(void *)data,*values=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
                for(size_t i=0;i<items->count;++i){struct LocationPlacemark *place=(void *)host_allocate(external_class("_OBJC_CLASS_$_CLPlacemark"));place->fields=items->items[i];struct Sequence *coordinate=(void *)placemark_field(place,"coordinate");if(coordinate && coordinate->count==2){place->location=host_allocate(external_class("_OBJC_CLASS_$_CLLocation"));location_initialize_coordinates(place->location,"Geocoding result",numeric_double((void *)coordinate->items[0]),numeric_double((void *)coordinate->items[1]));}sequence_add(values,"Geocoding result",(Word)place);}result=(Word)values;
            }
            struct Block_layout *block=(void *)owned_block;((void (*)(void *,Word,Word))block->invoke)(block,result,error);
        }
    }
    if(owned_block)_Block_release((void *)owned_block);may_unwind=0;return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
JNIEXPORT void JNICALL HOST_JNI(HostActivity_closeLocationSources)(JNIEnv *env,jobject instance){
    pthread_mutex_lock(&location_states_mutex);
    struct LocationManagerState *managers=location_managers;struct GeocoderState *requests=geocoders;
    pthread_mutex_unlock(&location_states_mutex);
    for(struct LocationManagerState *state=managers;state;state=state->next){__atomic_store_n(&state->active,0,__ATOMIC_RELEASE);__atomic_add_fetch(&state->generation,1,__ATOMIC_ACQ_REL);if(state->work)loop_function_close(state->work);pthread_mutex_lock(&state->mutex);struct LocationEvent *event=state->first;state->first=state->last=NULL;pthread_mutex_unlock(&state->mutex);while(event){struct LocationEvent *next=event->next;free(event);event=next;}}
    for(struct GeocoderState *state=requests;state;state=state->next){pthread_mutex_lock(&state->mutex);Word block=state->block;state->block=0;++state->generation;pthread_mutex_unlock(&state->mutex);if(block)_Block_release((void *)block);}
}
static Method location_method(Word object,const char *selector,const char *receiver,int class_receiver){
    if(!receiver)return NULL;Word cls=class_receiver ? object : read_word(object);
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_CLLocation")) && !class_receiver){
        if(!strcmp(selector,"init"))return (Method)location_initialize;if(!strcmp(selector,"initWithLatitude:longitude:"))return (Method)location_initialize_coordinates;
        if(!strcmp(selector,"initWithCoordinate:altitude:horizontalAccuracy:verticalAccuracy:timestamp:"))return (Method)location_initialize_accuracy;
        if(!strcmp(selector,"initWithCoordinate:altitude:horizontalAccuracy:verticalAccuracy:course:speed:timestamp:"))return (Method)location_initialize_motion;
        if(!strcmp(selector,"initWithCoordinate:altitude:horizontalAccuracy:verticalAccuracy:course:courseAccuracy:speed:speedAccuracy:timestamp:"))return (Method)location_initialize_full;
        if(!strcmp(selector,"coordinate"))return (Method)location_coordinate;
        if(!strcmp(selector,"altitude") || !strcmp(selector,"horizontalAccuracy") || !strcmp(selector,"verticalAccuracy") || !strcmp(selector,"course") || !strcmp(selector,"speed") || !strcmp(selector,"courseAccuracy") || !strcmp(selector,"speedAccuracy"))return (Method)location_measurement;
        if(!strcmp(selector,"timestamp") || !strcmp(selector,"sourceInformation"))return (Method)location_reference;
        if(!strcmp(selector,"distanceFromLocation:") || !strcmp(selector,"getDistanceFrom:"))return (Method)location_distance;
        if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)identity;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_CLLocationSourceInformation")) && !class_receiver){if(!strcmp(selector,"isSimulatedBySoftware") || !strcmp(selector,"isProducedByAccessory"))return (Method)location_source_property;if(!strcmp(selector,"initWithSoftwareSimulationState:andExternalAccessoryState:"))return (Method)location_source_init;}
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_CLLocationManager"))){
        if(!strcmp(selector,"authorizationStatus"))return (Method)location_authorization;if(class_receiver && !strcmp(selector,"locationServicesEnabled"))return (Method)location_enabled;
        if(!class_receiver){if(!strcmp(selector,"init"))return (Method)location_manager_initialize;if(!strcmp(selector,"delegate"))return (Method)location_delegate;if(!strcmp(selector,"setDelegate:"))return (Method)location_set_delegate;
            if(!strcmp(selector,"location"))return (Method)location_current;if(!strcmp(selector,"startUpdatingLocation") || !strcmp(selector,"requestLocation"))return (Method)location_start;if(!strcmp(selector,"stopUpdatingLocation"))return (Method)location_stop;
            if(!strcmp(selector,"desiredAccuracy") || !strcmp(selector,"distanceFilter"))return (Method)location_manager_measurement;if(!strcmp(selector,"setDesiredAccuracy:") || !strcmp(selector,"setDistanceFilter:"))return (Method)location_manager_set_measurement;
            if(!strcmp(selector,"requestWhenInUseAuthorization") || !strcmp(selector,"requestAlwaysAuthorization"))return (Method)location_request_authorization;
        }
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_CLGeocoder")) && !class_receiver){
        if(!strcmp(selector,"isGeocoding") || !strcmp(selector,"geocoding"))return (Method)geocoder_active;if(!strcmp(selector,"cancelGeocode"))return (Method)geocoder_cancel;
        if(!strcmp(selector,"reverseGeocodeLocation:completionHandler:"))return (Method)geocoder_reverse;if(!strcmp(selector,"reverseGeocodeLocation:preferredLocale:completionHandler:"))return (Method)geocoder_reverse_locale;
        if(!strcmp(selector,"geocodeAddressString:completionHandler:"))return (Method)geocoder_address;if(!strcmp(selector,"geocodeAddressString:inRegion:completionHandler:"))return (Method)geocoder_address_region;
        if(!strcmp(selector,"geocodeAddressString:inRegion:preferredLocale:completionHandler:"))return (Method)geocoder_address_region_locale;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_CLPlacemark")) && !class_receiver){
        if(!strcmp(selector,"location"))return (Method)placemark_reference;if(!strcmp(selector,"addressDictionary"))return (Method)placemark_dictionary;if(!strcmp(selector,"postalAddress"))return (Method)placemark_postal;
        if(!strcmp(selector,"initWithPlacemark:"))return (Method)placemark_initialize;if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)identity;
        const char *fields[]={"name","thoroughfare","subThoroughfare","locality","subLocality","administrativeArea","subAdministrativeArea","postalCode","country","ISOcountryCode","inlandWater","ocean","areasOfInterest","region","timeZone"};for(unsigned i=0;i<COUNT(fields);++i)if(!strcmp(selector,fields[i]))return (Method)placemark_field;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_CNPostalAddress")) && !class_receiver){
        if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:") || !strcmp(selector,"mutableCopy") || !strcmp(selector,"mutableCopyWithZone:"))return (Method)postal_copy;
        const char *fields[]={"street","city","subLocality","state","subAdministrativeArea","postalCode","country","ISOCountryCode"};for(unsigned i=0;i<COUNT(fields);++i){if(!strcmp(selector,fields[i]))return (Method)postal_field;size_t n=strlen(fields[i]);char initial=fields[i][0]>='a' && fields[i][0]<='z' ? (char)(fields[i][0]-'a'+'A') : fields[i][0];if(class_inherits(cls,external_class("_OBJC_CLASS_$_CNMutablePostalAddress")) && strlen(selector)==n+4 && !strncmp(selector,"set",3) && selector[3]==initial && !memcmp(selector+4,fields[i]+1,n-1) && selector[n+3]==':')return (Method)postal_set_field;}
    }
    return NULL;
}
