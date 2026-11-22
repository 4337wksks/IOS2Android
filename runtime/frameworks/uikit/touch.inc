 
#define I2A_TOUCH_LIMIT 32
struct I2ATouch {
    Word isa,view,window;
    struct GuestPoint location,previous;
    double timestamp,pressure;
    long phase,taps,type;
    int pointer,active;
};
struct I2ATouchEvent { Word isa; double timestamp; struct Sequence all; };
static struct I2ATouch i2a_touches[I2A_TOUCH_LIMIT];
static Word i2a_changed_items[I2A_TOUCH_LIMIT],i2a_all_items[I2A_TOUCH_LIMIT];
static struct Sequence i2a_changed;
static struct I2ATouchEvent i2a_touch_event;
static uint64_t i2a_touch_deliveries[5];
static struct GuestPoint touch_in_view(struct GuestPoint point,Word view) {
    for (unsigned depth=0;view && view!=(Word)&main_window && depth<1024;++depth) {
        struct ViewState *state=view_state(view);
        point.x+=state->bounds.x-state->frame.x;
        point.y+=state->bounds.y-state->frame.y;
        view=state->parent;
    }
    return point;
}
static struct GuestPoint touch_location(struct I2ATouch *touch,const char *selector,Word view) { return touch_in_view(touch->location,view); }
static struct GuestPoint touch_previous(struct I2ATouch *touch,const char *selector,Word view) { return touch_in_view(touch->previous,view); }
static double touch_timestamp(struct I2ATouch *touch) { return touch->timestamp; }
static double touch_force(struct I2ATouch *touch) { return touch->pressure; }
static double touch_max_force(void) { return 1; }  
static Word touch_view(struct I2ATouch *touch) { return touch->view; }
static Word touch_window(struct I2ATouch *touch) { return touch->window; }
static Word touch_phase(struct I2ATouch *touch) { return touch->phase; }
static Word touch_taps(struct I2ATouch *touch) { return touch->taps; }
static Word touch_type(struct I2ATouch *touch) { return touch->type; }
static double touch_event_time(struct I2ATouchEvent *event) { return event->timestamp; }
static Word touch_event_kind(void) { return 0; }
static Word touch_event_all(struct I2ATouchEvent *event) { return (Word)&event->all; }
static Word touch_event_filtered(struct I2ATouchEvent *event,const char *selector,Word object) {
    Word matches[I2A_TOUCH_LIMIT]; size_t count=0;
    for (size_t i=0;i<event->all.count;++i) {
        struct I2ATouch *touch=(void *)event->all.items[i];
        if ((!strcmp(selector,"touchesForView:") ? touch->view : touch->window)==object) matches[count++]=(Word)touch;
    }
    return new_array(external_class("_OBJC_CLASS_$_NSSet"),selector,matches,count);
}
static Method ui_touch_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || class_receiver) return NULL;
    if (!strcmp(receiver,"_OBJC_CLASS_$_UITouch")) {
        if (!strcmp(selector,"locationInView:") || !strcmp(selector,"preciseLocationInView:")) return (Method)touch_location;
        if (!strcmp(selector,"previousLocationInView:") || !strcmp(selector,"precisePreviousLocationInView:")) return (Method)touch_previous;
        if (!strcmp(selector,"timestamp")) return (Method)touch_timestamp;
        if (!strcmp(selector,"force")) return (Method)touch_force;
        if (!strcmp(selector,"maximumPossibleForce")) return (Method)touch_max_force;
        if (!strcmp(selector,"view")) return (Method)touch_view;
        if (!strcmp(selector,"window")) return (Method)touch_window;
        if (!strcmp(selector,"phase")) return (Method)touch_phase;
        if (!strcmp(selector,"tapCount")) return (Method)touch_taps;
        if (!strcmp(selector,"type")) return (Method)touch_type;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_UIEvent")) {
        if (!strcmp(selector,"timestamp")) return (Method)touch_event_time;
        if (!strcmp(selector,"allTouches")) return (Method)touch_event_all;
        if (!strcmp(selector,"touchesForView:") || !strcmp(selector,"touchesForWindow:")) return (Method)touch_event_filtered;
        if (!strcmp(selector,"type") || !strcmp(selector,"subtype")) return (Method)touch_event_kind;
    }
    return NULL;
}
static void touch_deliver(long phase,double timestamp) {
    if (!i2a_changed.count) return;
    static const char *selectors[]={"touchesBegan:withEvent:","touchesMoved:withEvent:",NULL,"touchesEnded:withEvent:","touchesCancelled:withEvent:"};
    i2a_touch_event.isa=external_class("_OBJC_CLASS_$_UIEvent"); i2a_touch_event.timestamp=timestamp;
    i2a_touch_event.all.isa=i2a_changed.isa=external_class("_OBJC_CLASS_$_NSSet");
    i2a_touch_event.all.items=i2a_all_items; i2a_touch_event.all.capacity=I2A_TOUCH_LIMIT; i2a_touch_event.all.count=0;
    ++i2a_touch_event.all.mutation; ++i2a_changed.mutation;
    for (unsigned i=0;i<I2A_TOUCH_LIMIT;++i) if (i2a_touches[i].active) i2a_all_items[i2a_touch_event.all.count++]=(Word)&i2a_touches[i];
    Method callback=find_implementation(root_controller,selectors[phase]);
    if (!callback) stop("Missing original touch callback",selectors[phase]);
    callback(root_controller,selectors[phase],(Word)&i2a_changed,(Word)&i2a_touch_event,0,0);
    ++i2a_touch_deliveries[phase];
    if (render_diagnostics_requested && (phase!=1 || i2a_touch_deliveries[phase]%60==1))
        LOG("[DIAG-input] original %s changed=%lu active=%lu total=%llu",selectors[phase],i2a_changed.count,i2a_touch_event.all.count,(unsigned long long)i2a_touch_deliveries[phase]);
}
static void touch_cancel_all(double timestamp) {
    if (runtime_extension.cancel_touches) runtime_extension.cancel_touches();
    i2a_changed.items=i2a_changed_items; i2a_changed.capacity=I2A_TOUCH_LIMIT; i2a_changed.count=0;
    for (unsigned i=0;i<I2A_TOUCH_LIMIT;++i) if (i2a_touches[i].active) {
        i2a_touches[i].phase=4; i2a_touches[i].timestamp=timestamp;
        i2a_changed_items[i2a_changed.count++]=(Word)&i2a_touches[i];
    }
    touch_deliver(4,timestamp);
    for (unsigned i=0;i<I2A_TOUCH_LIMIT;++i) i2a_touches[i].active=0;
}
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_deliverTouches)(JNIEnv *env,jobject activity,jint action,jint changed,jlong millis,jintArray pointers,jfloatArray coordinates) {
    if (!application_ready) return NULL;
    may_unwind=1;
    int failed=setjmp(boundary);
    if (!failed) {
        double timestamp=millis*0.001;
        if (action==3) touch_cancel_all(timestamp);
        else {
            jsize count=(*env)->GetArrayLength(env,pointers);
            if (count<1 || count>I2A_TOUCH_LIMIT || (*env)->GetArrayLength(env,coordinates)!=count*4) stop("Invalid touch packet","Android MotionEvent");
            jint ids[I2A_TOUCH_LIMIT]; jfloat positions[I2A_TOUCH_LIMIT*4];
            (*env)->GetIntArrayRegion(env,pointers,0,count,ids); (*env)->GetFloatArrayRegion(env,coordinates,0,count*4,positions);
            if (action==0) touch_cancel_all(timestamp);
            Word view=controller_view(root_controller);
            if (!view || !view_state(view)->interaction || view_state(view)->hidden || view_state(root_controller)->presented) {
                if (runtime_extension.cancel_touches) runtime_extension.cancel_touches();
                goto touch_done;
            }
            double scale=screen_scale(); if (scale<=0) scale=1;
            i2a_changed.items=i2a_changed_items; i2a_changed.capacity=I2A_TOUCH_LIMIT; i2a_changed.count=0;
            long phase=action==0 || action==5 ? 0 : action==1 || action==6 ? 3 : 1;
            for (jsize i=0;i<count;++i) {
                int selected=phase==1 || ids[i]==changed;
                if (runtime_extension.filter_touch && runtime_extension.filter_touch(selected ? phase : 2,ids[i],timestamp,positions[i*4],positions[i*4+1])) continue;
                struct I2ATouch *touch=NULL;
                for (unsigned j=0;j<I2A_TOUCH_LIMIT;++j) if (i2a_touches[j].active && i2a_touches[j].pointer==ids[i]) { touch=&i2a_touches[j]; break; }
                if (!touch && selected && phase==0) {
                    unsigned active=0; for (unsigned j=0;j<I2A_TOUCH_LIMIT;++j) active+=i2a_touches[j].active;
                    if (active && !view_state(view)->multitouch) continue;
                    for (unsigned j=0;j<I2A_TOUCH_LIMIT;++j) if (!i2a_touches[j].active) { touch=&i2a_touches[j]; break; }
                    if (!touch) stop("Touch capacity exceeded","Android MotionEvent");
                    memset(touch,0,sizeof(*touch)); touch->isa=external_class("_OBJC_CLASS_$_UITouch");
                    touch->pointer=ids[i]; touch->active=1; touch->view=view; touch->window=(Word)&main_window; touch->taps=1;
                    touch->location=(struct GuestPoint){positions[i*4]/scale,positions[i*4+1]/scale};
                }
                if (!touch) continue;
                touch->previous=touch->location;
                touch->location=(struct GuestPoint){positions[i*4]/scale,positions[i*4+1]/scale};
                touch->pressure=fmax(0,positions[i*4+2]); touch->timestamp=timestamp;
                touch->type=positions[i*4+3]==2 ? 2 : 0;
                touch->phase=selected ? phase : 2;
                if (selected) i2a_changed_items[i2a_changed.count++]=(Word)touch;
            }
            touch_deliver(phase,timestamp);
            if (phase==3) for (size_t i=0;i<i2a_changed.count;++i) ((struct I2ATouch *)i2a_changed_items[i])->active=0;
        }
    }
touch_done:
    may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}
