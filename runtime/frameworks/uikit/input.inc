 
static struct GuestRect keyboard_frame;
static int keyboard_frame_known,keyboard_visible;
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_updateGuestKeyboard)(JNIEnv *env,jobject instance,jboolean visible,jint x,jint y,jint width,jint height) {
    if (!started || width<=0 || height<0) return NULL;
    may_unwind=1; snprintf(stage,sizeof(stage),"Android UIKit keyboard notifications");
    int failed=setjmp(boundary);
    if (!failed) {
        double scale=screen_scale();
        struct GuestRect next={x/scale,y/scale,width/scale,height/scale};
        struct GuestRect previous=keyboard_frame_known ? keyboard_frame : (struct GuestRect){next.x,next.y+next.height,next.width,0};
        int shown=visible!=0,transition=shown!=keyboard_visible;
        if (!keyboard_frame_known || transition || memcmp(&next,&keyboard_frame,sizeof(next))) {
            keyboard_frame=next; keyboard_frame_known=1;
            if (transition || shown) {
                struct GuestMap *info=(void *)empty_map(external_class("_OBJC_CLASS_$_NSDictionary"));
                Word cls=external_class("_OBJC_CLASS_$_NSValue");
                map_set(info,"UIKit keyboard",value_rect(cls,"valueWithCGRect:",previous),copy_string("UIKeyboardFrameBeginUserInfoKey"));
                map_set(info,"UIKit keyboard",value_rect(cls,"valueWithCGRect:",next),copy_string("UIKeyboardFrameEndUserInfoKey"));
                map_set(info,"UIKit keyboard",real_number_factory(external_class("_OBJC_CLASS_$_NSNumber"),"keyboard duration",0),copy_string("UIKeyboardAnimationDurationUserInfoKey"));
                map_set(info,"UIKit keyboard",number_integer(0),copy_string("UIKeyboardAnimationCurveUserInfoKey"));
                map_set(info,"UIKit keyboard",number_bool(external_class("_OBJC_CLASS_$_NSNumber"),"keyboard local",1),copy_string("UIKeyboardIsLocalUserInfoKey"));
                keyboard_visible=shown;
                platform_notification("UIKeyboardWillChangeFrameNotification",0,(Word)info);
                if (transition) platform_notification(shown ? "UIKeyboardWillShowNotification" : "UIKeyboardWillHideNotification",0,(Word)info);
                platform_notification("UIKeyboardDidChangeFrameNotification",0,(Word)info);
                if (transition) platform_notification(shown ? "UIKeyboardDidShowNotification" : "UIKeyboardDidHideNotification",0,(Word)info);
#if I2A_DEVELOPMENT
                LOG("[DIAG-ime-state] visible=%d transition=%d frame=%g,%g,%g,%g",shown,transition,next.x,next.y,next.width,next.height);
#endif
            }
        }
    }
    may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static int text_delegate_bool(Word field,const char *selector) {
    Word delegate=text_field_state(field)->properties[12];
    Method method=delegate ? find_implementation(delegate,selector) : NULL;
    return !method || (method(delegate,selector,field,0,0,0)&255);
}
static void text_delegate_notify(Word field,const char *selector) {
    Word delegate=text_field_state(field)->properties[12];
    Method method=delegate ? find_implementation(delegate,selector) : NULL;
    if (method) method(delegate,selector,field,0,0,0);
}
static void text_field_sync_input(Word object,int show_keyboard) {
    struct TextFieldState *field=text_field_state(object);
    JNIEnv *env=attached_environment("UITextField input");
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"syncGuestInput","(JLjava/lang/String;IIIZZ)V");
    jstring text=http_java_string(env,field->properties[0]);
    (*env)->CallVoidMethod(env,activity,method,(jlong)object,text,(jint)field->properties[13],(jint)field->properties[6],(jint)field->properties[9],field->properties[15] ? JNI_TRUE : JNI_FALSE,show_keyboard ? JNI_TRUE : JNI_FALSE);
    (*env)->DeleteLocalRef(env,text); (*env)->DeleteLocalRef(env,cls);
}
static void text_field_sync(Word object) { text_field_sync_input(object,0); }
static Word text_resign(Word object) {
    if (text_first_responder!=object) return 1;
    if (!text_delegate_bool(object,"textFieldShouldEndEditing:")) return 0;
    text_first_responder=0;
    JNIEnv *env=attached_environment("UITextField resign"); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"hideGuestInput","(J)V");
    (*env)->CallVoidMethod(env,activity,method,(jlong)object); (*env)->DeleteLocalRef(env,cls);
    text_delegate_notify(object,"textFieldDidEndEditing:");
    return 1;
}
static Word text_become(Word object) {
    if (text_first_responder==object) { text_field_sync_input(object,1); return 1; }
    if (!text_field_state(object)->properties[16] || !text_delegate_bool(object,"textFieldShouldBeginEditing:")) return 0;
    if (text_first_responder && !text_resign(text_first_responder)) return 0;
    text_first_responder=object; text_field_sync_input(object,1);
    text_delegate_notify(object,"textFieldDidBeginEditing:"); return 1;
}
static Word text_is_first(Word object) { return object==text_first_responder; }
static Word text_can_become(Word object) { return text_field_state(object)->properties[16]!=0; }
static Word text_has_text(Word object) { return string_length(text_field_state(object)->properties[0],"length")!=0; }
static Word input_yes(void) { return 1; }
static Word input_no(void) { return 0; }
static Word input_end_editing(Word object,const char *selector,unsigned char force) {
    if (!text_first_responder || (object!=(Word)&main_window && !view_descendant(text_first_responder,selector,object))) return 1;
    return text_resign(text_first_responder);
}
static Word input_replace(Word object,Word value) {
    struct TextFieldState *field=text_field_state(object);
    struct TextView old=text_view(field->properties[0],"UITextField edit"),next=text_view(value,"UITextField edit");
    size_t prefix=0,suffix=0;
    while (prefix<old.length && prefix<next.length && old.units[prefix]==next.units[prefix]) ++prefix;
    while (suffix<old.length-prefix && suffix<next.length-prefix && old.units[old.length-1-suffix]==next.units[next.length-1-suffix]) ++suffix;
    Word replacement=text_from_units(external_class("_OBJC_CLASS_$_NSString"),next.units+prefix,next.length-prefix-suffix);
    struct GuestRange range={prefix,old.length-prefix-suffix}; free(old.owned); free(next.owned);
    Word delegate=field->properties[12];
    const char *selector="textField:shouldChangeCharactersInRange:replacementString:";
    Method method=delegate ? find_implementation(delegate,selector) : NULL;
    if (method && !(((Word (*)(Word,const char *,Word,struct GuestRange,Word))method)(delegate,selector,object,range,replacement)&255)) return field->properties[0];
    field->properties[0]=text_copy(value,"copy");
    return field->properties[0];
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_editGuestInput)(JNIEnv *env,jobject activity,jlong token,jstring value,jboolean submit) {
    if ((Word)token!=text_first_responder) return NULL;
    may_unwind=1; int failed=setjmp(boundary); Word accepted=0;
    if (!failed) {
        if (submit) { text_delegate_bool(token,"textFieldShouldReturn:"); }
        else {
            const jchar *units=(*env)->GetStringChars(env,value,NULL);
            Word input=text_from_units(external_class("_OBJC_CLASS_$_NSString"),units,(*env)->GetStringLength(env,value));
            (*env)->ReleaseStringChars(env,value,units);
            accepted=input_replace(token,input);
        }
    }
    may_unwind=0;
    if (failed) {
        jclass cls=(*env)->GetObjectClass(env,activity); jmethodID report=(*env)->GetMethodID(env,cls,"guestFailure","(Ljava/lang/String;)V");
        jstring error=(*env)->NewStringUTF(env,status); (*env)->CallVoidMethod(env,activity,report,error);
        (*env)->DeleteLocalRef(env,error); (*env)->DeleteLocalRef(env,cls); return NULL;
    }
    return accepted ? http_java_string(env,accepted) : NULL;
}
static Method ui_input_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || class_receiver) return NULL;
    const char *base=external_base(read_word(object));
    int field=base && !strcmp(base,"_OBJC_CLASS_$_UITextField");
    int responder=field || (base && (!strcmp(base,"_OBJC_CLASS_$_UIView") || !strcmp(base,"_OBJC_CLASS_$_UIWindow") || !strcmp(base,"_OBJC_CLASS_$_UIViewController") || !strcmp(base,"_OBJC_CLASS_$_UILabel") || !strcmp(base,"_OBJC_CLASS_$_UIImageView")));
    if (!responder) return NULL;
    if (!strcmp(selector,"resignFirstResponder")) return (Method)text_resign;
    if (!strcmp(selector,"isFirstResponder")) return (Method)text_is_first;
    if (!strcmp(selector,"canResignFirstResponder")) return (Method)input_yes;
    if (!strcmp(selector,"canBecomeFirstResponder")) return field ? (Method)text_can_become : (Method)input_no;
    if (!strcmp(selector,"becomeFirstResponder")) return field ? (Method)text_become : (Method)input_no;
    if (!strcmp(selector,"endEditing:")) return (Method)input_end_editing;
    if (field && !strcmp(selector,"hasText")) return (Method)text_has_text;
    return NULL;
}
