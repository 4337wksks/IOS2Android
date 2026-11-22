 

struct Certificate { Word tag; jobject certificate; Word data; };
struct Trust { Word tag; jobjectArray chain; Word anchors; int anchors_only; struct Certificate **certificates; unsigned managed_references; };

static pthread_key_t attached_java_key;
static pthread_once_t attached_java_once=PTHREAD_ONCE_INIT;
static void detach_java_thread(void *token) { if (token && java_vm) (*java_vm)->DetachCurrentThread(java_vm); }
static void attached_java_key_init(void) { pthread_key_create(&attached_java_key,detach_java_thread); }
static JNIEnv *attached_environment(const char *operation) {
    JNIEnv *env=NULL;
    jint result=(*java_vm)->GetEnv(java_vm,(void **)&env,JNI_VERSION_1_6);
    if (result==JNI_EDETACHED) {
        if ((*java_vm)->AttachCurrentThread(java_vm,&env,NULL)!=JNI_OK) stop("JNI thread attachment failed",operation);
        pthread_once(&attached_java_once,attached_java_key_init);
        pthread_setspecific(attached_java_key,(void *)1);
    } else if (result!=JNI_OK) stop("JNI environment unavailable",operation);
    return env;
}

static struct Certificate *certificate_create(Word allocator,struct HostObject *data) {
    if (allocator || !data || data->content[0]>INT32_MAX) return NULL;
    JNIEnv *env=attached_environment("SecCertificateCreateWithData");
    jbyteArray bytes=(*env)->NewByteArray(env,data->content[0]);
    if (!bytes) stop("Allocation failed","Certificate bytes");
    (*env)->SetByteArrayRegion(env,bytes,0,data->content[0],(const jbyte *)data_buffer(data));
    jobject certificate=(*env)->CallObjectMethod(env,activity,certificate_method,bytes);
    (*env)->DeleteLocalRef(env,bytes);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return NULL;
    }
    if (!certificate) return NULL;
    struct Certificate *result=calloc(1,sizeof(*result));
    if (!result) stop("Allocation failed","Certificate");
    result->tag=(Word)&certificate_tag;
    result->certificate=(*env)->NewGlobalRef(env,certificate);
    result->data=(Word)data;
    (*env)->DeleteLocalRef(env,certificate);
    LOG("X.509 certificate parsed (%lu bytes)",data->content[0]);
    return result;
}

static int trust_anchors(struct Trust *trust,struct Sequence *certificates) {
    if (!trust || trust->tag!=(Word)&trust_tag || !certificates) return -50;
    for (size_t index=0;index<certificates->count;++index)
        if (!certificates->items[index] || read_word(certificates->items[index])!=(Word)&certificate_tag) return -50;
    trust->anchors=(Word)certificates;
    trust->anchors_only=1;
    return 0;
}

static int trust_only_anchors(struct Trust *trust,unsigned char only) {
    if (!trust || trust->tag!=(Word)&trust_tag) return -50;
    trust->anchors_only=only!=0;
    return 0;
}

static Word trust_certificate_count(struct Trust *trust) {
    if (!trust || trust->tag!=(Word)&trust_tag || !trust->chain) return 0;
    JNIEnv *env=attached_environment("SecTrustGetCertificateCount");
    return (Word)(*env)->GetArrayLength(env,trust->chain);
}
static struct Certificate *trust_certificate_at(struct Trust *trust,intptr_t index) {
    Word count=trust_certificate_count(trust);
    if (index<0 || (Word)index>=count) return NULL;
    if (!trust->certificates) {
        trust->certificates=calloc(count,sizeof(*trust->certificates));
        if (!trust->certificates) stop("Allocation failed","Trust certificates");
    }
    if (trust->certificates[index]) return trust->certificates[index];
    JNIEnv *env=attached_environment("SecTrustGetCertificateAtIndex");
    jobject certificate=(*env)->GetObjectArrayElement(env,trust->chain,(jsize)index);
    jclass type=(*env)->GetObjectClass(env,certificate);
    jmethodID encode=(*env)->GetMethodID(env,type,"getEncoded","()[B");
    jbyteArray bytes=(*env)->CallObjectMethod(env,certificate,encode);
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); (*env)->DeleteLocalRef(env,certificate); return NULL; }
    struct Certificate *result=calloc(1,sizeof(*result));
    if (!result || !bytes) stop("Allocation failed","Trust certificate");
    jbyte *raw=(*env)->GetByteArrayElements(env,bytes,NULL);
    result->tag=(Word)&certificate_tag; result->certificate=(*env)->NewGlobalRef(env,certificate);
    result->data=data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"Certificate DER",raw,(*env)->GetArrayLength(env,bytes));
    (*env)->ReleaseByteArrayElements(env,bytes,raw,JNI_ABORT); (*env)->DeleteLocalRef(env,bytes); (*env)->DeleteLocalRef(env,certificate);
    trust->certificates[index]=result;
    return result;
}
static Word certificate_copy_data(struct Certificate *certificate) { return certificate && certificate->tag==(Word)&certificate_tag ? certificate->data : 0; }

static int trust_evaluate(struct Trust *trust,uint32_t *result) {
    if (!trust || trust->tag!=(Word)&trust_tag || !result || !trust->chain) return -50;
    JNIEnv *env=attached_environment("SecTrustEvaluate");
    jclass type=(*env)->FindClass(env,"java/security/cert/X509Certificate");
    struct Sequence *certificates=(void *)trust->anchors;
    jobjectArray anchors=(*env)->NewObjectArray(env,certificates ? certificates->count : 0,type,NULL);
    (*env)->DeleteLocalRef(env,type);
    if (!anchors) stop("Allocation failed","Trust anchors");
    if (certificates) {
        for (size_t index=0;index<certificates->count;++index) {
            struct Certificate *certificate=(void *)certificates->items[index];
            (*env)->SetObjectArrayElement(env,anchors,index,certificate->certificate);
        }
    }
    jboolean accepted=(*env)->CallBooleanMethod(env,activity,trust_method,trust->chain,anchors,trust->anchors_only);
    (*env)->DeleteLocalRef(env,anchors);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        *result=6;  
        return -2070;
    }
    *result=accepted ? 4 : 5;  
    return 0;
}

/* Existing Foundation trust handles keep their current ownership. TLS-created
 * handles have real CF copy/retain/release ownership. */
static void trust_retain(struct Trust *trust) {
    if (trust->managed_references) __atomic_add_fetch(&trust->managed_references,1,__ATOMIC_RELAXED);
}
static void trust_release(Word object) {
    struct Trust *trust=(void *)object;
    if (!trust->managed_references || __atomic_sub_fetch(&trust->managed_references,1,__ATOMIC_ACQ_REL)) return;
    JNIEnv *env=attached_environment("TLS peer trust release");
    if (trust->certificates) {
        size_t count=trust_certificate_count(trust);
        for (size_t i=0;i<count;++i) if (trust->certificates[i]) {
            (*env)->DeleteGlobalRef(env,trust->certificates[i]->certificate);
            free(trust->certificates[i]);
        }
        free(trust->certificates);
    }
    if (trust->chain) (*env)->DeleteGlobalRef(env,trust->chain);
    free(trust);
}
