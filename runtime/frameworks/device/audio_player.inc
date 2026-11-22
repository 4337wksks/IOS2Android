 
static Word audio_player_init(Word object,const char *selector,struct HostObject *url,Word *error) {
    if (error) *error=0;
    if (!url || !url->content[1]) return 0;
    char *path=native_path(string_bytes(url_path(url),selector));
    if (!path) { file_failure(error,"AVAudioPlayer",errno); return 0; }
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID prepare=(*env)->GetMethodID(env,type,"prepareAudio","(JLjava/lang/String;)Z");
    jstring filename=(*env)->NewStringUTF(env,path);
    int prepared=(*env)->CallBooleanMethod(env,activity,prepare,(jlong)object,filename);
    (*env)->DeleteLocalRef(env,filename); free(path);
    if (!prepared && error) {
        jmethodID last_error=(*env)->GetMethodID(env,type,"audioError","()Ljava/lang/String;");
        jstring message=(*env)->CallObjectMethod(env,activity,last_error);
        struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
        map_set(info,selector,string_from_java(env,message,selector),copy_string("NSLocalizedDescription"));
        *error=error_with_domain(external_class("_OBJC_CLASS_$_NSError"),selector,copy_string("NSOSStatusErrorDomain"),-50,(Word)info);
        (*env)->DeleteLocalRef(env,message);
    }
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android audio bridge exception",selector); }
    if (prepared) ((struct HostObject *)object)->content[0]=(Word)url;
    return prepared ? object : 0;
}
static Word audio_player_play(Word object) {
    JNIEnv *env=attached_environment("AVAudioPlayer play");
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID play=(*env)->GetMethodID(env,type,"playAudio","(J)Z");
    Word result=(*env)->CallBooleanMethod(env,activity,play,(jlong)object);
    (*env)->DeleteLocalRef(env,type);
    return result;
}
static void audio_player_action(Word object,const char *selector) {
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,!strcmp(selector,"pause") ? "pauseAudio" : "stopAudio","(J)V");
    (*env)->CallVoidMethod(env,activity,method,(jlong)object); (*env)->DeleteLocalRef(env,type);
}
static double audio_player_property(Word object,const char *selector) {
    int property=!strcmp(selector,"duration") ? 0 : !strcmp(selector,"currentTime") ? 1 : !strcmp(selector,"isPlaying") ? 2 : !strcmp(selector,"volume") ? 3 : !strcmp(selector,"numberOfChannels") ? 4 : 5;
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"audioProperty","(JI)D");
    double value=(*env)->CallDoubleMethod(env,activity,method,(jlong)object,(jint)property);
    (*env)->DeleteLocalRef(env,type);
    return value;
}
static Word audio_player_integer(Word object,const char *selector) { return (Word)audio_player_property(object,selector); }
static float audio_player_volume(Word object,const char *selector) { return (float)audio_player_property(object,selector); }
static void audio_player_set_time(Word object,const char *selector,double value) {
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"audioSeek","(JD)Z");
    int success=(*env)->CallBooleanMethod(env,activity,method,(jlong)object,(jdouble)value); (*env)->DeleteLocalRef(env,cls);
    if (!success) stop("Invalid audio playback position",selector);
}
static void audio_player_set_loops(Word object,const char *selector,intptr_t value) {
    if (value < -1 || value>INT_MAX) stop("Invalid audio loop count",selector);
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"audioLoops","(JI)Z");
    int success=(*env)->CallBooleanMethod(env,activity,method,(jlong)object,(jint)value); (*env)->DeleteLocalRef(env,cls);
    if (!success) stop("Unsupported audio loop count",selector);
}
static void audio_player_set_volume(Word object,const char *selector,float value) {
    JNIEnv *env=attached_environment(selector);
    jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,type,"audioVolume","(JF)V");
    (*env)->CallVoidMethod(env,activity,method,(jlong)object,(jfloat)value); (*env)->DeleteLocalRef(env,type);
}
static void audio_player_set_delegate(struct HostObject *player,const char *selector,Word delegate) { player->content[1]=delegate; }
static Word audio_player_delegate(struct HostObject *player) { return player->content[1]; }
static Word audio_player_prepared(struct HostObject *player) { return player->content[0]!=0; }
JNIEXPORT jstring JNICALL HOST_JNI(AudioOutput_finished)(JNIEnv *env,jclass cls,jlong token,jboolean success) {
    if (token<=-0x10000 && token>=-(jlong)INT32_MAX) return NULL;
    may_unwind=1; snprintf(stage,sizeof(stage),"AVAudioPlayer completion");
    int failed=setjmp(boundary);
    if (!failed) {
        struct HostObject *player=(void *)token;
        Method callback=find_implementation(player->content[1],"audioPlayerDidFinishPlaying:successfully:");
        if (callback) callback(player->content[1],"audioPlayerDidFinishPlaying:successfully:",(Word)player,success,0,0);
    }
    may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Method audio_player_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || class_receiver || strcmp(receiver,"_OBJC_CLASS_$_AVAudioPlayer")) return NULL;
    if (!strcmp(selector,"initWithContentsOfURL:error:")) return (Method)audio_player_init;
    if (!strcmp(selector,"prepareToPlay")) return (Method)audio_player_prepared;
    if (!strcmp(selector,"play")) return (Method)audio_player_play;
    if (!strcmp(selector,"pause") || !strcmp(selector,"stop")) return (Method)audio_player_action;
    if (!strcmp(selector,"duration") || !strcmp(selector,"currentTime")) return (Method)audio_player_property;
    if (!strcmp(selector,"setCurrentTime:")) return (Method)audio_player_set_time;
    if (!strcmp(selector,"setNumberOfLoops:")) return (Method)audio_player_set_loops;
    if (!strcmp(selector,"isPlaying") || !strcmp(selector,"numberOfChannels") || !strcmp(selector,"numberOfLoops")) return (Method)audio_player_integer;
    if (!strcmp(selector,"volume")) return (Method)audio_player_volume;
    if (!strcmp(selector,"setVolume:")) return (Method)audio_player_set_volume;
    if (!strcmp(selector,"delegate")) return (Method)audio_player_delegate;
    if (!strcmp(selector,"setDelegate:")) return (Method)audio_player_set_delegate;
    return NULL;
}

