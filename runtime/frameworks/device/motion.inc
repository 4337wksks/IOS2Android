/* Availability reflects Android sensors. A missing device-motion stream is nil;
 * query/interval compatibility does not manufacture accelerometer samples. */
static unsigned motion_sensor_mask(void) {
    JNIEnv *env=attached_environment("CoreMotion sensor query");
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID query=(*env)->GetMethodID(env,type,"motionSensorMask","()I");
    jint result=(*env)->CallIntMethod(env,activity,query);
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); stop("Android sensor query failed","CMMotionManager"); }
    return (unsigned)result;
}
static Word motion_available(Word object,const char *selector) {
    unsigned bit=strstr(selector,"DeviceMotion") ? 8 : strstr(selector,"Accelerometer") ? 1 : strstr(selector,"Gyro") ? 2 : 4;
    return (motion_sensor_mask()&bit)!=0;
}
static unsigned motion_interval_index(const char *selector) {
    return strstr(selector,"DeviceMotion") || strstr(selector,"deviceMotion") ? 0 : strstr(selector,"Accelerometer") || strstr(selector,"accelerometer") ? 1 : strstr(selector,"Gyro") || strstr(selector,"gyro") ? 2 : 3;
}
static void motion_set_interval(struct HostObject *object,const char *selector,double interval) {
    if (!isfinite(interval) || interval<0) stop("Invalid motion update interval",selector);
    memcpy(&object->content[motion_interval_index(selector)],&interval,sizeof(interval));
}
static double motion_interval(struct HostObject *object,const char *selector) {
    double interval; memcpy(&interval,&object->content[motion_interval_index(selector)],sizeof(interval)); return interval;
}
static Word motion_inactive(void) { return 0; }
static Method motion_stream_method(Word,const char *,const char *,int);
static Method motion_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    Method stream=motion_stream_method(object,selector,receiver,class_receiver); if (stream) return stream;
    if (!receiver || class_receiver || strcmp(receiver,"_OBJC_CLASS_$_CMMotionManager")) return NULL;
    if (!strcmp(selector,"isDeviceMotionAvailable") || !strcmp(selector,"isAccelerometerAvailable") || !strcmp(selector,"isGyroAvailable") || !strcmp(selector,"isMagnetometerAvailable")) return (Method)motion_available;
    if (!strcmp(selector,"setDeviceMotionUpdateInterval:") || !strcmp(selector,"setAccelerometerUpdateInterval:") || !strcmp(selector,"setGyroUpdateInterval:") || !strcmp(selector,"setMagnetometerUpdateInterval:")) return (Method)motion_set_interval;
    if (!strcmp(selector,"deviceMotionUpdateInterval") || !strcmp(selector,"accelerometerUpdateInterval") || !strcmp(selector,"gyroUpdateInterval") || !strcmp(selector,"magnetometerUpdateInterval")) return (Method)motion_interval;
    if (!strcmp(selector,"isDeviceMotionActive") || !strcmp(selector,"isAccelerometerActive") || !strcmp(selector,"isGyroActive") || !strcmp(selector,"isMagnetometerActive") || !strcmp(selector,"deviceMotion") || !strcmp(selector,"accelerometerData") || !strcmp(selector,"gyroData") || !strcmp(selector,"magnetometerData")) return (Method)motion_inactive;
    return NULL;
}
struct MotionVector { double x,y,z; };
struct MotionQuaternion { double x,y,z,w; };
struct MotionAttitude { Word isa; struct MotionQuaternion quaternion; };
struct MotionSample { Word isa; double timestamp; struct MotionVector gravity,acceleration,rate; struct MotionAttitude attitude; };
struct MotionStream { Word object,queue,handler,sample; unsigned generation,active,pending; struct MotionStream *next; };
struct MotionDelivery { Word isa; struct MotionStream *stream; unsigned generation; Word handler,sample,error; };
static struct MotionStream *motion_streams;
static pthread_mutex_t motion_stream_mutex=PTHREAD_MUTEX_INITIALIZER;
static Word unsupported_service_error(const char *,int,const char *);
static struct MotionStream *motion_stream(Word object) {
    for (struct MotionStream *s=motion_streams;s;s=s->next) if (s->object==object) return s;
    struct MotionStream *s=calloc(1,sizeof(*s)); if (!s) stop("Allocation failed","CoreMotion stream");
    s->object=object; s->next=motion_streams; motion_streams=s; return s;
}
static void motion_deliver(Word object,const char *selector,Word argument) {
    struct MotionDelivery *d=(void *)object;
    pthread_mutex_lock(&motion_stream_mutex);
    int current=d->generation==d->stream->generation && (d->stream->active || d->error);
    pthread_mutex_unlock(&motion_stream_mutex);
    if (current && d->handler) ((void (*)(Word,Word,Word))read_word(d->handler+16))(d->handler,d->sample,d->error);
}
static void motion_delivery_cleanup(void *context) {
    struct MotionDelivery *d=context;
    pthread_mutex_lock(&motion_stream_mutex);
    if (d->generation==d->stream->generation) d->stream->pending=0;
    pthread_mutex_unlock(&motion_stream_mutex);
    if (d->handler) _Block_release((void *)d->handler);
    free(d);
}
static void motion_enqueue(struct MotionDelivery *delivery,Word queue) {
    Word operation=host_allocate(external_class("_OBJC_CLASS_$_NSInvocationOperation"));
    invocation_operation(operation,"initWithTarget:selector:object:",(Word)delivery,"performMotionDelivery:",0);
    struct Operation *state=operation_state(operation); state->cleanup=motion_delivery_cleanup; state->cleanup_context=delivery;
    queue_add(queue,"addOperation:",operation);
}
static void motion_stream_stop(Word object,const char *selector) {
    pthread_mutex_lock(&motion_stream_mutex); struct MotionStream *s=motion_stream(object);
    s->active=0; s->pending=0; ++s->generation; Word handler=s->handler; s->handler=0;
    pthread_mutex_unlock(&motion_stream_mutex);
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID stop=(*env)->GetMethodID(env,cls,"stopDeviceMotion","(J)V"); (*env)->CallVoidMethod(env,activity,stop,(jlong)object); (*env)->DeleteLocalRef(env,cls);
    if (handler) _Block_release((void *)handler);
}
static void motion_stream_start(Word object,const char *selector,Word reference,Word queue,Word handler) {
    if (reference!=1 || !queue || !handler) stop("Unsupported device-motion request",selector);
    motion_stream_stop(object,selector);
    double interval=motion_interval((void *)object,"deviceMotionUpdateInterval"); if (!(interval>0)) interval=1.0/60.0;
    pthread_mutex_lock(&motion_stream_mutex); struct MotionStream *s=motion_stream(object);
    s->queue=queue; s->handler=(Word)_Block_copy((void *)handler); s->active=1; unsigned generation=s->generation;
    pthread_mutex_unlock(&motion_stream_mutex);
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID start=(*env)->GetMethodID(env,cls,"startDeviceMotion","(JID)Z");
    int started=(*env)->CallBooleanMethod(env,activity,start,(jlong)object,(jint)generation,(jdouble)interval); (*env)->DeleteLocalRef(env,cls);
    if (!started) {
        struct MotionDelivery *d=calloc(1,sizeof(*d)); if (!d) stop("Allocation failed",selector);
        d->isa=external_class("_OBJC_CLASS_$_MotionSourceDelivery"); d->stream=s; d->generation=generation; d->handler=(Word)_Block_copy((void *)handler);
        d->error=unsupported_service_error("CMErrorDomain",109,"Android device-motion sensors are unavailable");
        pthread_mutex_lock(&motion_stream_mutex); s->active=0; pthread_mutex_unlock(&motion_stream_mutex); motion_enqueue(d,queue);
    }
}
static Word motion_stream_active(Word object) { pthread_mutex_lock(&motion_stream_mutex); Word active=motion_stream(object)->active; pthread_mutex_unlock(&motion_stream_mutex); return active; }
static Word motion_stream_sample(Word object) { pthread_mutex_lock(&motion_stream_mutex); Word sample=motion_stream(object)->sample; pthread_mutex_unlock(&motion_stream_mutex); return sample; }
static struct MotionVector motion_gravity(struct MotionSample *s) { return s->gravity; }
static struct MotionVector motion_acceleration(struct MotionSample *s) { return s->acceleration; }
static struct MotionVector motion_rate(struct MotionSample *s) { return s->rate; }
static double motion_timestamp(struct MotionSample *s) { return s->timestamp; }
static Word motion_attitude(struct MotionSample *s) { return (Word)&s->attitude; }
static struct MotionQuaternion motion_quaternion(struct MotionAttitude *a) { return a->quaternion; }
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_deliverDeviceMotion)(JNIEnv *env,jobject instance,jlong token,jint generation,jdoubleArray values) {
    if (!values || (*env)->GetArrayLength(env,values)!=14) return (*env)->NewStringUTF(env,"Invalid Android motion sample");
    jdouble v[14]; (*env)->GetDoubleArrayRegion(env,values,0,14,v);
    pthread_mutex_lock(&motion_stream_mutex); struct MotionStream *s=motion_stream((Word)token);
    if (!s->active || s->generation!=(unsigned)generation || s->pending) { pthread_mutex_unlock(&motion_stream_mutex); return NULL; }
    struct MotionSample *sample=calloc(1,sizeof(*sample)); struct MotionDelivery *d=calloc(1,sizeof(*d));
    if (!sample || !d) { free(sample); free(d); pthread_mutex_unlock(&motion_stream_mutex); return (*env)->NewStringUTF(env,"CoreMotion allocation failed"); }
    *sample=(struct MotionSample){.isa=external_class("_OBJC_CLASS_$_CMDeviceMotion"),.timestamp=v[0],.gravity={v[5],v[6],v[7]},.acceleration={v[8],v[9],v[10]},.rate={v[11],v[12],v[13]},
        .attitude={external_class("_OBJC_CLASS_$_CMAttitude"),{v[1],v[2],v[3],v[4]}}};
    s->sample=(Word)sample; s->pending=1;
    *d=(struct MotionDelivery){external_class("_OBJC_CLASS_$_MotionSourceDelivery"),s,(unsigned)generation,(Word)_Block_copy((void *)s->handler),(Word)sample,0};
    Word queue=s->queue; pthread_mutex_unlock(&motion_stream_mutex);
    may_unwind=1; snprintf(stage,sizeof(stage),"Android device-motion enqueue"); int failed=setjmp(boundary);
    if (!failed) motion_enqueue(d,queue);
    may_unwind=0; return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Method motion_stream_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || class_receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_CMMotionManager")) {
        if (!strcmp(selector,"startDeviceMotionUpdatesUsingReferenceFrame:toQueue:withHandler:")) return (Method)motion_stream_start;
        if (!strcmp(selector,"stopDeviceMotionUpdates")) return (Method)motion_stream_stop;
        if (!strcmp(selector,"isDeviceMotionActive")) return (Method)motion_stream_active;
        if (!strcmp(selector,"deviceMotion")) return (Method)motion_stream_sample;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_CMDeviceMotion")) {
        if (!strcmp(selector,"gravity")) return (Method)motion_gravity;
        if (!strcmp(selector,"userAcceleration")) return (Method)motion_acceleration;
        if (!strcmp(selector,"rotationRate")) return (Method)motion_rate;
        if (!strcmp(selector,"attitude")) return (Method)motion_attitude;
        if (!strcmp(selector,"timestamp")) return (Method)motion_timestamp;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_CMAttitude") && !strcmp(selector,"quaternion")) return (Method)motion_quaternion;
    if (!strcmp(receiver,"_OBJC_CLASS_$_MotionSourceDelivery") && !strcmp(selector,"performMotionDelivery:")) return (Method)motion_deliver;
    return NULL;
}
