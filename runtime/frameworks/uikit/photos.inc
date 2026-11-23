 
struct PhotoWrite { Word image,target; const char *selector; void *context; };
static Word drawing_data_from_java(JNIEnv *,jbyteArray,const char *);
struct PhotoPickerState { Word object,delegate,types,completion;long source;unsigned editing,active;struct PhotoPickerState *next; };
struct PhotoOptions { long version,delivery,resize;unsigned synchronous,network; };
struct PhotoRead { Word identifier,block;unsigned kind;struct PhotoRead *next; };
struct PhotoAuthorization { Word block;unsigned legacy; };
static struct PhotoPickerState *photo_pickers;
static struct PhotoRead *photo_reads;
static pthread_mutex_t photo_reads_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned photo_request_sequence;
static struct HostObject photo_manager,photo_library;
static jmethodID photo_host_method(JNIEnv *env,const char *name,const char *signature) {
    jclass cls=(*env)->GetObjectClass(env,activity);jmethodID method=(*env)->GetMethodID(env,cls,name,signature);(*env)->DeleteLocalRef(env,cls);return method;
}
static void photo_host_failure(JNIEnv *env,const char *selector) { if((*env)->ExceptionCheck(env)){(*env)->ExceptionClear(env);stop("Android photo operation failed",selector);} }
static Word photo_json(JNIEnv *env,jstring value) {
    if(!value)return 0;Word text=string_from_java(env,value,"Photo metadata"),bytes=encoded_string(text,"Photo metadata",4),error=0;return json_parse(0,"Photo metadata",(void *)bytes,0,&error);
}
static Word photo_value(Word metadata,const char *key) { return metadata ? map_get((void *)metadata,"Photo metadata",copy_string(key)) : 0; }
static long photo_integer(Word metadata,const char *key) { Word value=photo_value(metadata,key);return value ? (long)numeric_integer((void *)value) : 0; }
static Word photo_asset(Word metadata) {
    if(!metadata)return 0;struct HostObject *asset=(void *)host_allocate(external_class("_OBJC_CLASS_$_PHAsset"));asset->content[0]=metadata;return (Word)asset;
}
static struct PhotoPickerState *photo_picker(Word object) {
    for(struct PhotoPickerState *picker=photo_pickers;picker;picker=picker->next)if(picker->object==object)return picker;
    struct PhotoPickerState *picker=calloc(1,sizeof(*picker));if(!picker)stop("Allocation failed","UIImagePickerController");picker->object=object;Word image=copy_string("public.image");picker->types=new_array(external_class("_OBJC_CLASS_$_NSArray"),"Photo media types",&image,1);picker->next=photo_pickers;photo_pickers=picker;return picker;
}
static Word photo_source_available(Word cls,const char *selector,long source) {
    JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=photo_host_method(env,"photoSourceAvailable","(I)Z");jboolean result=(*env)->CallBooleanMethod(env,activity,method,(jint)source);photo_host_failure(env,selector);return result;
}
static Word photo_available_types(Word cls,const char *selector,long source) { if(!photo_source_available(cls,selector,source))return 0;Word image=copy_string("public.image");return new_array(external_class("_OBJC_CLASS_$_NSArray"),selector,&image,1); }
static Word photo_picker_init(Word object,const char *selector) { photo_picker(object);return controller_init_nib(object,selector,0,0); }
static Word photo_picker_property(Word object,const char *selector) { struct PhotoPickerState *picker=photo_picker(object);if(!strcmp(selector,"delegate"))return picker->delegate;if(!strcmp(selector,"sourceType"))return picker->source;if(!strcmp(selector,"mediaTypes"))return picker->types;return picker->editing; }
static void photo_picker_set(Word object,const char *selector,Word value) {
    struct PhotoPickerState *picker=photo_picker(object);
    if(!strcmp(selector,"setDelegate:"))store_weak(&picker->delegate,value);
    else if(!strcmp(selector,"setSourceType:")){if(!photo_source_available(0,selector,(long)value))stop("The requested photo source is unavailable",selector);picker->source=(long)value;}
    else if(!strcmp(selector,"setAllowsEditing:"))picker->editing=value!=0;
    else{struct Sequence *types=(void *)value;if(!types || !types->count)stop("Photo media types must not be empty",selector);for(size_t i=0;i<types->count;++i)if(strcmp(string_bytes(types->items[i],selector),"public.image"))stop("The photo picker supports image media",selector);picker->types=sequence_copy(types,"copy");}
}
static void photo_picker_clear(struct PhotoPickerState *picker) {
    Word presenter=view_state(picker->object)->presenting;if(presenter && view_state(presenter)->presented==picker->object)view_state(presenter)->presented=0;view_state(picker->object)->presenting=0;picker->active=0;
}
static void photo_present(Word presenter,const char *selector,Word object,Word completion) {
    struct PhotoPickerState *picker=photo_picker(object);if(picker->active)stop("The photo picker is already presented",selector);if(picker->editing)stop("Photo picker cropping is unavailable",selector);
    picker->active=1;picker->completion=completion ? (Word)_Block_copy((void *)completion) : 0;view_state(presenter)->presented=object;view_state(object)->presenting=presenter;
    JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=photo_host_method(env,"showGuestPhotoPicker","(J)V");(*env)->CallVoidMethod(env,activity,method,(jlong)object);photo_host_failure(env,selector);
}
static void photo_dismiss(Word object,const char *selector,Word completion) {
    struct PhotoPickerState *picker=photo_picker(object);photo_picker_clear(picker);if(picker->completion){_Block_release((void *)picker->completion);picker->completion=0;}
    JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=photo_host_method(env,"dismissGuestPhotoPicker","(JJ)V");(*env)->CallVoidMethod(env,activity,method,(jlong)object,(jlong)completion);photo_host_failure(env,selector);
}
static Word photo_error(long code) { return web_error("PHPhotosErrorDomain",(int)code,code==3072 ? "The photo request was cancelled." : code==3311 ? "Photo access was denied." : code==3164 ? "The photo provider requires network access." : code==3303 ? "This photo version or provider policy is unavailable." : "The photo could not be read.",0); }
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completePhotoPicker)(JNIEnv *env,jobject instance,jlong token,jint result,jstring description,jbyteArray bytes) {
    struct PhotoPickerState *picker=photo_picker(token);if(!picker->active)return NULL;may_unwind=1;int failed=setjmp(boundary);Word completion=0;
    if(!failed && result==2){completion=picker->completion;picker->completion=0;if(completion)((void (*)(Word))read_word(completion+16))(completion);}
    else if(!failed){
        if(result<0)stop("Android photo selection failed",description ? string_bytes(string_from_java(env,description,"Photo picker"),"Photo picker") : "Photo picker unavailable");
        if(result==0){Method callback=picker->delegate ? find_implementation(picker->delegate,"imagePickerControllerDidCancel:") : NULL;if(callback)callback(picker->delegate,"imagePickerControllerDidCancel:",token,0,0,0);}
        else{Word metadata=photo_json(env,description),data=drawing_data_from_java(env,bytes,"Selected photo"),image=image_from_data(external_class("_OBJC_CLASS_$_UIImage"),"Selected photo",(void *)data);if(!image)stop("Selected photo is not a supported image","UIImagePickerController");
            Word info=empty_map(external_class("_OBJC_CLASS_$_NSDictionary")),address=photo_value(metadata,"uri"),url=address ? url_from_string(external_class("_OBJC_CLASS_$_NSURL"),"URLWithString:",address) : 0;
            map_set((void *)info,"Selected photo",image,copy_string("UIImagePickerControllerOriginalImage"));map_set((void *)info,"Selected photo",copy_string("public.image"),copy_string("UIImagePickerControllerMediaType"));
            if(url){map_set((void *)info,"Selected photo",url,copy_string("UIImagePickerControllerReferenceURL"));map_set((void *)info,"Selected photo",url,copy_string("UIImagePickerControllerImageURL"));}map_set((void *)info,"Selected photo",photo_asset(metadata),copy_string("UIImagePickerControllerPHAsset"));
            Method callback=picker->delegate ? find_implementation(picker->delegate,"imagePickerController:didFinishPickingMediaWithInfo:") : NULL;
            if(callback)callback(picker->delegate,"imagePickerController:didFinishPickingMediaWithInfo:",token,info,0,0);
            else{callback=picker->delegate ? find_implementation(picker->delegate,"imagePickerController:didFinishPickingImage:editingInfo:") : NULL;if(callback)callback(picker->delegate,"imagePickerController:didFinishPickingImage:editingInfo:",token,image,0,0);}
        }
    }
    if(completion)_Block_release((void *)completion);may_unwind=0;return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Word photo_authorization_access(Word object,const char *selector,long access) {
    if(access!=1 && access!=2)stop("Invalid photo access level",selector);JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=photo_host_method(env,"photoAuthorization","(I)I");jint status=(*env)->CallIntMethod(env,activity,method,(jint)access);photo_host_failure(env,selector);return status;
}
static Word photo_authorization(Word object,const char *selector) { Word status=photo_authorization_access(object,selector,2);return status==4 ? 3 : status; }
static void photo_request_access(Word object,const char *selector,long access,Word block) {
    if(!block)return;if(access!=1 && access!=2)stop("Invalid photo access level",selector);struct PhotoAuthorization *request=calloc(1,sizeof(*request));if(!request)stop("Allocation failed",selector);request->block=(Word)_Block_copy((void *)block);request->legacy=!strcmp(selector,"requestAuthorization:");JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=photo_host_method(env,"authorizePhotos","(JI)V");(*env)->CallVoidMethod(env,activity,method,(jlong)(Word)request,(jint)access);if((*env)->ExceptionCheck(env)){_Block_release((void *)request->block);free(request);photo_host_failure(env,selector);}
}
static void photo_request_authorization(Word object,const char *selector,Word block) { photo_request_access(object,selector,2,block); }
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completePhotoAuthorization)(JNIEnv *env,jobject instance,jlong token,jint result) {
    struct PhotoAuthorization *request=(void *)(Word)token;if(!request)return NULL;Word block=request->block;may_unwind=1;int failed=setjmp(boundary);if(!failed && block)((void (*)(Word,Word))read_word(block+16))(block,(Word)(request->legacy && result==4 ? 3 : result));if(block)_Block_release((void *)block);free(request);may_unwind=0;return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Word photo_shared(Word cls,const char *selector) { struct HostObject *shared=!strcmp(selector,"defaultManager") ? &photo_manager : &photo_library;shared->isa=cls;return (Word)shared; }
static struct PhotoOptions *photo_options(Word object) {
    struct HostObject *host=(void *)object;if(!host->content[0]){struct PhotoOptions *options=calloc(1,sizeof(*options));if(!options)stop("Allocation failed","Photo options");host->content[0]=(Word)options;}return (void *)host->content[0];
}
static Word photo_options_init(Word object,const char *selector) { photo_options(object);return object; }
static Word photo_options_copy(Word object,const char *selector,Word zone) { Word copy=host_allocate(read_word(object));*photo_options(copy)=*photo_options(object);return copy; }
static Word photo_option(Word object,const char *selector) { struct PhotoOptions *options=photo_options(object);if(!strcmp(selector,"version"))return options->version;if(!strcmp(selector,"deliveryMode"))return options->delivery;if(!strcmp(selector,"resizeMode"))return options->resize;if(!strcmp(selector,"isSynchronous"))return options->synchronous;return options->network; }
static void photo_set_option(Word object,const char *selector,Word value) {
    struct PhotoOptions *options=photo_options(object);if(!strcmp(selector,"setVersion:")){if(value>2)stop("Invalid photo request version",selector);options->version=(long)value;}else if(!strcmp(selector,"setDeliveryMode:")){if(value>2)stop("Invalid photo delivery mode",selector);options->delivery=(long)value;}else if(!strcmp(selector,"setResizeMode:")){if(value>2)stop("Invalid photo resize mode",selector);options->resize=(long)value;}else if(!strcmp(selector,"setSynchronous:"))options->synchronous=value!=0;else options->network=value!=0;
}
static Word photo_fetch(Word cls,const char *selector,Word identifiers,Word options) {
    struct Sequence *input=(void *)identifiers;Word result=sequence_empty(external_class("_OBJC_CLASS_$_PHFetchResult"));int all=!strcmp(selector,"fetchAssetsWithOptions:") || !strcmp(selector,"fetchAssetsWithMediaType:options:");if(!all && (!input || !input->count))return result;
    Word limit=options ? ((struct HostObject *)options)->content[0] : 0;if((input && input->count>INT_MAX) || limit>INT_MAX)stop("Photo fetch exceeds Android array limits",selector);
    JNIEnv *env=attached_environment(selector);jobjectArray values=NULL;if(input){jclass strings=(*env)->FindClass(env,"java/lang/String");values=(*env)->NewObjectArray(env,(jsize)input->count,strings,NULL);(*env)->DeleteLocalRef(env,strings);
        for(size_t i=0;i<input->count;++i){Word value=strstr(selector,"ALAssetURLs") ? ui_url_string(input->items[i],selector) : input->items[i];jstring text=value ? http_java_string(env,value) : NULL;(*env)->SetObjectArrayElement(env,values,(jsize)i,text);if(text)(*env)->DeleteLocalRef(env,text);}}
    static jmethodID method;if(!method)method=photo_host_method(env,"fetchPhotoAssets","([Ljava/lang/String;I)[Ljava/lang/String;");jobjectArray records=(*env)->CallObjectMethod(env,activity,method,values,(jint)limit);if(values)(*env)->DeleteLocalRef(env,values);photo_host_failure(env,selector);
    if(records){jsize count=(*env)->GetArrayLength(env,records);for(jsize i=0;i<count;++i){jstring record=(*env)->GetObjectArrayElement(env,records,i);Word asset=photo_asset(photo_json(env,record));if(asset)sequence_add((void *)result,selector,asset);if(record)(*env)->DeleteLocalRef(env,record);}(*env)->DeleteLocalRef(env,records);}return result;
}
static Word photo_fetch_all(Word cls,const char *selector,Word options) { return photo_fetch(cls,selector,0,options); }
static Word photo_fetch_media(Word cls,const char *selector,long media,Word options) { if(media!=1)stop("Photo library enumeration supports image assets",selector);return photo_fetch(cls,selector,0,options); }
static Word photo_fetch_limit(struct HostObject *object) { return object->content[0]; }
static void photo_set_fetch_limit(struct HostObject *object,const char *selector,Word limit) { if(limit>INT_MAX)stop("Invalid photo fetch limit",selector);object->content[0]=limit; }
static Word photo_fetch_options_copy(Word object,const char *selector,Word zone) { Word copy=host_allocate(read_word(object));((struct HostObject *)copy)->content[0]=((struct HostObject *)object)->content[0];return copy; }
static Word photo_asset_property(struct HostObject *object,const char *selector) {
    Word metadata=object->content[0];if(!strcmp(selector,"localIdentifier"))return photo_value(metadata,"uri");if(!strcmp(selector,"pixelWidth"))return photo_integer(metadata,"width");if(!strcmp(selector,"pixelHeight"))return photo_integer(metadata,"height");if(!strcmp(selector,"mediaType"))return 1;
    if(!strcmp(selector,"creationDate") || !strcmp(selector,"modificationDate")){long seconds=photo_integer(metadata,!strcmp(selector,"creationDate") ? "date" : "modified");return seconds ? date_epoch(external_class("_OBJC_CLASS_$_NSDate"),selector,!strcmp(selector,"creationDate") ? seconds/1000.0 : seconds) : 0;}
    if(!strcmp(selector,"isFavorite"))return photo_integer(metadata,"favorite")!=0;return 0;
}
static double photo_duration(Word object) { return 0; }
static Word photo_asset_equal(struct HostObject *object,const char *selector,Word other) { return other && class_inherits(read_word(other),external_class("_OBJC_CLASS_$_PHAsset")) && equal_objects(photo_value(object->content[0],"uri"),selector,photo_value(((struct HostObject *)other)->content[0],"uri")); }
static Word photo_asset_hash(struct HostObject *object) { return foundation_string_hash(photo_value(object->content[0],"uri"),"hash"); }
static Word photo_fetch_count(struct Sequence *result) { return result->count; }
static Word photo_fetch_first(struct Sequence *result) { return result->count ? result->items[0] : 0; }
static Word photo_fetch_last(struct Sequence *result) { return result->count ? result->items[result->count-1] : 0; }
static Word photo_fetch_index(struct Sequence *result,const char *selector,Word object) { for(size_t i=0;i<result->count;++i)if(photo_asset_equal((void *)result->items[i],selector,object))return i;return INT64_MAX; }
static Word photo_fetch_contains(struct Sequence *result,const char *selector,Word object) { return photo_fetch_index(result,selector,object)!=INT64_MAX; }
static Word photo_fetch_media_count(struct Sequence *result,const char *selector,Word media) { return media==1 ? result->count : 0; }
static void photo_fetch_enumerate(struct Sequence *result,const char *selector,Word options,Word block) {
    if(!block)stop("Photo enumeration requires a block",selector);if(options&~(Word)3)stop("Invalid photo enumeration options",selector);unsigned char done=0;
    for(size_t n=0;n<result->count && !done;++n){size_t index=options&2 ? result->count-n-1 : n;((void (*)(Word,Word,Word,unsigned char *))read_word(block+16))(block,result->items[index],index,&done);}
}
static void photo_fetch_enumerate_plain(struct Sequence *result,const char *selector,Word block) { photo_fetch_enumerate(result,selector,0,block); }
static Word photo_fetch_copy(struct Sequence *result,const char *selector) { return new_array(result->isa,selector,result->items,result->count); }
static struct PhotoRead *photo_take_request(Word identifier) {
    pthread_mutex_lock(&photo_reads_mutex);struct PhotoRead **link=&photo_reads;while(*link && (*link)->identifier!=identifier)link=&(*link)->next;struct PhotoRead *request=*link;if(request)*link=request->next;pthread_mutex_unlock(&photo_reads_mutex);return request;
}
static void photo_read_deliver(JNIEnv *env,struct PhotoRead *request,jbyteArray bytes,jstring json,int error) {
    Word metadata=photo_json(env,json),data=bytes ? drawing_data_from_java(env,bytes,"Photo data") : 0,info=empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set((void *)info,"Photo result",number_integer(request->identifier),copy_string("PHImageResultRequestIDKey"));map_set((void *)info,"Photo result",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Photo result",0),copy_string("PHImageResultIsDegradedKey"));
    if(error){map_set((void *)info,"Photo result",photo_error(error),copy_string("PHImageErrorKey"));if(error==3072)map_set((void *)info,"Photo result",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Photo cancellation",1),copy_string("PHImageCancelledKey"));if(error==3164)map_set((void *)info,"Photo result",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"Photo cloud",1),copy_string("PHImageResultIsInCloudKey"));}
    if(request->kind==2){Word image=data ? image_from_data(external_class("_OBJC_CLASS_$_UIImage"),"Photo result",(void *)data) : 0;if(data && !image)map_set((void *)info,"Photo result",photo_error(3302),copy_string("PHImageErrorKey"));((void (*)(Word,Word,Word))read_word(request->block+16))(request->block,image,info);}
    else{const char *mime=metadata ? string_bytes(photo_value(metadata,"mime"),"Photo mime") : "";Word type=copy_string(!strcmp(mime,"image/jpeg") ? "public.jpeg" : !strcmp(mime,"image/png") ? "public.png" : !strcmp(mime,"image/heic") ? "public.heic" : !strcmp(mime,"image/gif") ? "com.compuserve.gif" : !strcmp(mime,"image/webp") ? "org.webmproject.webp" : "public.image");long orientation=photo_integer(metadata,"orientation");if(orientation<1 || orientation>8)orientation=1;static const unsigned ui_orientation[]={0,0,4,1,5,6,3,7,2};
        ((void (*)(Word,Word,Word,long,Word))read_word(request->block+16))(request->block,data,data ? type : 0,request->kind ? orientation : ui_orientation[orientation],info);}
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completePhotoRead)(JNIEnv *env,jobject instance,jlong token,jbyteArray bytes,jstring metadata,jint error) {
    struct PhotoRead *request=photo_take_request(token);if(!request)return NULL;may_unwind=1;int failed=setjmp(boundary);if(!failed)photo_read_deliver(env,request,bytes,metadata,error);_Block_release((void *)request->block);free(request);may_unwind=0;return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Word photo_read_request(Word asset,Word options,Word block,unsigned kind,struct GuestSize size,long mode) {
    if(!block)stop("Photo request requires a result handler","PHImageManager");if(mode<0 || mode>1)stop("Invalid photo content mode","PHImageManager");
    struct PhotoOptions defaults={0},*settings=options ? photo_options(options) : &defaults;struct PhotoRead *request=calloc(1,sizeof(*request));if(!request)stop("Allocation failed","Photo request");request->identifier=__atomic_add_fetch(&photo_request_sequence,1,__ATOMIC_RELAXED);request->block=(Word)_Block_copy((void *)block);request->kind=kind;
    if(!asset || !class_inherits(read_word(asset),external_class("_OBJC_CLASS_$_PHAsset"))){_Block_release((void *)request->block);free(request);stop("Photo request requires an asset","PHImageManager");}
    int width=size.width>0 && size.width<INT_MAX ? (int)ceil(size.width) : 0,height=size.height>0 && size.height<INT_MAX ? (int)ceil(size.height) : 0;
    jint arguments[]={kind==2,settings->network,settings->version,width,height,(int)mode,(int)settings->resize};JNIEnv *env=attached_environment("Photo request");jintArray flags=(*env)->NewIntArray(env,COUNT(arguments));(*env)->SetIntArrayRegion(env,flags,0,COUNT(arguments),arguments);jstring uri=http_java_string(env,photo_value(((struct HostObject *)asset)->content[0],"uri"));Word identifier=request->identifier;
    if(settings->synchronous){static jmethodID method;if(!method)method=photo_host_method(env,"readPhotoData","(Ljava/lang/String;[I)[Ljava/lang/Object;");jobjectArray result=(*env)->CallObjectMethod(env,activity,method,uri,flags);
        struct GuestCallBoundary call={.previous=active_guest_call_boundary};active_guest_call_boundary=&call;int failed=setjmp(call.environment);
        if(!failed){photo_host_failure(env,"Photo read");if(!result)stop("Android returned no photo result","PHImageManager");jbyteArray bytes=(*env)->GetObjectArrayElement(env,result,0);jstring metadata=(*env)->GetObjectArrayElement(env,result,1);jobject error=(*env)->GetObjectArrayElement(env,result,2);jclass cls=(*env)->GetObjectClass(env,error);jmethodID value=(*env)->GetMethodID(env,cls,"intValue","()I");int code=(*env)->CallIntMethod(env,error,value);photo_read_deliver(env,request,bytes,metadata,code);if(bytes)(*env)->DeleteLocalRef(env,bytes);if(metadata)(*env)->DeleteLocalRef(env,metadata);(*env)->DeleteLocalRef(env,error);(*env)->DeleteLocalRef(env,cls);}
        active_guest_call_boundary=call.previous;if(result)(*env)->DeleteLocalRef(env,result);_Block_release((void *)request->block);free(request);(*env)->DeleteLocalRef(env,uri);(*env)->DeleteLocalRef(env,flags);if(failed){char error[sizeof(status)];memcpy(error,status,sizeof(error));stop("Synchronous photo request failed",error);}return identifier;
    }
    pthread_mutex_lock(&photo_reads_mutex);request->next=photo_reads;photo_reads=request;pthread_mutex_unlock(&photo_reads_mutex);static jmethodID method;if(!method)method=photo_host_method(env,"requestPhotoData","(JLjava/lang/String;[I)V");(*env)->CallVoidMethod(env,activity,method,(jlong)identifier,uri,flags);(*env)->DeleteLocalRef(env,uri);(*env)->DeleteLocalRef(env,flags);
    if((*env)->ExceptionCheck(env)){request=photo_take_request(identifier);if(request){_Block_release((void *)request->block);free(request);}photo_host_failure(env,"Photo request");}return identifier;
}
static Word photo_request_data(Word object,const char *selector,Word asset,Word options,Word block) { return photo_read_request(asset,options,block,strstr(selector,"AndOrientation") ? 1 : 0,(struct GuestSize){0},0); }
static Word photo_request_image(Word object,const char *selector,Word asset,struct GuestSize size,long mode,Word options,Word block) { if(isnan(size.width) || isnan(size.height) || size.width<0 || size.height<0)stop("Invalid photo target size",selector);return photo_read_request(asset,options,block,2,size,mode); }
static void photo_cancel(Word object,const char *selector,Word identifier) { JNIEnv *env=attached_environment(selector);static jmethodID method;if(!method)method=photo_host_method(env,"cancelPhotoRequest","(J)V");(*env)->CallVoidMethod(env,activity,method,(jlong)identifier);photo_host_failure(env,selector); }
static void photo_options_dealloc(struct HostObject *object) { free((void *)object->content[0]);object->content[0]=0; }
static Method photos_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if(!receiver)return NULL;Word cls=class_receiver ? object : read_word(object);
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_UIImagePickerController"))){
        if(class_receiver){if(!strcmp(selector,"isSourceTypeAvailable:"))return (Method)photo_source_available;if(!strcmp(selector,"availableMediaTypesForSourceType:"))return (Method)photo_available_types;return NULL;}
        if(!strcmp(selector,"init"))return (Method)photo_picker_init;
        const char *getters[]={"delegate","sourceType","mediaTypes","allowsEditing"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)photo_picker_property;
        const char *setters[]={"setDelegate:","setSourceType:","setMediaTypes:","setAllowsEditing:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)photo_picker_set;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_PHPhotoLibrary")) && class_receiver){if(!strcmp(selector,"sharedPhotoLibrary"))return (Method)photo_shared;if(!strcmp(selector,"authorizationStatus"))return (Method)photo_authorization;if(!strcmp(selector,"authorizationStatusForAccessLevel:"))return (Method)photo_authorization_access;if(!strcmp(selector,"requestAuthorization:"))return (Method)photo_request_authorization;if(!strcmp(selector,"requestAuthorizationForAccessLevel:handler:"))return (Method)photo_request_access;}
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_PHImageManager"))){if(class_receiver && !strcmp(selector,"defaultManager"))return (Method)photo_shared;if(!class_receiver){if(!strcmp(selector,"requestImageDataForAsset:options:resultHandler:") || !strcmp(selector,"requestImageDataAndOrientationForAsset:options:resultHandler:"))return (Method)photo_request_data;if(!strcmp(selector,"requestImageForAsset:targetSize:contentMode:options:resultHandler:"))return (Method)photo_request_image;if(!strcmp(selector,"cancelImageRequest:"))return (Method)photo_cancel;}}
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_PHImageRequestOptions")) && !class_receiver){
        if(!strcmp(selector,"init"))return (Method)photo_options_init;if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)photo_options_copy;if(!strcmp(selector,"dealloc"))return (Method)photo_options_dealloc;
        const char *getters[]={"version","deliveryMode","resizeMode","isSynchronous","isNetworkAccessAllowed"};for(unsigned i=0;i<COUNT(getters);++i)if(!strcmp(selector,getters[i]))return (Method)photo_option;
        const char *setters[]={"setVersion:","setDeliveryMode:","setResizeMode:","setSynchronous:","setNetworkAccessAllowed:"};for(unsigned i=0;i<COUNT(setters);++i)if(!strcmp(selector,setters[i]))return (Method)photo_set_option;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_PHFetchOptions")) && !class_receiver){if(!strcmp(selector,"fetchLimit"))return (Method)photo_fetch_limit;if(!strcmp(selector,"setFetchLimit:"))return (Method)photo_set_fetch_limit;if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)photo_fetch_options_copy;}
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_PHAsset"))){
        if(class_receiver){if(!strcmp(selector,"fetchAssetsWithALAssetURLs:options:") || !strcmp(selector,"fetchAssetsWithLocalIdentifiers:options:"))return (Method)photo_fetch;if(!strcmp(selector,"fetchAssetsWithOptions:"))return (Method)photo_fetch_all;if(!strcmp(selector,"fetchAssetsWithMediaType:options:"))return (Method)photo_fetch_media;return NULL;}
        if(!strcmp(selector,"duration"))return (Method)photo_duration;if(!strcmp(selector,"isEqual:"))return (Method)photo_asset_equal;if(!strcmp(selector,"hash"))return (Method)photo_asset_hash;
        const char *properties[]={"localIdentifier","pixelWidth","pixelHeight","mediaType","creationDate","modificationDate","isFavorite"};for(unsigned i=0;i<COUNT(properties);++i)if(!strcmp(selector,properties[i]))return (Method)photo_asset_property;
    }
    if(class_inherits(cls,external_class("_OBJC_CLASS_$_PHFetchResult")) && !class_receiver){
        if(!strcmp(selector,"count"))return (Method)photo_fetch_count;if(!strcmp(selector,"objectAtIndex:") || !strcmp(selector,"objectAtIndexedSubscript:"))return (Method)array_at;if(!strcmp(selector,"firstObject"))return (Method)photo_fetch_first;if(!strcmp(selector,"lastObject"))return (Method)photo_fetch_last;if(!strcmp(selector,"indexOfObject:"))return (Method)photo_fetch_index;if(!strcmp(selector,"containsObject:"))return (Method)photo_fetch_contains;if(!strcmp(selector,"countOfAssetsWithMediaType:"))return (Method)photo_fetch_media_count;if(!strcmp(selector,"enumerateObjectsUsingBlock:"))return (Method)photo_fetch_enumerate_plain;if(!strcmp(selector,"enumerateObjectsWithOptions:usingBlock:"))return (Method)photo_fetch_enumerate;if(!strcmp(selector,"countByEnumeratingWithState:objects:count:"))return (Method)array_enumerate;if(!strcmp(selector,"copy") || !strcmp(selector,"copyWithZone:"))return (Method)photo_fetch_copy;
    }
    return NULL;
}
static void photo_write_finish(struct PhotoWrite *write,Word error) {
    if (write->target && write->selector) {
        Method callback=find_implementation(write->target,write->selector);
        if (!callback) stop("Missing photo save completion selector",write->selector);
        callback(write->target,write->selector,write->image,error,(Word)write->context,0);
    }
}
static void photo_write_release(struct PhotoWrite *write) {
    metal_object_release(write->image); metal_object_release(write->target); free(write);
}
static Word photo_write_error(long code,Word description) {
    struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
    map_set(info,"Photo save",description,copy_string("NSLocalizedDescription"));
    return error_with_domain(external_class("_OBJC_CLASS_$_NSError"),"Photo save",copy_string("NSCocoaErrorDomain"),code,(Word)info);
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completePhotoWrite)(JNIEnv *env,jobject instance,jlong token,jlong code,jstring description) {
    struct PhotoWrite *write=(void *)(Word)token;
    if (!write) return NULL;
    may_unwind=1; snprintf(stage,sizeof(stage),"Original UIImage photo-save completion");
    int failed=setjmp(boundary);
    if (!failed) {
        Word error=code ? photo_write_error((long)code,description ? string_from_java(env,description,"Photo save") : copy_string("Photo could not be saved")) : 0;
        photo_write_finish(write,error);
    }
    photo_write_release(write); may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static void image_write_album(struct HostObject *image,Word target,const char *selector,void *context) {
    struct PhotoWrite *write=calloc(1,sizeof(*write));
    if (!write) stop("Allocation failed","UIImageWriteToSavedPhotosAlbum");
    *write=(struct PhotoWrite){metal_object_retain((Word)image),metal_object_retain(target),selector,context};
    Word representation=image_png(image);
    struct HostObject *data=(void *)representation;
    JNIEnv *env=attached_environment("UIImageWriteToSavedPhotosAlbum");
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"saveGuestPhoto","(J[B)V");
    jbyteArray bytes=NULL;
    if (data && data->content[0]<=INT32_MAX) {
        bytes=(*env)->NewByteArray(env,(jsize)data->content[0]);
        if (bytes) (*env)->SetByteArrayRegion(env,bytes,0,(jsize)data->content[0],(const jbyte *)data_buffer(data));
    }
    if (!(*env)->ExceptionCheck(env)) (*env)->CallVoidMethod(env,activity,method,(jlong)write,bytes);
    if (bytes) (*env)->DeleteLocalRef(env,bytes);
    (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); photo_write_release(write);
        stop("Android photo-save dispatch failed","UIImageWriteToSavedPhotosAlbum");
    }
}
