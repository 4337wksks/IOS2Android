 
struct UIKitLegacyAlert;
struct UIKitAlert { Word isa,title,message,actions,presenter,present_completion,attributed_title,attributed_message,controller,fields; struct UIKitLegacyAlert *legacy; unsigned style,presented,submitting,external_modal,animated; struct I2AModal custom; };
struct UIKitLegacyAlert { Word isa,delegate; struct UIKitAlert *alert; intptr_t cancel,destructive,dismiss_index; unsigned sheet,dismissing; };
static struct UIKitAlert *storyboard_capability_alert(Word);
static void storyboard_capability_close(Word);
struct UIKitAlertAction { Word isa,title,handler; unsigned style,enabled; struct UIKitAlert *alert; };
static void ui_alert_present(struct UIKitAlert *,const char *);
static Word ui_alert_initialize(Word object,const char *selector) {
    struct UIKitAlert *alert=(void *)object;
    if (!alert->actions) alert->actions=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    return object;
}
static Word ui_alert_create(Word cls,const char *selector,Word title,Word message,Word style) {
    if (style>1) stop("Invalid alert style",selector);
    struct UIKitAlert *alert=(in_image(cls) || dynamic_class(cls)) ? (void *)guest_object(cls) : calloc(1,sizeof(*alert)); if (!alert) stop("Allocation failed",selector);
    alert->isa=cls; alert->title=title ? text_copy(title,"copy") : 0; alert->message=message ? text_copy(message,"copy") : 0;
    alert->style=style; alert->actions=sequence_empty(external_class("_OBJC_CLASS_$_NSArray")); return (Word)alert;
}
static Word ui_alert_action(Word cls,const char *selector,Word title,Word style,Word block) {
    if (style>2) stop("Invalid alert action style",selector);
    struct UIKitAlertAction *action=(in_image(cls) || dynamic_class(cls)) ? (void *)guest_object(cls) : calloc(1,sizeof(*action)); if (!action) stop("Allocation failed",selector);
    *action=(struct UIKitAlertAction){.isa=cls,.title=title ? text_copy(title,"copy") : 0,.handler=block ? (Word)_Block_copy((void *)block) : 0,.style=(unsigned)style,.enabled=1}; return (Word)action;
}
static void ui_alert_add(struct UIKitAlert *alert,const char *selector,struct UIKitAlertAction *action) {
    if (!action || !class_inherits(action->isa,external_class("_OBJC_CLASS_$_UIAlertAction")) || alert->presented) stop("Invalid alert action",selector);
    sequence_add((void *)alert->actions,selector,(Word)action);
    action->alert=alert;
}
static Word ui_alert_title(struct UIKitAlert *alert) { return alert->title; }
static Word ui_alert_message(struct UIKitAlert *alert) { return alert->message; }
static Word ui_alert_actions(struct UIKitAlert *alert) { return alert->actions; }
static void ui_alert_add_field(struct UIKitAlert *alert,const char *selector,Word block) {
    if (alert->style!=1 || alert->presented) stop("Invalid alert text field",selector);
    if (!alert->fields) alert->fields=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    Word field=host_allocate(external_class("_OBJC_CLASS_$_UITextField")); text_field_state(field);
    sequence_add((void *)alert->fields,selector,field);
    if (block) ((void (*)(Word,Word))read_word(block+16))(block,field);
}
static Word ui_alert_fields(struct UIKitAlert *alert) { return alert->fields; }
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_editAlertField)(JNIEnv *env,jobject instance,jlong alert_token,jint index,jstring value) {
    struct UIKitAlert *alert=(void *)(Word)alert_token;
    may_unwind=1; snprintf(stage,sizeof(stage),"Alert text input"); int failed=setjmp(boundary); Word accepted=0;
    if (!failed) {
        struct Sequence *fields=(void *)alert->fields;
        if (!alert->presented || !fields || index<0 || (size_t)index>=fields->count) stop("Invalid alert field", "UITextField");
        accepted=input_replace(fields->items[index],string_from_java(env,value,"UIAlertController input"));
    }
    may_unwind=0;
    if (failed) {
        jclass cls=(*env)->GetObjectClass(env,instance); jmethodID report=(*env)->GetMethodID(env,cls,"guestFailure","(Ljava/lang/String;)V");
        jstring error=(*env)->NewStringUTF(env,status); (*env)->CallVoidMethod(env,instance,report,error);
        (*env)->DeleteLocalRef(env,error); (*env)->DeleteLocalRef(env,cls); return NULL;
    }
    return accepted ? http_java_string(env,accepted) : NULL;
}
static Word ui_alert_style(struct UIKitAlert *alert) { return alert->style; }
static Word ui_action_title(struct UIKitAlertAction *action) { return action->title; }
static Word ui_action_style(struct UIKitAlertAction *action) { return action->style; }
static Word ui_action_enabled(struct UIKitAlertAction *action) { return action->enabled; }
static void ui_action_set_enabled(struct UIKitAlertAction *action,const char *selector,Word enabled) {
    action->enabled=enabled!=0;
    struct UIKitAlert *alert=action->alert;
    if (!alert || !alert->presented) return;
    struct Sequence *actions=(void *)alert->actions;
    for (size_t i=0;i<actions->count;++i) if (actions->items[i]==(Word)action) {
        JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
        jmethodID method=(*env)->GetMethodID(env,cls,"setGuestAlertActionEnabled","(JIZ)V");
        (*env)->CallVoidMethod(env,activity,method,(jlong)alert,(jint)i,(jboolean)action->enabled); (*env)->DeleteLocalRef(env,cls);
        break;
    }
}
static void ui_alert_set_title(struct UIKitAlert *alert,const char *selector,Word value) { alert->title=value ? text_copy(value,"copy") : 0; }
static void ui_alert_set_message(struct UIKitAlert *alert,const char *selector,Word value) { alert->message=value ? text_copy(value,"copy") : 0; }
static void ui_alert_value(struct UIKitAlert *alert,const char *selector,Word value,Word key) {
    const char *name=string_bytes(key,selector);
    if (!strcmp(name,"attributedTitle")) { alert->attributed_title=value; alert->title=value ? attributed_string(value) : 0; }
    else if (!strcmp(name,"attributedMessage")) { alert->attributed_message=value; alert->message=value ? attributed_string(value) : 0; }
    else stop("Unsupported alert key",name);
}
static struct UIKitAlert *extension_modal(Word controller,const char *selector) {
    struct I2AModal description={0};
    if (!runtime_extension.describe_modal || !runtime_extension.describe_modal(controller,&description)) return NULL;
    if (!description.count || description.count>COUNT(description.actions) || !runtime_extension.complete_modal)
        stop("Invalid external modal description",selector);
    struct UIKitAlert *alert=(void *)ui_alert_create(external_class("_OBJC_CLASS_$_UIAlertController"),selector,description.title,description.message,description.style);
    for (unsigned i=0;i<description.count;++i) {
        Word action=ui_alert_action(external_class("_OBJC_CLASS_$_UIAlertAction"),selector,description.actions[i].title,description.actions[i].style,0);
        ui_alert_add(alert,selector,(void *)action);
    }
    alert->custom=description;
    return alert;
}
static void ui_controller_present(Word object,const char *selector,Word controller,Word animated,Word completion) {
    if (controller && (view_state(controller)->modal_style==3 || view_state(controller)->modal_style==6)) {
        Word context=object;
        while (view_state(context)->parent && !view_state(context)->defines_context) context=view_state(context)->parent;
        object=context;
    }
    if (!controller || view_state(object)->presented || view_state(controller)->presenting)
        stop("Unsupported or already presented controller",selector);
    if (read_word(controller)==external_class("_OBJC_CLASS_$_UIActivityViewController")) { ui_share_present(object,selector,controller,completion); return; }
    if(class_inherits(read_word(controller),external_class("_OBJC_CLASS_$_UIImagePickerController"))){photo_present(object,selector,controller,completion);return;}
    struct UIKitAlert *alert;
    unsigned external_modal=0;
    if (class_inherits(read_word(controller),external_class("_OBJC_CLASS_$_UIAlertController"))) alert=(void *)controller;
    else if (view_state(controller)->deferred_storyboard) alert=storyboard_capability_alert(controller);
    else { alert=extension_modal(controller,selector); if (!alert) stop("Unsupported modal controller",class_name(read_word(controller)) ? class_name(read_word(controller)) : selector); external_modal=1; }
    alert->external_modal=external_modal;
    alert->animated=animated!=0;
    alert->controller=controller; alert->presented=1; alert->presenter=object;
    view_state(object)->presented=controller; view_state(controller)->presenting=object; view_state(controller)->modal_alert=(Word)alert;
    alert->present_completion=completion ? (Word)_Block_copy((void *)completion) : 0;
    ui_alert_present(alert,selector);
}
static jobject ui_modal_text(JNIEnv *env,jclass cls,Word plain,Word attributed,long alignment,unsigned tint) {
    struct UIKitColor color={external_class("_OBJC_CLASS_$_UIColor"),((tint>>16)&255)/255.0,((tint>>8)&255)/255.0,(tint&255)/255.0,1};
    struct LabelState state={.text=attributed ? attributed_string(attributed) : plain ? plain : copy_string(""),.attributed=attributed,.alignment=alignment,.color=(Word)&color};
    jobject text=(*env)->NewLocalRef(env,label_rich_text(env,cls,&state));
    (*env)->DeleteGlobalRef(env,state.rich_text); return text;
}
static void ui_custom_modal_present(struct UIKitAlert *alert,const char *selector) {
    struct I2AModal *description=&alert->custom;
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity),sequence=(*env)->FindClass(env,"java/lang/CharSequence");
    jmethodID show=(*env)->GetMethodID(env,cls,"showGuestModal","(JLjava/lang/String;Ljava/lang/CharSequence;Ljava/lang/CharSequence;[Ljava/lang/CharSequence;F[BZ)V");
    jobject title=ui_modal_text(env,cls,description->title,description->attributed_title,1,0),message=ui_modal_text(env,cls,description->message,description->attributed_message,0,0);
    jobjectArray actions=(*env)->NewObjectArray(env,description->count,sequence,NULL);
    for (unsigned i=0;i<description->count;++i) {
        jobject text=ui_modal_text(env,cls,description->actions[i].title,description->actions[i].attributed_title,1,0x007aff);
        (*env)->SetObjectArrayElement(env,actions,i,text); (*env)->DeleteLocalRef(env,text);
    }
    jbyteArray image=NULL;
    if (description->image_name) {
        Word original=ui_image_named(external_class("_OBJC_CLASS_$_UIImage"),"imageNamed:",copy_string(description->image_name));
        struct HostObject *png=original ? (void *)image_png((void *)original) : NULL;
        if (!png || png->content[0]>INT32_MAX) stop("Invalid external modal image",description->image_name);
        image=(*env)->NewByteArray(env,png->content[0]); (*env)->SetByteArrayRegion(env,image,0,png->content[0],(void *)data_buffer(png));
    }
    jstring name=(*env)->NewStringUTF(env,description->presentation);
    (*env)->CallVoidMethod(env,activity,show,(jlong)alert,name,title,message,actions,(jfloat)screen_scale(),image,(jboolean)alert->animated);
    if (image) (*env)->DeleteLocalRef(env,image);
    (*env)->DeleteLocalRef(env,name); (*env)->DeleteLocalRef(env,title); (*env)->DeleteLocalRef(env,message); (*env)->DeleteLocalRef(env,actions); (*env)->DeleteLocalRef(env,sequence); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android custom modal failed",selector); }
}
static void ui_alert_present(struct UIKitAlert *alert,const char *selector) {
    if (alert->custom.presentation) { ui_custom_modal_present(alert,selector); return; }
    JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity),string=(*env)->FindClass(env,"java/lang/String");
    jmethodID show=(*env)->GetMethodID(env,cls,"showGuestAlert","(JIILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Z[I[Ljava/lang/String;[Ljava/lang/String;[I)V");
    struct Sequence *actions=(void *)alert->actions;
    jint cancel=-1;
    jobjectArray titles=(*env)->NewObjectArray(env,actions->count,string,NULL); jbooleanArray enabled=(*env)->NewBooleanArray(env,actions->count);
    jintArray styles=(*env)->NewIntArray(env,actions->count);
    for (size_t i=0;i<actions->count;++i) {
        struct UIKitAlertAction *action=(void *)actions->items[i]; jstring title=http_java_string(env,action->title);
        (*env)->SetObjectArrayElement(env,titles,i,title); jboolean value=action->enabled; (*env)->SetBooleanArrayRegion(env,enabled,i,1,&value);
        jint style=action->style; (*env)->SetIntArrayRegion(env,styles,i,1,&style);
        if (style==1) cancel=(jint)i;
        if (title) (*env)->DeleteLocalRef(env,title);
    }
    jstring title=http_java_string(env,alert->title),message=http_java_string(env,alert->message);
    struct Sequence *fields=(void *)alert->fields; size_t count=fields ? fields->count : 0;
    jobjectArray values=(*env)->NewObjectArray(env,count,string,NULL),hints=(*env)->NewObjectArray(env,count,string,NULL);
    jintArray options=(*env)->NewIntArray(env,count*5);
    for (size_t i=0;i<count;++i) {
        struct TextFieldState *field=text_field_state(fields->items[i]);
        jstring value=http_java_string(env,field->properties[0]),hint=http_java_string(env,field->properties[1]);
        (*env)->SetObjectArrayElement(env,values,i,value); (*env)->SetObjectArrayElement(env,hints,i,hint);
        jint settings[]={field->properties[13],field->properties[6],field->properties[9],field->properties[15],field->properties[16]};
        (*env)->SetIntArrayRegion(env,options,i*5,5,settings);
        if (value) (*env)->DeleteLocalRef(env,value); if (hint) (*env)->DeleteLocalRef(env,hint);
    }
    (*env)->CallVoidMethod(env,activity,show,(jlong)alert,(jint)alert->style,cancel,title,message,titles,enabled,styles,values,hints,options);
    (*env)->DeleteLocalRef(env,styles);
    (*env)->DeleteLocalRef(env,values); (*env)->DeleteLocalRef(env,hints); (*env)->DeleteLocalRef(env,options);
    if (title) (*env)->DeleteLocalRef(env,title); if (message) (*env)->DeleteLocalRef(env,message);
    (*env)->DeleteLocalRef(env,titles); (*env)->DeleteLocalRef(env,enabled); (*env)->DeleteLocalRef(env,string); (*env)->DeleteLocalRef(env,cls);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android alert presentation failed",selector); }
    LOG("Original UIKit alert queued: actions=%lu",actions->count);
}
static void ui_alert_clear(struct UIKitAlert *alert) {
    if (alert->presenter) view_state(alert->presenter)->presented=0;
    view_state(alert->controller)->presenting=0; view_state(alert->controller)->modal_alert=0; alert->presenter=0; alert->presented=0;
}
static Word ui_legacy_initialize(Word object,const char *selector) {
    struct UIKitLegacyAlert *legacy=(void *)object;
    if (!legacy->alert) {
        legacy->sheet=class_inherits(legacy->isa,external_class("_OBJC_CLASS_$_UIActionSheet"));
        legacy->cancel=legacy->destructive=-1;
        legacy->alert=(void *)ui_alert_create(external_class("_OBJC_CLASS_$_UIAlertController"),selector,0,0,!legacy->sheet);
        legacy->alert->legacy=legacy;
    }
    return object;
}
static Word ui_legacy_add_button(Word object,const char *selector,Word title) {
    ui_legacy_initialize(object,selector); struct UIKitLegacyAlert *legacy=(void *)object;
    Word index=((struct Sequence *)legacy->alert->actions)->count;
    Word action=ui_alert_action(external_class("_OBJC_CLASS_$_UIAlertAction"),selector,title,0,0);
    ui_alert_add(legacy->alert,selector,(void *)action); return index;
}
__attribute__((used,noinline)) static Word ui_legacy_init_arguments(Word object,const char *selector,Word title,Word a,Word b,Word c,Word first,const Word *remaining) {
    ui_legacy_initialize(object,selector); struct UIKitLegacyAlert *legacy=(void *)object;
    ui_alert_set_title(legacy->alert,selector,title);
    legacy->delegate=legacy->sheet ? a : b;
    Word cancel=legacy->sheet ? b : c;
    if (!legacy->sheet) {
        ui_alert_set_message(legacy->alert,selector,a);
        if (cancel) legacy->cancel=ui_legacy_add_button(object,selector,cancel);
    } else if (c) legacy->destructive=ui_legacy_add_button(object,selector,c);
    size_t count=0;
    while (first) {
        if (++count>4096) stop("Unterminated alert button arguments",selector);
        ui_legacy_add_button(object,selector,first); first=*remaining++;
    }
    if (legacy->sheet && cancel) legacy->cancel=ui_legacy_add_button(object,selector,cancel);
    return object;
}
__attribute__((naked)) static void ui_legacy_init(void) { __asm__ volatile("mov x7,sp\n b ui_legacy_init_arguments"); }
static void ui_legacy_delegate_call(struct UIKitLegacyAlert *legacy,const char *alert_selector,const char *sheet_selector,int indexed,intptr_t index) {
    const char *selector=legacy->sheet ? sheet_selector : alert_selector;
    Method callback=legacy->delegate ? find_implementation(legacy->delegate,selector) : NULL;
    if (callback) callback(legacy->delegate,selector,(Word)legacy,indexed ? (Word)index : 0,0,0);
}
static void ui_legacy_begin_dismiss(struct UIKitLegacyAlert *legacy,intptr_t index,int clicked,int cancelled) {
    if (!legacy->alert->presented || legacy->dismissing) return;
    legacy->dismissing=1; legacy->dismiss_index=index;
    if (cancelled) ui_legacy_delegate_call(legacy,"alertViewCancel:","actionSheetCancel:",0,0);
    if (clicked) ui_legacy_delegate_call(legacy,"alertView:clickedButtonAtIndex:","actionSheet:clickedButtonAtIndex:",1,index);
    ui_legacy_delegate_call(legacy,"alertView:willDismissWithButtonIndex:","actionSheet:willDismissWithButtonIndex:",1,index);
}
static void ui_legacy_show_deliver(void *context) {
    struct UIKitLegacyAlert *legacy=context;
    if (!legacy->alert->presented || legacy->dismissing) return;
    ui_legacy_delegate_call(legacy,"willPresentAlertView:","willPresentActionSheet:",0,0);
    if (!legacy->dismissing) ui_alert_present(legacy->alert,"show");
}
static void ui_legacy_show(Word object,const char *selector) {
    ui_legacy_initialize(object,selector); struct UIKitLegacyAlert *legacy=(void *)object;
    if (legacy->alert->presented) return;
    struct Sequence *actions=(void *)legacy->alert->actions;
    for (size_t i=0;i<actions->count;++i) ((struct UIKitAlertAction *)actions->items[i])->style=
        (intptr_t)i==legacy->cancel ? 1 : (intptr_t)i==legacy->destructive ? 2 : 0;
    legacy->alert->presented=1; legacy->dismissing=0;
    schedule_main_function(ui_legacy_show_deliver,legacy);
}
struct UIKitLegacyDismiss { struct UIKitLegacyAlert *legacy; intptr_t index; unsigned animated; };
static void ui_legacy_dismiss_deliver(void *context) {
    struct UIKitLegacyDismiss *request=context; struct UIKitLegacyAlert *legacy=request->legacy;
    intptr_t index=request->index; unsigned animated=request->animated; free(request);
    if (!legacy->alert->presented || legacy->dismissing) return;
    ui_legacy_begin_dismiss(legacy,index,0,0);
    JNIEnv *env=attached_environment("dismissWithClickedButtonIndex:animated:"); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID dismiss=(*env)->GetMethodID(env,cls,"dismissGuestAlert","(JJZ)V");
    (*env)->CallVoidMethod(env,activity,dismiss,(jlong)legacy->alert,(jlong)0,(jboolean)animated); (*env)->DeleteLocalRef(env,cls);
}
static void ui_legacy_dismiss(Word object,const char *selector,intptr_t index,Word animated) {
    struct UIKitLegacyAlert *legacy=(void *)object; if (!legacy->alert || !legacy->alert->presented) return;
    struct Sequence *actions=(void *)legacy->alert->actions;
    if (index < -1 || (index>=0 && (size_t)index>=actions->count)) stop("Invalid alert button index",selector);
    struct UIKitLegacyDismiss *request=malloc(sizeof(*request)); if (!request) stop("Allocation failed",selector);
    *request=(struct UIKitLegacyDismiss){legacy,index,animated!=0}; schedule_main_function(ui_legacy_dismiss_deliver,request);
}
static Word ui_legacy_property(Word object,const char *selector) {
    ui_legacy_initialize(object,selector); struct UIKitLegacyAlert *legacy=(void *)object;
    if (!strcmp(selector,"title")) return legacy->alert->title;
    if (!strcmp(selector,"message")) return legacy->alert->message;
    if (!strcmp(selector,"delegate")) return legacy->delegate;
    if (!strcmp(selector,"cancelButtonIndex")) return legacy->cancel;
    if (!strcmp(selector,"destructiveButtonIndex")) return legacy->destructive;
    if (!strcmp(selector,"isVisible")) return legacy->alert->presented;
    struct Sequence *actions=(void *)legacy->alert->actions;
    if (!strcmp(selector,"numberOfButtons")) return actions->count;
    for (size_t i=0;i<actions->count;++i) if ((intptr_t)i!=legacy->cancel && (intptr_t)i!=legacy->destructive) return i;
    return -1;
}
static void ui_legacy_set_property(Word object,const char *selector,Word value) {
    ui_legacy_initialize(object,selector); struct UIKitLegacyAlert *legacy=(void *)object;
    if (!strcmp(selector,"setTitle:")) ui_alert_set_title(legacy->alert,selector,value);
    else if (!strcmp(selector,"setMessage:")) ui_alert_set_message(legacy->alert,selector,value);
    else if (!strcmp(selector,"setDelegate:")) legacy->delegate=value;
    else {
        struct Sequence *actions=(void *)legacy->alert->actions;
        if ((intptr_t)value < -1 || ((intptr_t)value>=0 && value>=actions->count)) stop("Invalid alert button index",selector);
        if (!strcmp(selector,"setCancelButtonIndex:")) legacy->cancel=value; else legacy->destructive=value;
    }
}
static Word ui_legacy_button_title(Word object,const char *selector,Word index) {
    ui_legacy_initialize(object,selector); struct UIKitLegacyAlert *legacy=(void *)object;
    struct Sequence *actions=(void *)legacy->alert->actions;
    if (index>=actions->count) stop("Invalid alert button index",selector);
    return ((struct UIKitAlertAction *)actions->items[index])->title;
}
static void ui_alert_completion_deliver(void *copied) { Word block=(Word)copied; ((void (*)(Word))read_word(block+16))(block); _Block_release(copied); }
static void ui_controller_dismiss(Word object,const char *selector,Word animated,Word completion) {
    Word target=view_state(object)->presenting ? object : view_state(object)->presented;
    Word copied=completion ? (Word)_Block_copy((void *)completion) : 0;
    if (target) {
        if (read_word(target)==external_class("_OBJC_CLASS_$_UIActivityViewController")) { ui_share_dismiss(target,selector,copied); return; }
        if(class_inherits(read_word(target),external_class("_OBJC_CLASS_$_UIImagePickerController"))){photo_dismiss(target,selector,copied);return;}
        struct UIKitAlert *alert=(void *)view_state(target)->modal_alert;
        if (!alert) stop("Unsupported modal controller",selector);
        ui_alert_clear(alert);
        JNIEnv *env=attached_environment(selector); jclass cls=(*env)->GetObjectClass(env,activity);
        jmethodID dismiss=(*env)->GetMethodID(env,cls,"dismissGuestAlert","(JJZ)V"); (*env)->CallVoidMethod(env,activity,dismiss,(jlong)alert,(jlong)copied,(jboolean)(animated!=0)); (*env)->DeleteLocalRef(env,cls);
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); stop("Android modal dismissal failed",selector); }
        return;
    }
    if (copied) schedule_main_function(ui_alert_completion_deliver,(void *)copied);
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completeModalDismiss)(JNIEnv *env,jobject instance,jlong value) {
    may_unwind=1; snprintf(stage,sizeof(stage),"Original modal dismiss completion"); int failed=setjmp(boundary);
    if (!failed && value) ((void (*)(Word))read_word((Word)value+16))((Word)value);
    if (value) _Block_release((void *)(Word)value);
    may_unwind=0; return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completeAlert)(JNIEnv *env,jobject instance,jlong token,jint index) {
    struct UIKitAlert *alert=(void *)(Word)token; if (!alert || !class_inherits(alert->isa,external_class("_OBJC_CLASS_$_UIAlertController"))) return (*env)->NewStringUTF(env,"Invalid original alert handle");
    may_unwind=1; snprintf(stage,sizeof(stage),"Original UIKit alert callback"); int failed=setjmp(boundary);
    if (!failed && alert->legacy) {
        struct UIKitLegacyAlert *legacy=alert->legacy;
        if (index==-1) {
            if (alert->presented && !legacy->dismissing) ui_legacy_delegate_call(legacy,"didPresentAlertView:","didPresentActionSheet:",0,0);
        } else if (index==-3) {
            if (legacy->dismissing) {
                intptr_t selected=legacy->dismiss_index; alert->presented=0; legacy->dismissing=0;
                ui_legacy_delegate_call(legacy,"alertView:didDismissWithButtonIndex:","actionSheet:didDismissWithButtonIndex:",1,selected);
            }
        } else {
            intptr_t selected=index==-2 ? legacy->cancel : index;
            struct Sequence *actions=(void *)alert->actions;
            if (selected < -1 || (selected>=0 && (size_t)selected>=actions->count)) stop("Invalid alert action index","UIKit");
            ui_legacy_begin_dismiss(legacy,selected,index>=0,index==-2);
        }
    } else if (!failed && index==-1) {
        Word block=alert->present_completion; alert->present_completion=0;
        if (block) { ((void (*)(Word))read_word(block+16))(block); _Block_release((void *)block); }
    } else if (!failed && index!=-3) {
        struct Sequence *actions=(void *)alert->actions;
        if (index==-2) for (size_t i=0;i<actions->count;++i) if (((struct UIKitAlertAction *)actions->items[i])->style==1) { index=i; break; }
        if (index<0 || (size_t)index>=actions->count) stop("Invalid alert action index","UIKit");
        struct UIKitAlertAction *action=(void *)actions->items[index];
        if (!action->enabled) stop("Disabled alert action selected","UIKit");
        if (alert->external_modal) {
            if (!alert->submitting && alert->presented) {
                alert->submitting=1;
                runtime_extension.complete_modal(alert->controller,(unsigned)index);
            }
        } else {
            Word controller=alert->controller;
            ui_alert_clear(alert);
            if (view_state(controller)->deferred_storyboard) storyboard_capability_close(controller);
            if (action->handler) ((void (*)(Word,Word))read_word(action->handler+16))(action->handler,(Word)action);
        }
    }
    may_unwind=0; return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
static Method ui_alerts_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    Word cls=class_receiver ? object : read_word(object);
    int legacy_sheet=class_inherits(cls,external_class("_OBJC_CLASS_$_UIActionSheet"));
    if (!class_receiver && (legacy_sheet || class_inherits(cls,external_class("_OBJC_CLASS_$_UIAlertView")))) {
        if (!strcmp(selector,"init")) return (Method)ui_legacy_initialize;
        if ((!legacy_sheet && !strcmp(selector,"initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:")) ||
            (legacy_sheet && !strcmp(selector,"initWithTitle:delegate:cancelButtonTitle:destructiveButtonTitle:otherButtonTitles:"))) return (Method)ui_legacy_init;
        if ((!legacy_sheet && !strcmp(selector,"show")) || (legacy_sheet && (!strcmp(selector,"showInView:") || !strcmp(selector,"showFromTabBar:") || !strcmp(selector,"showFromToolbar:")))) return (Method)ui_legacy_show;
        if (!strcmp(selector,"dismissWithClickedButtonIndex:animated:")) return (Method)ui_legacy_dismiss;
        if (!strcmp(selector,"addButtonWithTitle:")) return (Method)ui_legacy_add_button;
        if (!strcmp(selector,"buttonTitleAtIndex:")) return (Method)ui_legacy_button_title;
        if (!strcmp(selector,"title") || (!legacy_sheet && !strcmp(selector,"message")) || !strcmp(selector,"delegate") || !strcmp(selector,"cancelButtonIndex") ||
            (legacy_sheet && !strcmp(selector,"destructiveButtonIndex")) || !strcmp(selector,"isVisible") || !strcmp(selector,"numberOfButtons") || !strcmp(selector,"firstOtherButtonIndex")) return (Method)ui_legacy_property;
        if (!strcmp(selector,"setTitle:") || (!legacy_sheet && !strcmp(selector,"setMessage:")) || !strcmp(selector,"setDelegate:") || !strcmp(selector,"setCancelButtonIndex:") ||
            (legacy_sheet && !strcmp(selector,"setDestructiveButtonIndex:"))) return (Method)ui_legacy_set_property;
    }
    if (class_inherits(cls,external_class("_OBJC_CLASS_$_UIAlertController"))) {
        if (class_receiver && !strcmp(selector,"alertControllerWithTitle:message:preferredStyle:")) return (Method)ui_alert_create;
        if (!class_receiver) {
            if (!strcmp(selector,"setModalPresentationStyle:")) return (Method)controller_modal_style;
            if (!strcmp(selector,"modalPresentationStyle")) return (Method)controller_get_modal_style;
            if (!strcmp(selector,"addAction:")) return (Method)ui_alert_add;
            if (!strcmp(selector,"addTextFieldWithConfigurationHandler:")) return (Method)ui_alert_add_field;
            if (!strcmp(selector,"textFields")) return (Method)ui_alert_fields;
            if (!strcmp(selector,"title")) return (Method)ui_alert_title;
            if (!strcmp(selector,"message")) return (Method)ui_alert_message;
            if (!strcmp(selector,"actions")) return (Method)ui_alert_actions;
            if (!strcmp(selector,"preferredStyle")) return (Method)ui_alert_style;
            if (!strcmp(selector,"setTitle:")) return (Method)ui_alert_set_title;
            if (!strcmp(selector,"setMessage:")) return (Method)ui_alert_set_message;
            if (!strcmp(selector,"setValue:forKey:")) return (Method)ui_alert_value;
            if (!strcmp(selector,"presentedViewController")) return (Method)controller_presented;
            if (!strcmp(selector,"presentingViewController")) return (Method)controller_presenting;
        }
    }
    if (class_inherits(cls,external_class("_OBJC_CLASS_$_UIAlertAction"))) {
        if (class_receiver && !strcmp(selector,"actionWithTitle:style:handler:")) return (Method)ui_alert_action;
        if (!class_receiver) {
            if (!strcmp(selector,"title")) return (Method)ui_action_title;
            if (!strcmp(selector,"style")) return (Method)ui_action_style;
            if (!strcmp(selector,"isEnabled")) return (Method)ui_action_enabled;
            if (!strcmp(selector,"setEnabled:")) return (Method)ui_action_set_enabled;
        }
    }
    if (!class_receiver && class_inherits(cls,external_class("_OBJC_CLASS_$_UIViewController"))) {
        if (!strcmp(selector,"presentViewController:animated:completion:")) return (Method)ui_controller_present;
        if (!strcmp(selector,"dismissViewControllerAnimated:completion:")) return (Method)ui_controller_dismiss;
    }
    return NULL;
}