static Word system_sound_next=0x10000;
static int system_sound_create(Word url,uint32_t *output) {
    if (!output || !url) return -50;
    *output=0;
    char *path=native_path(string_bytes(url_path((void *)url),"AudioServicesCreateSystemSoundID"));
    if (!path) return -1500;
    Word id=__atomic_fetch_add(&system_sound_next,1,__ATOMIC_RELAXED);
    if (id>INT32_MAX) { free(path); return -1500; }
    JNIEnv *env=attached_environment("AudioServicesCreateSystemSoundID"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID prepare=(*env)->GetMethodID(env,type,"prepareAudio","(JLjava/lang/String;)Z");
    jstring filename=(*env)->NewStringUTF(env,path); free(path);
    int prepared=filename && prepare && (*env)->CallBooleanMethod(env,activity,prepare,-(jlong)id,filename);
    if (filename) (*env)->DeleteLocalRef(env,filename); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); return -1500; }
    if (!prepared) return -1500;
    *output=(uint32_t)id; return 0;
}
static int system_sound_dispose(uint32_t id) {
    JNIEnv *env=attached_environment("AudioServicesDisposeSystemSoundID"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID dispose=(*env)->GetMethodID(env,type,"disposeAudio","(J)Z");
    int disposed=dispose && (*env)->CallBooleanMethod(env,activity,dispose,-(jlong)id);
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); return -1500; }
    return disposed ? 0 : -1500;
}
static void system_sound_play(uint32_t id) {
    JNIEnv *env=attached_environment("AudioServicesPlaySystemSound"); jclass type=(*env)->GetObjectClass(env,activity);
    if (id==4095) {
        jmethodID vibrate=(*env)->GetMethodID(env,type,"systemSoundFeedback","()V");
        if (vibrate) (*env)->CallVoidMethod(env,activity,vibrate);
    } else {
        jmethodID play=(*env)->GetMethodID(env,type,"playAudio","(J)Z");
        if (play && !(*env)->CallBooleanMethod(env,activity,play,-(jlong)id)) LOG("System sound unavailable: %u",id);
    }
    (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
}
static Word resolve_audio_services_import(const char *name) {
    if (!strcmp(name,"_AudioServicesCreateSystemSoundID")) return (Word)system_sound_create;
    if (!strcmp(name,"_AudioServicesDisposeSystemSoundID")) return (Word)system_sound_dispose;
    if (!strcmp(name,"_AudioServicesPlaySystemSound")) return (Word)system_sound_play;
    return 0;
}
