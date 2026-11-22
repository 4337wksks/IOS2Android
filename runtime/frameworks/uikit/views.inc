 
struct GuestPoint { double x,y; };
struct GuestSize { double width,height; };
static void ui_label_sync(Word);
static Word ui_alert_initialize(Word,const char *);
static void ui_view_order_sync(Word);
static void view_request_layout(Word,const char *);
static void view_layout_if_needed(Word,const char *);
static void controller_geometry(Word);
static struct GuestSize ui_view_intrinsic_size(Word);
static int ui_view_rendered(Word);
static void ui_view_load_content(Word,const char *,const double *);
static void ui_view_request_display(Word,const char *);
struct ViewOpacity { Word object; float before; struct ViewOpacity *next; };
struct ViewAnimation { struct ViewOpacity *items,*sources; struct ViewAnimation *previous; };
static _Thread_local struct ViewAnimation *view_animation;
static void view_animation_capture(Word);
static void view_animation_change(Word);
struct ViewState {
    Word object,parent,children,layer,controller,view,owner_window,image_name,background,presented,presenting,modal_alert,popover,navigation_bar,storyboard,nib_name,nib_bundle;
    Word navigation_item,title,prompt,left_item,right_item,back_item;
    void *control;
    Word accessibility_label;
    Word constraints;
    struct GuestRect margins;
    struct GuestRect frame,bounds;
    double alpha,scale;
    double corner,z,window_level;
    long tag,content_mode,autoresizing,modal_style,transition_style,bar_style;
    int hidden,interaction,multitouch,clips,presenting_transition,dismissing_transition,modal_in_popover,deferred_storyboard,loading_view,view_did_load,autoresizes,exclusive_touch,accessibility_element;
    unsigned translates,layout_managed,layout_pending,layout_running,layout_notify,needs_layout,needs_constraints;
    unsigned edges,automatic_insets,defines_context,opaque_bars,clears_selection,moving_to,moving_from,opaque;
    unsigned needs_display,display_pending,display_running,display_checked;
    Method display_method;
    unsigned display_epoch;
    struct ViewState *next;
};
static struct ViewState *view_states;
static struct ViewState *view_state(Word object) {
    for (struct ViewState *state=view_states;state;state=state->next) if (state->object==object) return state;
    struct ViewState *state=calloc(1,sizeof(*state));
    if (!state) stop("Allocation failed","UIView state");
    state->object=object; state->alpha=1; state->scale=screen_scale(); state->interaction=1; state->autoresizes=1; state->translates=1;
    state->margins=(struct GuestRect){8,8,8,8};
    state->edges=15; state->automatic_insets=1; state->clears_selection=1; state->opaque=!class_inherits(read_word(object),external_class("_OBJC_CLASS_$_UILabel"));
    state->children=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));
    state->next=view_states; view_states=state;
    return state;
}
static void view_sync_subtree(Word object) {
    struct ViewState *state=view_state(object);
    ui_label_sync(object);
    struct Sequence *children=(void *)state->children;
    for (size_t i=0;i<children->count;++i) view_sync_subtree(children->items[i]);
}
static Word view_init_frame(Word object,const char *selector,struct GuestRect frame) {
    struct ViewState *state=view_state(object);
    state->frame=frame; state->bounds=(struct GuestRect){0,0,frame.width,frame.height}; state->needs_display=1;
    return object;
}
static Word view_initialize(Word object,const char *selector) {
    Method initialize=find_implementation(object,"initWithFrame:");
    if (!initialize) stop("Missing UIView initializer","initWithFrame:");
    return ((Word (*)(Word,const char *,struct GuestRect))initialize)(object,"initWithFrame:",(struct GuestRect){0});
}
static struct GuestRect view_frame(Word object) { return view_state(object)->frame; }
static struct GuestRect view_bounds(Word object) { return view_state(object)->bounds; }
static void view_draw_rect(Word object,const char *selector,struct GuestRect rect) {}
static void view_background(Word object,const char *selector,Word color) { view_state(object)->background=color; view_sync_subtree(object); }
static Word view_get_background(Word object) { return view_state(object)->background; }
static double view_window_level(Word object) { for (Word current=object;current;current=view_state(current)->parent) if (class_inherits(read_word(current),external_class("_OBJC_CLASS_$_UIWindow"))) return view_state(current)->window_level; return 0; }
static void window_set_level(Word object,const char *selector,double value) { if (!isfinite(value)) stop("Invalid window level",selector); view_state(object)->window_level=value; view_sync_subtree(object); }
static double window_get_level(Word object) { return view_state(object)->window_level; }
static void view_set_opaque(Word object,const char *selector,Word value) { view_state(object)->opaque=value!=0; }
static Word view_get_opaque(Word object) { return view_state(object)->opaque; }
static void view_resize_axis(double *origin,double *length,double parent,double delta,unsigned mask) {
    if (!mask || !delta) return;
    double segments[]={*origin,*length,parent-*origin-*length},total=0; unsigned flexible=0;
    for (unsigned i=0;i<3;++i) if (mask&(1U<<i)) { total+=fmax(0,segments[i]); ++flexible; }
    double leading=(mask&1) ? delta*(total>0 ? fmax(0,segments[0])/total : 1.0/flexible) : 0;
    double extent=(mask&2) ? delta*(total>0 ? fmax(0,segments[1])/total : 1.0/flexible) : 0;
    *origin+=leading; *length=fmax(0,*length+extent);
}
static void view_resize_children(struct ViewState *state,double width,double height) {
    double dx=state->bounds.width-width,dy=state->bounds.height-height;
    if (!state->autoresizes || (!dx && !dy)) return;
    struct Sequence *children=(void *)state->children;
    for (size_t i=0;i<children->count;++i) {
        struct ViewState *child=view_state(children->items[i]);
        if (!child->autoresizing || !child->translates) continue;
        double old_width=child->bounds.width,old_height=child->bounds.height;
        view_resize_axis(&child->frame.x,&child->frame.width,width,dx,(unsigned)child->autoresizing&7);
        view_resize_axis(&child->frame.y,&child->frame.height,height,dy,((unsigned)child->autoresizing>>3)&7);
        child->bounds.width=child->frame.width; child->bounds.height=child->frame.height;
        view_resize_children(child,old_width,old_height);
    }
}
static void view_set_frame(Word object,const char *selector,struct GuestRect frame) {
    struct ViewState *state=view_state(object); double width=state->bounds.width,height=state->bounds.height;
    if (!memcmp(&state->frame,&frame,sizeof(frame))) return;
    state->frame=frame; state->bounds.width=frame.width; state->bounds.height=frame.height;
    state->needs_display|=state->content_mode==3;
    view_resize_children(state,width,height); view_sync_subtree(object);
    view_request_layout(object,selector);
}
static void view_set_bounds(Word object,const char *selector,struct GuestRect bounds) {
    struct ViewState *state=view_state(object);
    if (!memcmp(&state->bounds,&bounds,sizeof(bounds))) return;
    double width=state->bounds.width,height=state->bounds.height;
    state->frame.x+=(state->bounds.width-bounds.width)*0.5;
    state->frame.y+=(state->bounds.height-bounds.height)*0.5;
    state->frame.width=bounds.width; state->frame.height=bounds.height; state->bounds=bounds;
    view_resize_children(state,width,height); view_sync_subtree(object);
    view_request_layout(object,selector);
}
static struct GuestPoint view_center(Word object) {
    struct GuestRect frame=view_state(object)->frame;
    return (struct GuestPoint){frame.x+frame.width*0.5,frame.y+frame.height*0.5};
}
static void view_set_center(Word object,const char *selector,struct GuestPoint center) {
    struct ViewState *state=view_state(object);
    state->frame.x=center.x-state->frame.width*0.5; state->frame.y=center.y-state->frame.height*0.5;
    view_sync_subtree(object);
}
static void view_hidden(Word object,const char *selector,unsigned char hidden) { view_state(object)->hidden=hidden!=0; view_sync_subtree(object); }
static Word view_is_hidden(Word object) { return view_state(object)->hidden; }
static void view_alpha(Word object,const char *selector,double alpha) { if (view_animation) view_animation_change(object); view_state(object)->alpha=fmin(1,fmax(0,alpha)); view_sync_subtree(object); }
static double view_get_alpha(Word object) { return view_state(object)->alpha; }
static void view_scale(Word object,const char *selector,double scale) { if (scale>0) view_state(object)->scale=scale; }
static double view_get_scale(Word object) { return view_state(object)->scale; }
static void view_interaction(Word object,const char *selector,unsigned char enabled) { view_state(object)->interaction=enabled!=0; view_sync_subtree(object); }
static Word view_get_interaction(Word object) { return view_state(object)->interaction; }
static void view_multitouch(Word object,const char *selector,unsigned char enabled) { view_state(object)->multitouch=enabled!=0; }
static Word view_get_multitouch(Word object) { return view_state(object)->multitouch; }
static void view_exclusive_touch(Word object,const char *selector,Word enabled) { view_state(object)->exclusive_touch=enabled!=0; }
static Word view_get_exclusive_touch(Word object) { return view_state(object)->exclusive_touch; }
static void view_accessibility(Word object,const char *selector,Word value) {
    struct ViewState *state=view_state(object);
    if (!strcmp(selector,"setIsAccessibilityElement:")) state->accessibility_element=value!=0;
    else state->accessibility_label=value ? text_copy(value,"copy") : 0;
    view_sync_subtree(object);
}
static Word view_get_accessibility(Word object,const char *selector) {
    return !strcmp(selector,"isAccessibilityElement") ? view_state(object)->accessibility_element : view_state(object)->accessibility_label;
}
static void view_tag(Word object,const char *selector,long tag) { view_state(object)->tag=tag; }
static Word view_get_tag(Word object) { return view_state(object)->tag; }
static void view_content_mode(Word object,const char *selector,long mode) { view_state(object)->content_mode=mode; }
static Word view_get_content_mode(Word object) { return view_state(object)->content_mode; }
static void view_set_autoresizing(Word object,const char *selector,Word mask) { view_state(object)->autoresizing=(long)mask; }
static Word view_autoresizing(Word object) { return (Word)view_state(object)->autoresizing; }
static void view_set_autoresizes(Word object,const char *selector,unsigned char enabled) { view_state(object)->autoresizes=enabled!=0; }
static Word view_autoresizes(Word object) { return view_state(object)->autoresizes; }
static Word view_find_tag(Word object,const char *selector,long tag) {
    struct ViewState *state=view_state(object);
    if (state->tag==tag) return object;
    struct Sequence *children=(void *)state->children;
    for (size_t i=0;i<children->count;++i) { Word found=view_find_tag(children->items[i],selector,tag); if (found) return found; }
    return 0;
}
static void view_remove(Word object) {
    struct ViewState *state=view_state(object);
    if (state->parent) sequence_remove((void *)view_state(state->parent)->children,"removeFromSuperview",object);
    state->parent=0;
    view_sync_subtree(object);
}
static void view_add(Word object,const char *selector,Word child) {
    if (!child) stop("Nil subview",selector);
    for (Word ancestor=object;ancestor;ancestor=view_state(ancestor)->parent) if (ancestor==child) stop("Cyclic UIView tree",selector);
    view_remove(child);
    view_state(child)->parent=object;
    sequence_add((void *)view_state(object)->children,selector,child);
    view_sync_subtree(child);
    ui_view_order_sync(object);
}
static Word view_superview(Word object) { return view_state(object)->parent; }
static Word view_subviews(Word object) { return sequence_copy((void *)view_state(object)->children,"copy"); }
static void view_move_child(Word object,const char *selector,size_t from,size_t to) {
    struct Sequence *children=(void *)view_state(object)->children;
    if (from>=children->count || to>=children->count) stop("Subview index out of range",selector);
    if (from==to) return;
    Word child=children->items[from];
    if (from<to) memmove(children->items+from,children->items+from+1,(to-from)*sizeof(Word));
    else memmove(children->items+to+1,children->items+to,(from-to)*sizeof(Word));
    children->items[to]=child; ++children->mutation; ui_view_order_sync(object);
}
static void view_insert_index(Word object,const char *selector,Word child,Word index) {
    struct Sequence *children=(void *)view_state(object)->children;
    if (index>children->count) stop("Subview index out of range",selector);
    if (child && view_state(child)->parent==object && index<children->count && children->items[index]==child) return;
    view_add(object,selector,child);
    if (index>=children->count) index=children->count-1;
    view_move_child(object,selector,children->count-1,index);
}
static void view_insert_relative(Word object,const char *selector,Word child,Word sibling) {
    if (!sibling || view_state(sibling)->parent!=object) stop("Relative view is not a subview",selector);
    if (child==sibling) return;
    view_add(object,selector,child); struct Sequence *children=(void *)view_state(object)->children;
    for (size_t i=0;i<children->count;++i) if (children->items[i]==sibling) {
        view_move_child(object,selector,children->count-1,i+(strstr(selector,"aboveSubview:")!=NULL)); return;
    }
}
static void view_reorder(Word object,const char *selector,Word child) {
    struct Sequence *children=(void *)view_state(object)->children;
    for (size_t i=0;i<children->count;++i) if (children->items[i]==child) {
        view_move_child(object,selector,i,!strcmp(selector,"bringSubviewToFront:") ? children->count-1 : 0); return;
    }
}
static void view_exchange(Word object,const char *selector,Word first,Word second) {
    struct Sequence *children=(void *)view_state(object)->children;
    if (first>=children->count || second>=children->count) stop("Subview index out of range",selector);
    if (first==second) return;
    Word temporary=children->items[first]; children->items[first]=children->items[second]; children->items[second]=temporary;
    ++children->mutation; ui_view_order_sync(object);
}
static Word default_layer_class(void) { return external_class("_OBJC_CLASS_$_CALayer"); }
static Word view_layer(Word object) {
    struct ViewState *state=view_state(object);
    if (!state->layer) {
        Word cls=read_word(object);
        Method layer_class=find_implementation(cls,"layerClass");
        Word layer=layer_class ? layer_class(cls,"layerClass",0,0,0,0) : default_layer_class();
        state->layer=host_allocate(layer);
        struct ViewState *layer_state=view_state(state->layer);
        layer_state->view=object;
        layer_state->frame=state->bounds; layer_state->bounds=state->bounds; layer_state->scale=state->scale;
    }
    return state->layer;
}
static Word controller_view(Word object) {
    struct ViewState *state=view_state(object);
    if (!state->view && object==root_controller) state->view=(Word)&root_view;
    if (!state->view && !state->loading_view) {
        state->loading_view=1;
        Method load=find_implementation(object,"loadView");
        if (load) load(object,"loadView",0,0,0,0);
        else {
            if (state->deferred_storyboard || state->nib_name) { state->loading_view=0; stop("Archived controller view decoding unavailable","loadView"); }
            Word cls=read_word(object),view_cls=external_class(class_inherits(cls,external_class("_OBJC_CLASS_$_UITableViewController")) ? "_OBJC_CLASS_$_UITableView" : "_OBJC_CLASS_$_UIView");
            state->view=view_init_frame(host_allocate(view_cls),"loadView",screen_bounds());
        }
        state->loading_view=0;
        if (!state->view) stop("Controller loadView did not set a view",class_name(read_word(object)));
        view_state(state->view)->controller=object;
        if (!state->view_did_load) {
            state->view_did_load=1; Method loaded=find_implementation(object,"viewDidLoad");
            if (loaded) loaded(object,"viewDidLoad",0,0,0,0);
        }
    }
    if (state->view) view_state(state->view)->controller=object;
    return state->view;
}
static Word controller_loaded_view(Word object) { return view_state(object)->view; }
static Word controller_init_nib(Word object,const char *selector,Word name,Word bundle) {
    struct ViewState *state=view_state(object);
    state->nib_name=name ? text_copy(name,"copy") : 0; state->nib_bundle=bundle;
    return object;
}
static Word controller_initialize(Word object,const char *selector) {
    const char *initializer=class_inherits(read_word(object),external_class("_OBJC_CLASS_$_UITableViewController")) ? "initWithStyle:" : "initWithNibName:bundle:";
    Method initialize=find_implementation(object,initializer);
    if (!initialize) stop("Missing controller initializer",initializer);
    return initialize(object,initializer,0,0,0,0);
}
static Word controller_init_style(Word object,const char *selector,long style) { view_state(object)->bar_style=style; return object; }
static Word controller_table_view(Word object) { return controller_view(object); }
static void view_strong_property(Word *slot,Word value) {
    Word previous=*slot; *slot=metal_object_retain(value); metal_object_release(previous);
}
static Word navigation_item_title(Word object) { return view_state(object)->title; }
static void navigation_item_set_title(Word object,const char *selector,Word title) {
    struct ViewState *state=view_state(object); view_strong_property(&state->title,title ? text_copy(title,"copy") : 0);
}
static Word controller_navigation_item(Word object) {
    struct ViewState *state=view_state(object);
    if (!state->navigation_item) {
        state->navigation_item=host_allocate(external_class("_OBJC_CLASS_$_UINavigationItem"));
        navigation_item_set_title(state->navigation_item,"setTitle:",state->title);
    }
    return state->navigation_item;
}
static void controller_set_title(Word object,const char *selector,Word title) {
    navigation_item_set_title(object,selector,title);
    struct ViewState *state=view_state(object);
    if (state->navigation_item) navigation_item_set_title(state->navigation_item,selector,title);
}
static Word navigation_item_init(Word object,const char *selector,Word title) { navigation_item_set_title(object,selector,title); return object; }
static Word navigation_item_value(Word object,const char *selector) {
    struct ViewState *state=view_state(object);
    if (!strcmp(selector,"leftBarButtonItem")) return state->left_item;
    if (!strcmp(selector,"rightBarButtonItem")) return state->right_item;
    if (!strcmp(selector,"backBarButtonItem")) return state->back_item;
    if (!strcmp(selector,"titleView")) return state->view;
    return state->prompt;
}
static void navigation_item_set_value(Word object,const char *selector,Word value) {
    struct ViewState *state=view_state(object);
    Word *slot=!strncmp(selector,"setLeftBarButtonItem:",21) ? &state->left_item :
        !strncmp(selector,"setRightBarButtonItem:",22) ? &state->right_item :
        !strcmp(selector,"setBackBarButtonItem:") ? &state->back_item : !strcmp(selector,"setTitleView:") ? &state->view : &state->prompt;
    if (slot==&state->prompt) value=value ? text_copy(value,"copy") : 0;
    view_strong_property(slot,value);
}
static void controller_set_view(Word object,const char *selector,Word view) {
    struct ViewState *state=view_state(object);
    if (state->view==view) return;
    Word parent=state->view ? view_state(state->view)->parent : 0;
     
    if (!parent) parent=state->owner_window;
    if (state->view) { view_state(state->view)->controller=0; view_remove(state->view); }
    state->view=view;
    if (view) view_state(view)->controller=object;
    if (!view) state->view_did_load=0;
    if (parent && view) view_add(parent,selector,view);
}
static Word controller_loaded(Word object) { return view_state(object)->view!=0; }
static void controller_geometry(Word object) {
    struct ViewState *controller=view_state(object); if (!controller->view || !controller->owner_window) return;
    struct ViewState *window=view_state(controller->owner_window); struct GuestRect frame={0,0,window->bounds.width,window->bounds.height};
    JNIEnv *env=attached_environment("UIViewController extended layout"); static jmethodID insets_method;
    if (!insets_method) { jclass cls=(*env)->GetObjectClass(env,activity); insets_method=(*env)->GetMethodID(env,cls,"guestSafeInsets","()[I"); (*env)->DeleteLocalRef(env,cls); }
    jintArray insets=(*env)->CallObjectMethod(env,activity,insets_method); jint values[4]={0};
    if (insets && (*env)->GetArrayLength(env,insets)==4) (*env)->GetIntArrayRegion(env,insets,0,4,values);
    if (insets) (*env)->DeleteLocalRef(env,insets);
    double scale=screen_scale();
    double top=(controller->edges&1) ? 0 : values[0]/scale,left=(controller->edges&2) ? 0 : values[1]/scale;
    double bottom=(controller->edges&4) ? 0 : values[2]/scale,right=(controller->edges&8) ? 0 : values[3]/scale;
    frame.x=left; frame.y=top; frame.width=fmax(0,frame.width-left-right); frame.height=fmax(0,frame.height-top-bottom);
    view_set_frame(controller->view,"UIViewController extended layout",frame);
}
static Word controller_layout_property(Word object,const char *selector) {
    struct ViewState *state=view_state(object);
    if (!strcmp(selector,"edgesForExtendedLayout")) return state->edges;
    if (!strcmp(selector,"automaticallyAdjustsScrollViewInsets")) return state->automatic_insets;
    if (!strcmp(selector,"definesPresentationContext")) return state->defines_context;
    if (!strcmp(selector,"extendedLayoutIncludesOpaqueBars")) return state->opaque_bars;
    if (!strcmp(selector,"clearsSelectionOnViewWillAppear")) return state->clears_selection;
    return !strcmp(selector,"isMovingToParentViewController") ? state->moving_to : state->moving_from;
}
static void controller_set_layout_property(Word object,const char *selector,Word value) {
    struct ViewState *state=view_state(object);
    if (!strcmp(selector,"setEdgesForExtendedLayout:")) { if (value&~15UL) stop("Invalid extended layout edges",selector); state->edges=value; }
    else if (!strcmp(selector,"setAutomaticallyAdjustsScrollViewInsets:")) state->automatic_insets=value!=0;
    else if (!strcmp(selector,"setDefinesPresentationContext:")) state->defines_context=value!=0;
    else if (!strcmp(selector,"setExtendedLayoutIncludesOpaqueBars:")) state->opaque_bars=value!=0;
    else state->clears_selection=value!=0;
    if (!strcmp(selector,"setEdgesForExtendedLayout:") || !strcmp(selector,"setExtendedLayoutIncludesOpaqueBars:")) controller_geometry(object);
}
static Word controller_split_view(Word object) {
    for (Word parent=object;parent;parent=view_state(parent)->parent) if (class_inherits(read_word(parent),external_class("_OBJC_CLASS_$_UISplitViewController"))) return parent;
    return 0;
}
static void controller_move(Word object,const char *selector,Word parent) {
    struct ViewState *state=view_state(object);
    int moving=!strcmp(selector,"willMoveToParentViewController:"); state->moving_to=moving && parent; state->moving_from=moving && !parent;
}
static void controller_add_child(Word object,const char *selector,Word child) {
    if (!child || !class_inherits(read_word(child),external_class("_OBJC_CLASS_$_UIViewController"))) stop("Invalid child controller",selector);
    struct ViewState *state=view_state(child); state->moving_to=1;
    Method will=find_implementation(child,"willMoveToParentViewController:"); if (will) will(child,"willMoveToParentViewController:",object,0,0,0);
    view_add(object,selector,child);
}
static void controller_remove_parent(Word object,const char *selector) {
    if (!view_state(object)->parent) return;
    struct ViewState *state=view_state(object); state->moving_from=1;
    Method will=find_implementation(object,"willMoveToParentViewController:"); if (will) will(object,"willMoveToParentViewController:",0,0,0,0);
    view_remove(object);
    Method did=find_implementation(object,"didMoveToParentViewController:"); if (did) did(object,"didMoveToParentViewController:",0,0,0,0);
    state->moving_to=state->moving_from=0;
}
 
static Word controller_navigation(Word object) {
    object=view_state(object)->parent;
    for (unsigned depth=0;object && depth<1024;++depth) {
        const char *base=external_base(read_word(object));
        if (base && (!strcmp(base,"_OBJC_CLASS_$_UINavigationController") || !strcmp(base,"_OBJC_CLASS_$_UIImagePickerController"))) return object;
        object=view_state(object)->parent;
    }
    return 0;
}
static Word controller_navigation_bar(Word object) {
    struct ViewState *state=view_state(object);
    if (!state->navigation_bar) state->navigation_bar=host_allocate(external_class("_OBJC_CLASS_$_UINavigationBar"));
    return state->navigation_bar;
}
static Word navigation_bar_style(Word object) { return view_state(object)->bar_style; }
static void navigation_bar_set_style(Word object,const char *selector,long style) { view_state(object)->bar_style=style; }
static Word controller_nib_name(Word object) { if (object==root_controller) controller_storyboard(object); return view_state(object)->nib_name; }
static Word controller_nib_bundle(Word object) { if (object==root_controller) controller_storyboard(object); return view_state(object)->nib_bundle; }
static Word navigation_top(Word object) {
    struct Sequence *children=(void *)view_state(object)->children;
    return children->count ? children->items[children->count-1] : 0;
}
static Word navigation_visible(Word object) {
    Word presented=view_state(object)->presented;
    if (presented) return presented;
    Word top=navigation_top(object);
    return top && view_state(top)->presented ? view_state(top)->presented : top;
}
static void navigation_push(Word object,const char *selector,Word controller,Word animated) {
    if (!controller || controller==object || view_state(controller)->parent) stop("Invalid navigation push",selector);
    view_add(object,selector,controller);
}
static Word navigation_pop(Word object,const char *selector,Word animated) {
    struct Sequence *children=(void *)view_state(object)->children;
    if (children->count<=1) return 0;
    Word controller=children->items[children->count-1]; view_remove(controller); return controller;
}
static void navigation_set_controllers(Word object,const char *selector,struct Sequence *controllers,Word animated) {
    if (!controllers || !controllers->count) stop("Empty navigation stack",selector);
    for (size_t i=0;i<controllers->count;++i) {
        Word child=controllers->items[i];
        if (!child || child==object || (view_state(child)->parent && view_state(child)->parent!=object)) stop("Invalid navigation child",selector);
        for (size_t j=0;j<i;++j) if (controllers->items[j]==child) stop("Duplicate navigation child",selector);
    }
    struct Sequence *children=(void *)view_state(object)->children;
    if (controllers==children) return;
    while (children->count) view_remove(children->items[children->count-1]);
    for (size_t i=0;i<controllers->count;++i) view_add(object,selector,controllers->items[i]);
}
static void navigation_set_controllers_plain(Word object,const char *selector,struct Sequence *controllers) { navigation_set_controllers(object,selector,controllers,0); }
static Word navigation_init_root(Word object,const char *selector,Word controller) { navigation_push(object,selector,controller,0); return object; }
static void controller_set_modal_popover(Word object,const char *selector,Word enabled) { view_state(object)->modal_in_popover=enabled!=0; }
static Word controller_modal_popover(Word object) { return view_state(object)->modal_in_popover; }
static Word window_controller(Word object) { return view_state(object)->controller; }
static Word controller_presented(Word object) { return view_state(object)->presented; }
static Word controller_presenting(Word object) { return view_state(object)->presenting; }
static Word controller_is_presenting(Word object) { return view_state(object)->presenting_transition; }
static Word controller_is_dismissing(Word object) { return view_state(object)->dismissing_transition; }
static void controller_modal_style(Word object,const char *selector,long style) { view_state(object)->modal_style=style; }
static Word controller_get_modal_style(Word object) { return view_state(object)->modal_style; }
static void controller_transition_style(Word object,const char *selector,long style) { view_state(object)->transition_style=style; }
static Word controller_get_transition_style(Word object) { return view_state(object)->transition_style; }
static Word controller_popover(Word object) {
    struct ViewState *state=view_state(object);
    if (state->modal_style!=6) return 0;
    if (!state->popover) state->popover=host_allocate(external_class("_OBJC_CLASS_$_UIPopoverPresentationController"));
    return state->popover;
}
static void popover_set_source(Word object,const char *selector,Word source) { view_state(object)->view=source; }
static Word popover_source(Word object) { return view_state(object)->view; }
static void popover_set_delegate(Word object,const char *selector,Word delegate) { view_state(object)->controller=delegate; }
static Word popover_delegate(Word object) { return view_state(object)->controller; }
static void popover_set_arrows(Word object,const char *selector,Word directions) { view_state(object)->tag=directions; }
static Word popover_arrows(Word object) { return view_state(object)->tag; }
static Word view_descendant(Word object,const char *selector,Word ancestor) {
    if (!ancestor) return 0;
    for (unsigned depth=0;object && depth<1024;++depth) {
        if (object==ancestor) return 1;
        object=view_state(object)->parent;
    }
    if (object) stop("Cycle in UIView parent hierarchy",selector);
    return 0;
}
static Word view_window(Word object) {
    for (unsigned depth=0;object && depth<1024;++depth) {
        const char *base=external_base(read_word(object));
        if (object==(Word)&main_window || (base && !strcmp(base,"_OBJC_CLASS_$_UIWindow"))) return object;
        object=view_state(object)->parent;
    }
    return 0;
}
static void window_set_controller(Word object,const char *selector,Word controller) {
    struct ViewState *window=view_state(object);
    if (window->controller && window->controller!=controller) {
        struct ViewState *previous=view_state(window->controller);
        if (previous->view) view_remove(previous->view);
        previous->owner_window=0;
    }
    window->controller=controller;
    if (controller) {
        view_state(controller)->owner_window=object;
        Word view=controller_view(controller);
        if (view && view_state(view)->parent!=object) view_add(object,selector,view);
        if (view_state(controller)->edges!=15) controller_geometry(controller);
    }
}
static void view_set_clips(Word object,const char *selector,Word clips) { view_state(object)->clips=clips!=0; ui_label_sync(object); }
static Word view_clips(Word object) { return view_state(object)->clips; }
static void layer_set_masks(Word object,const char *selector,Word masks) { view_state(object)->clips=masks!=0; ui_label_sync(view_state(object)->view); }
static void layer_set_corner(Word object,const char *selector,double radius) { view_state(object)->corner=radius; ui_label_sync(view_state(object)->view); }
static double layer_corner(Word object) { return view_state(object)->corner; }
static void layer_set_z(Word object,const char *selector,double z) { view_state(object)->z=z; ui_label_sync(view_state(object)->view); }
static double layer_z(Word object) { return view_state(object)->z; }

static void view_animation_capture(Word object) {
    if (ui_view_rendered(object)) {
        struct ViewOpacity *item=view_animation->items;
        while (item && item->object!=object) item=item->next;
        if (!item) { item=calloc(1,sizeof(*item)); if (!item) stop("Allocation failed","UIView animation"); item->object=object; item->next=view_animation->items; view_animation->items=item; }
    }
    struct Sequence *children=(void *)view_state(object)->children;
    for (size_t i=0;i<children->count;++i) view_animation_capture(children->items[i]);
}
static void view_animation_change(Word object) {
    struct ViewOpacity *source=view_animation->sources;
    while (source && source->object!=object) source=source->next;
    if (!source) { source=calloc(1,sizeof(*source)); if (!source) stop("Allocation failed","UIView animation"); source->object=object; source->before=(float)view_state(object)->alpha; source->next=view_animation->sources; view_animation->sources=source; }
    view_animation_capture(object);
}
static void view_animation_free(struct ViewAnimation *context) {
    while (context->items) { struct ViewOpacity *item=context->items; context->items=item->next; free(item); }
    while (context->sources) { struct ViewOpacity *item=context->sources; context->sources=item->next; free(item); }
    free(context);
}
static void view_animate(Word cls,const char *selector,double duration,double delay,Word options,Word animation,Word completion) {
    if (!isfinite(duration) || !isfinite(delay) || duration<0 || delay<0 || duration+delay>(double)INT64_MAX/1000.0) stop("Invalid animation time",selector);
    struct ViewAnimation *context=calloc(1,sizeof(*context)); if (!context) stop("Allocation failed",selector);
    context->previous=view_animation; view_animation=context;
    struct GuestCallBoundary call={.previous=active_guest_call_boundary}; active_guest_call_boundary=&call;
    int failed=setjmp(call.environment);
    if (!failed && animation) { struct Block_layout *block=(void *)animation; ((void (*)(Word))block->invoke)(animation); }
    active_guest_call_boundary=call.previous; view_animation=context->previous;
    if (failed) { char failure[sizeof(status)]; memcpy(failure,status,sizeof(failure)); view_animation_free(context); stop("UIView animation failed",failure); }
    unsigned count=0,sources=0,ancestors=0;
    for (struct ViewOpacity *item=context->items;item;item=item->next) { ++count; for (Word object=item->object;object;object=view_state(object)->parent) ++ancestors; }
    for (struct ViewOpacity *item=context->sources;item;item=item->next) ++sources;
    if (!count && !completion) { view_animation_free(context); return; }
    JNIEnv *env=attached_environment(selector); static jmethodID animate;
    if (!animate) { jclass type=(*env)->GetObjectClass(env,activity); animate=(*env)->GetMethodID(env,type,"animateGuestOpacity","([J[F[F[J[I[J[FJJIJ)V"); (*env)->DeleteLocalRef(env,type); }
    jlongArray changed=(*env)->NewLongArray(env,sources),tokens=(*env)->NewLongArray(env,count),chain=(*env)->NewLongArray(env,ancestors);
    jintArray offsets=(*env)->NewIntArray(env,count+1);
    jfloatArray before=(*env)->NewFloatArray(env,sources),after=(*env)->NewFloatArray(env,sources),model=(*env)->NewFloatArray(env,ancestors);
    if (!animate || !changed || !tokens || !chain || !offsets || !before || !after || !model) { view_animation_free(context); if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env); stop("Android animation allocation failed",selector); }
    unsigned i=0;
    for (struct ViewOpacity *item=context->sources;item;item=item->next,++i) {
        jlong token=item->object; jfloat target=(float)view_state(item->object)->alpha;
        (*env)->SetLongArrayRegion(env,changed,i,1,&token); (*env)->SetFloatArrayRegion(env,before,i,1,&item->before); (*env)->SetFloatArrayRegion(env,after,i,1,&target);
    }
    i=0; jint cursor=0;
    for (struct ViewOpacity *item=context->items;item;item=item->next,++i) {
        jlong token=item->object; (*env)->SetLongArrayRegion(env,tokens,i,1,&token); (*env)->SetIntArrayRegion(env,offsets,i,1,&cursor);
        for (Word object=item->object;object;object=view_state(object)->parent,++cursor) {
            token=object; jfloat alpha=(float)view_state(object)->alpha;
            (*env)->SetLongArrayRegion(env,chain,cursor,1,&token); (*env)->SetFloatArrayRegion(env,model,cursor,1,&alpha);
        }
    }
    (*env)->SetIntArrayRegion(env,offsets,count,1,&cursor);
    Word retained=completion ? (Word)_Block_copy((void *)completion) : 0;
    (*env)->CallVoidMethod(env,activity,animate,changed,before,after,tokens,offsets,chain,model,(jlong)(duration*1000),(jlong)(delay*1000),(jint)options,(jlong)retained);
    (*env)->DeleteLocalRef(env,changed); (*env)->DeleteLocalRef(env,tokens); (*env)->DeleteLocalRef(env,chain); (*env)->DeleteLocalRef(env,offsets); (*env)->DeleteLocalRef(env,before); (*env)->DeleteLocalRef(env,after); (*env)->DeleteLocalRef(env,model); view_animation_free(context);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); if (retained) _Block_release((void *)retained); stop("Android animation failed",selector); }
}
static void view_animate_plain(Word cls,const char *selector,double duration,Word animation,Word completion) { view_animate(cls,selector,duration,0,0,animation,completion); }
JNIEXPORT jstring JNICALL HOST_JNI(HostActivity_completeAnimation)(JNIEnv *env,jobject activity,jlong value,jboolean completed) {
    may_unwind=1;
    snprintf(stage,sizeof(stage),"UIView animation completion");
    int failed=setjmp(boundary);
    if (!failed) {
        struct Block_layout *block=(void *)value;
        ((void (*)(void *,unsigned char))block->invoke)(block,completed);
    }
    _Block_release((void *)value);
    may_unwind=0;
    return failed ? (*env)->NewStringUTF(env,status) : NULL;
}

static double scene_number(plist_t dict,const char *key,double fallback) {
    plist_t value=plist_dict_get_item(dict,key);
    if (!value) return fallback;
    if (plist_get_node_type(value)==PLIST_BOOLEAN) { uint8_t flag=0; plist_get_bool_val(value,&flag); return flag; }
    if (plist_get_node_type(value)==PLIST_REAL) { double real=0; plist_get_real_val(value,&real); return real; }
    int64_t integer=0; plist_get_int_val(value,&integer); return (double)integer;
}
static void scene_attribute(double *equation,int node,int attribute,double scale) {
    if (node<0) return;
    double *terms=equation+node*4;
    switch (attribute) {
        case 1: case 5: terms[0]+=scale; break;
        case 2: case 6: terms[0]+=scale; terms[2]+=scale; break;
        case 3: terms[1]+=scale; break;
        case 4: terms[1]+=scale; terms[3]+=scale; break;
        case 7: terms[2]+=scale; break;
        case 8: terms[3]+=scale; break;
        case 9: terms[0]+=scale; terms[2]+=scale*0.5; break;
        case 10: terms[1]+=scale; terms[3]+=scale*0.5; break;
        default: stop("Unsupported startup layout attribute","Storyboard");
    }
}
static int layout_equation(double *basis,unsigned variables,double *row,int required) {
    for (unsigned i=0;i<variables;++i) {
        double factor=row[i];
        if (fabs(factor)<1e-9) continue;
        double *pivot=basis+i*(variables+1);
        if (pivot[i]) { for (unsigned j=i;j<=variables;++j) row[j]-=factor*pivot[j]; }
        else { for (unsigned j=i;j<=variables;++j) pivot[j]=row[j]/factor; return 1; }
    }
    return !required || fabs(row[variables])<=1e-5;
}
static void scene_equation(double *basis,unsigned variables,double *row,int required) { if (!layout_equation(basis,variables,row,required)) stop("Inconsistent required startup constraint","Storyboard"); }
struct LayoutConstraint { Word isa,first,second,owner,identifier; intptr_t attribute,other_attribute,relation; double multiplier,constant; float priority; unsigned active; };
static Word constraint_create(Word cls,const char *selector,Word first,intptr_t attribute,intptr_t relation,Word second,intptr_t other,double multiplier,double constant) {
    if (!first || attribute<1 || attribute>20 || (second && (other<1 || other>20)) || relation < -1 || relation>1 || !isfinite(multiplier) || !isfinite(constant) ||
        (!second && attribute!=7 && attribute!=8)) stop("Invalid layout constraint",selector);
    if (attribute==11 || attribute==12 || (second && (other==11 || other==12))) stop("Baseline layout is not yet supported",selector);
    if (relation && (second || (attribute!=7 && attribute!=8))) stop("Unsupported inequality between layout items",selector);
    struct LayoutConstraint *constraint=(void *)host_allocate(cls);
    *constraint=(struct LayoutConstraint){.isa=cls,.first=first,.second=second,.attribute=attribute,.other_attribute=second ? other : 0,.relation=relation,.multiplier=multiplier,.constant=constant,.priority=1000};
    return (Word)constraint;
}
static Word layout_root(Word object) { while (view_state(object)->parent) object=view_state(object)->parent; return object; }
static void view_layout_deliver(void *context) {
    Word object=(Word)context; view_state(object)->layout_pending=0; view_layout_if_needed(object,"layoutIfNeeded");
}
static void view_request_layout(Word object,const char *selector) {
    Word root=layout_root(object); struct ViewState *state=view_state(root);
    if (!state->layout_managed && strcmp(selector,"setNeedsLayout") && strcmp(selector,"setNeedsUpdateConstraints")) return;
    state->needs_layout=1;
    view_state(object)->needs_layout=1;
    if (!state->layout_pending && !state->layout_running) { state->layout_pending=1; schedule_main_function(view_layout_deliver,(void *)root); }
}
static Word constraint_common_owner(struct LayoutConstraint *constraint) {
    if (!constraint->second) return constraint->first;
    for (Word a=constraint->first;a;a=view_state(a)->parent)
        for (Word b=constraint->second;b;b=view_state(b)->parent) if (a==b) return a;
    stop("Layout items have no common ancestor","NSLayoutConstraint"); return 0;
}
static void view_add_constraint(Word object,const char *selector,Word value) {
    if (!value || !class_inherits(read_word(value),external_class("_OBJC_CLASS_$_NSLayoutConstraint"))) stop("Invalid layout constraint",selector);
    struct LayoutConstraint *constraint=(void *)value;
    if (!view_descendant(constraint->first,selector,object) || (constraint->second && !view_descendant(constraint->second,selector,object))) stop("Layout owner is not an ancestor",selector);
    if (constraint->active && constraint->owner==object) return;
    if (constraint->active) sequence_remove((void *)view_state(constraint->owner)->constraints,selector,value);
    struct ViewState *owner=view_state(object);
    if (!owner->constraints) owner->constraints=sequence_empty(external_class("_OBJC_CLASS_$_NSMutableArray"));
    sequence_add((void *)owner->constraints,selector,value); constraint->owner=object; constraint->active=1;
    view_state(layout_root(object))->layout_managed=1; view_request_layout(object,selector);
}
static void view_remove_constraint(Word object,const char *selector,Word value) {
    struct ViewState *owner=view_state(object);
    if (!owner->constraints || !value || !sequence_contains((void *)owner->constraints,selector,value)) return;
    sequence_remove((void *)owner->constraints,selector,value); ((struct LayoutConstraint *)value)->active=0; ((struct LayoutConstraint *)value)->owner=0;
    view_request_layout(object,selector);
}
static void view_constraints_batch(Word object,const char *selector,struct Sequence *constraints) {
    if (!constraints) return;
    int remove=!strcmp(selector,"removeConstraints:");
    for (size_t i=0;i<constraints->count;++i) if (remove) view_remove_constraint(object,selector,constraints->items[i]); else view_add_constraint(object,selector,constraints->items[i]);
}
static void constraint_set_active(struct LayoutConstraint *constraint,const char *selector,Word active) {
    if (!constraint || !class_inherits(constraint->isa,external_class("_OBJC_CLASS_$_NSLayoutConstraint"))) stop("Invalid layout constraint",selector);
    if (!!active==constraint->active) return;
    if (active) view_add_constraint(constraint_common_owner(constraint),selector,(Word)constraint);
    else view_remove_constraint(constraint->owner,selector,(Word)constraint);
}
static void constraints_set_active(Word cls,const char *selector,struct Sequence *constraints) {
    if (constraints) for (size_t i=0;i<constraints->count;++i) constraint_set_active((void *)constraints->items[i],selector,!strcmp(selector,"activateConstraints:"));
}
static double constraint_constant(struct LayoutConstraint *constraint) { return constraint->constant; }
static void constraint_set_constant(struct LayoutConstraint *constraint,const char *selector,double value) {
    if (!isfinite(value)) stop("Invalid layout constant",selector);
    if (constraint->constant==value) return;
    constraint->constant=value; if (constraint->active) view_request_layout(constraint->owner,selector);
}
static float constraint_priority(struct LayoutConstraint *constraint) { return constraint->priority; }
static void constraint_set_priority(struct LayoutConstraint *constraint,const char *selector,float value) {
    if (!isfinite(value) || value<1 || value>1000) stop("Invalid layout priority",selector);
    if (constraint->priority==value) return;
    constraint->priority=value; if (constraint->active) view_request_layout(constraint->owner,selector);
}
static Word constraint_property(struct LayoutConstraint *constraint,const char *selector) {
    if (!strcmp(selector,"firstItem")) return constraint->first;
    if (!strcmp(selector,"secondItem")) return constraint->second;
    if (!strcmp(selector,"firstAttribute")) return constraint->attribute;
    if (!strcmp(selector,"secondAttribute")) return constraint->other_attribute;
    if (!strcmp(selector,"relation")) return constraint->relation;
    if (!strcmp(selector,"isActive")) return constraint->active;
    return constraint->identifier;
}
static double constraint_multiplier(struct LayoutConstraint *constraint) { return constraint->multiplier; }
static void constraint_set_identifier(struct LayoutConstraint *constraint,const char *selector,Word value) { constraint->identifier=value ? text_copy(value,"copy") : 0; }
static void layout_collect(Word object,struct Sequence *constraints,struct Sequence *objects) {
    struct ViewState *state=view_state(object); struct Sequence *installed=(void *)state->constraints,*children=(void *)state->children;
    for (size_t i=0;installed && i<installed->count;++i) {
        struct LayoutConstraint *constraint=(void *)installed->items[i]; if (!constraint->active) continue;
        sequence_add(constraints,"UILayout",(Word)constraint);
        Word items[]={constraint->first,constraint->second};
        for (unsigned j=0;j<2;++j) {
            Word chain[128]; unsigned count=0;
            for (Word item=items[j];item;item=view_state(item)->parent) { if (count==COUNT(chain)) stop("Layout ancestry exceeds supported size","UILayout"); chain[count++]=item; }
            while (count) { Word item=chain[--count]; if (!sequence_contains(objects,"UILayout",item)) sequence_add(objects,"UILayout",item); }
        }
    }
    for (size_t i=0;i<children->count;++i) layout_collect(children->items[i],constraints,objects);
}
static void layout_attribute(double *row,struct Sequence *objects,Word object,int attribute,double factor,int rtl,unsigned variables) {
    if (!object) return;
    size_t index=sequence_index(objects,"indexOfObjectIdenticalTo:",object);
    if (index==INT64_MAX) stop("Unlisted layout item","UILayout");
    struct GuestRect margins=view_state(object)->margins;
    double offset=0; int horizontal=attribute==1 || attribute==2 || attribute==5 || attribute==6 || attribute==9 || attribute==13 || attribute==14 || attribute==17 || attribute==18 || attribute==19;
    if (attribute==5) attribute=rtl ? 2 : 1; if (attribute==6) attribute=rtl ? 1 : 2;
    if (attribute==17) attribute=rtl ? 14 : 13; if (attribute==18) attribute=rtl ? 13 : 14;
    if (attribute==13) { attribute=1; offset=margins.y; } else if (attribute==14) { attribute=2; offset=-margins.height; }
    else if (attribute==15) { attribute=3; offset=margins.x; } else if (attribute==16) { attribute=4; offset=-margins.width; }
    else if (attribute==19) { attribute=9; offset=(margins.y-margins.height)*0.5; } else if (attribute==20) { attribute=10; offset=(margins.x-margins.width)*0.5; }
    if (attribute==11 || attribute==12) { attribute=4; }
    scene_attribute(row,index,attribute,factor); row[variables]-=offset*factor;
    if (attribute!=7 && attribute!=8) for (Word parent=view_state(object)->parent;parent;parent=view_state(parent)->parent) {
        size_t p=sequence_index(objects,"indexOfObjectIdenticalTo:",parent); if (p==INT64_MAX) break;
        row[p*4+(horizontal ? 0 : 1)]+=factor;
        row[variables]+=factor*(horizontal ? view_state(parent)->bounds.x : view_state(parent)->bounds.y);
    }
}
static int layout_priority_order(const void *a,const void *b) {
    const struct LayoutConstraint *x=*(const struct LayoutConstraint *const *)a,*y=*(const struct LayoutConstraint *const *)b;
    return x->priority>y->priority ? -1 : x->priority<y->priority ? 1 : 0;
}
static void layout_values(double *basis,double *values,unsigned variables) {
    for (unsigned i=variables;i>0;--i) { double *pivot=basis+(i-1)*(variables+1); values[i-1]=pivot[variables]; for (unsigned j=i;j<variables;++j) values[i-1]-=pivot[j]*values[j]; }
}
static void layout_prepare_callbacks(Word object) {
    struct ViewState *state=view_state(object); state->layout_notify|=state->needs_layout || state->needs_constraints; state->needs_layout=0;
    if (state->needs_constraints) { state->needs_constraints=0; Method update=find_implementation(object,"updateConstraints"); if (update) update(object,"updateConstraints",0,0,0,0); }
    struct Sequence *children=(void *)state->children;
    for (size_t i=0;i<children->count;++i) layout_prepare_callbacks(children->items[i]);
}
static void layout_finish_callbacks(Word object) {
    struct ViewState *state=view_state(object);
    if (state->layout_notify) { state->layout_notify=0; Method layout=find_implementation(object,"layoutSubviews"); if (layout) layout(object,"layoutSubviews",0,0,0,0); }
    struct Sequence *children=(void *)state->children;
    for (size_t i=0;i<children->count;++i) layout_finish_callbacks(children->items[i]);
}
static void view_layout_if_needed(Word object,const char *selector) {
    Word root=layout_root(object); struct ViewState *state=view_state(root);
    if (state->layout_running || !state->needs_layout) return;
    state->layout_running=1; state->layout_notify=1;
    layout_prepare_callbacks(root);
    Word controller=state->controller; Method lifecycle=controller ? find_implementation(controller,"viewWillLayoutSubviews") : NULL;
    if (lifecycle) lifecycle(controller,"viewWillLayoutSubviews",0,0,0,0);
    state->needs_layout=0;
    struct Sequence *constraints=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray")),*objects=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
    sequence_add(objects,selector,root); layout_collect(root,constraints,objects);
    if (objects->count>128) stop("Layout component exceeds supported size",selector);
    unsigned variables=objects->count*4; size_t cells=(size_t)variables*(variables+1);
    double *basis=calloc(cells,sizeof(double)),*required=calloc(cells,sizeof(double)),*row=calloc(variables+1,sizeof(double)),*values=calloc(variables,sizeof(double));
    double *lower=malloc(variables*sizeof(double)),*upper=malloc(variables*sizeof(double));
    struct GuestSize *intrinsic=malloc(objects->count*sizeof(*intrinsic)); const char *failure=NULL;
    if (!basis || !required || !row || !values || !lower || !upper || !intrinsic) { failure="Layout allocation failed"; goto cleanup; }
    for (size_t i=0;i<objects->count;++i) intrinsic[i]=ui_view_intrinsic_size(objects->items[i]);
#define INSERT_LAYOUT_ROW(storage,mandatory) do { if (!layout_equation(storage,variables,row,mandatory)) { failure="Inconsistent required layout equation"; goto cleanup; } } while (0)
    for (unsigned i=0;i<variables;++i) { lower[i]=-INFINITY; upper[i]=INFINITY; }
    JNIEnv *env=attached_environment(selector); static jmethodID direction;
    if (!direction) { jclass cls=(*env)->GetObjectClass(env,activity); direction=(*env)->GetMethodID(env,cls,"guestLayoutDirection","()I"); (*env)->DeleteLocalRef(env,cls); }
    int rtl=(*env)->CallIntMethod(env,activity,direction)!=0;
    double fixed[]={0,0,state->bounds.width,state->bounds.height};
    int self_sizing=0;
    for (size_t i=0;i<constraints->count;++i) { struct LayoutConstraint *constraint=(void *)constraints->items[i]; self_sizing|=constraint->first==root && !constraint->second; }
    for (unsigned i=0;i<(self_sizing ? 2U : 4U);++i) { memset(row,0,(variables+1)*sizeof(double)); row[i]=1; row[variables]=fixed[i]; INSERT_LAYOUT_ROW(required,1); }
    qsort(constraints->items,constraints->count,sizeof(Word),layout_priority_order);
    for (size_t i=0;i<constraints->count;++i) {
        struct LayoutConstraint *constraint=(void *)constraints->items[i];
        if (constraint->relation) {
            if (constraint->priority!=1000) { failure="Optional layout inequalities are not yet supported"; goto cleanup; }
            size_t index=sequence_index(objects,"indexOfObjectIdenticalTo:",constraint->first)*4+(constraint->attribute==7 ? 2 : 3);
            if (constraint->relation>0) lower[index]=fmax(lower[index],constraint->constant); else upper[index]=fmin(upper[index],constraint->constant);
        } else if (constraint->priority==1000) {
            memset(row,0,(variables+1)*sizeof(double)); row[variables]=constraint->constant;
            layout_attribute(row,objects,constraint->first,constraint->attribute,1,rtl,variables);
            layout_attribute(row,objects,constraint->second,constraint->other_attribute,-constraint->multiplier,rtl,variables);
            INSERT_LAYOUT_ROW(required,1);
        }
    }
    for (unsigned pass=0;pass<=variables;++pass) {
        memcpy(basis,required,cells*sizeof(double));
        for (size_t i=0;i<constraints->count;++i) {
            struct LayoutConstraint *constraint=(void *)constraints->items[i]; if (constraint->relation || constraint->priority==1000) continue;
            memset(row,0,(variables+1)*sizeof(double)); row[variables]=constraint->constant;
            layout_attribute(row,objects,constraint->first,constraint->attribute,1,rtl,variables);
            layout_attribute(row,objects,constraint->second,constraint->other_attribute,-constraint->multiplier,rtl,variables);
            INSERT_LAYOUT_ROW(basis,0);
        }
        for (size_t i=0;i<objects->count;++i) {
            struct ViewState *v=view_state(objects->items[i]);
            double fallback[]={i ? v->frame.x : 0,i ? v->frame.y : 0,intrinsic[i].width>=0 && !v->translates ? intrinsic[i].width : v->frame.width,intrinsic[i].height>=0 && !v->translates ? intrinsic[i].height : v->frame.height};
            for (unsigned j=0;j<4;++j) { memset(row,0,(variables+1)*sizeof(double)); row[i*4+j]=1; row[variables]=fallback[j]; INSERT_LAYOUT_ROW(basis,0); }
        }
        layout_values(basis,values,variables); int violated=0;
        for (unsigned i=0;i<variables;++i) {
            if (lower[i]>upper[i]) { failure="Inconsistent required layout bounds"; goto cleanup; }
            double bounded=fmin(upper[i],fmax(lower[i],values[i]));
            if (fabs(bounded-values[i])>1e-7) { memset(row,0,(variables+1)*sizeof(double)); row[i]=1; row[variables]=bounded; INSERT_LAYOUT_ROW(required,1); violated=1; }
        }
        if (!violated) break; if (pass==variables) { failure="Required layout bounds cannot be satisfied"; goto cleanup; }
    }
    if (self_sizing) {
        double width=state->bounds.width,height=state->bounds.height;
        state->frame.width=state->bounds.width=fmax(0,values[2]); state->frame.height=state->bounds.height=fmax(0,values[3]); view_resize_children(state,width,height);
    }
    for (size_t i=1;i<objects->count;++i) {
        struct ViewState *v=view_state(objects->items[i]); double width=v->bounds.width,height=v->bounds.height;
        struct GuestRect frame={values[i*4],values[i*4+1],fmax(0,values[i*4+2]),fmax(0,values[i*4+3])}; v->layout_notify|=memcmp(&v->frame,&frame,sizeof(frame))!=0;
        v->frame=frame; v->bounds.width=v->frame.width; v->bounds.height=v->frame.height;
        view_resize_children(v,width,height);
    }
cleanup:
#undef INSERT_LAYOUT_ROW
    free(basis); free(required); free(row); free(values); free(lower); free(upper); free(intrinsic);
    free(constraints->items); free(constraints); free(objects->items); free(objects);
    if (failure) { state->layout_running=0; stop(failure,selector); }
    layout_finish_callbacks(root);
    lifecycle=controller ? find_implementation(controller,"viewDidLayoutSubviews") : NULL; if (lifecycle) lifecycle(controller,"viewDidLayoutSubviews",0,0,0,0);
    view_sync_subtree(root); state->layout_running=0;
    if (state->needs_layout) view_request_layout(root,selector);
}
static void view_set_translates(Word object,const char *selector,Word value) { struct ViewState *state=view_state(object); if (state->translates==!!value) return; state->translates=value!=0; view_request_layout(object,selector); }
static Word view_get_translates(Word object) { return view_state(object)->translates; }
static void view_set_margins(Word object,const char *selector,struct GuestRect value) { view_state(object)->margins=value; view_request_layout(object,selector); }
static struct GuestRect view_get_margins(Word object) { return view_state(object)->margins; }
static void view_needs_constraints(Word object,const char *selector) { view_state(object)->needs_constraints=1; view_request_layout(object,selector); }
static Word view_get_constraints(Word object) { Word value=view_state(object)->constraints; return value ? sequence_copy((void *)value,"copy") : sequence_empty(external_class("_OBJC_CLASS_$_NSArray")); }
struct VisualLayout { const char *cursor,*selector; struct GuestMap *metrics,*views; struct Sequence *constraints,*items; Word cls,owner; int vertical,rtl; };
static void visual_skip(struct VisualLayout *layout) { while (*layout->cursor==' ' || *layout->cursor=='\t') ++layout->cursor; }
static Word visual_lookup(struct GuestMap *map,const char *name,const char *selector) { return map ? map_get(map,selector,copy_string(name)) : 0; }
static void visual_name(struct VisualLayout *layout,char *output,size_t capacity) {
    visual_skip(layout); size_t count=0;
    while ((*layout->cursor>='a' && *layout->cursor<='z') || (*layout->cursor>='A' && *layout->cursor<='Z') || (*layout->cursor>='0' && *layout->cursor<='9') || *layout->cursor=='_') {
        if (count+1>=capacity) stop("Visual format identifier is too long",layout->selector);
        output[count++]=*layout->cursor++;
    }
    output[count]=0; if (!count) stop("Expected visual format identifier",layout->selector);
}
static double visual_number(struct VisualLayout *layout) {
    visual_skip(layout); char *end=NULL; double value=strtod(layout->cursor,&end);
    if (end!=layout->cursor) { layout->cursor=end; if (!isfinite(value)) stop("Invalid visual format number",layout->selector); return value; }
    char name[256]; visual_name(layout,name,sizeof(name)); Word metric=visual_lookup(layout->metrics,name,layout->selector);
    if (!metric) stop("Missing visual format metric",name); return numeric_double((void *)metric);
}
static void visual_constraint(struct VisualLayout *layout,Word first,int attribute,int relation,Word second,int other,double constant,float priority) {
    Word value=constraint_create(layout->cls,layout->selector,first,attribute,relation,second,other,1,constant);
    ((struct LayoutConstraint *)value)->priority=priority; sequence_add(layout->constraints,layout->selector,value);
}
static void visual_predicate(struct VisualLayout *layout,Word item) {
    visual_skip(layout); int relation=0;
    if (!strncmp(layout->cursor,">=",2)) { relation=1; layout->cursor+=2; }
    else if (!strncmp(layout->cursor,"<=",2)) { relation=-1; layout->cursor+=2; }
    else if (!strncmp(layout->cursor,"==",2)) layout->cursor+=2;
    visual_skip(layout); const char *start=layout->cursor; Word second=0; double constant=0; float priority=1000;
    if ((*start>='a' && *start<='z') || (*start>='A' && *start<='Z') || *start=='_') {
        char name[256]; visual_name(layout,name,sizeof(name)); second=visual_lookup(layout->views,name,layout->selector);
        if (!second) { layout->cursor=start; constant=visual_number(layout); }
    } else constant=visual_number(layout);
    visual_skip(layout); if (*layout->cursor=='@') { ++layout->cursor; priority=visual_number(layout); }
    if (priority<1 || priority>1000) stop("Invalid visual format priority",layout->selector);
    visual_constraint(layout,item,layout->vertical ? 8 : 7,relation,second,layout->vertical ? 8 : 7,constant,priority);
}
static Word visual_item(struct VisualLayout *layout) {
    visual_skip(layout);
    if (*layout->cursor=='|') { ++layout->cursor; if (!layout->owner) stop("Visual format requires a common superview",layout->selector); return layout->owner; }
    if (*layout->cursor++!='[') stop("Expected visual format view",layout->selector);
    char name[256]; visual_name(layout,name,sizeof(name)); Word item=visual_lookup(layout->views,name,layout->selector);
    if (!item) stop("Missing visual format view",name);
    sequence_add(layout->items,layout->selector,item); visual_skip(layout);
    if (*layout->cursor=='(') {
        ++layout->cursor;
        for (;;) { visual_predicate(layout,item); visual_skip(layout); if (*layout->cursor!=',') break; ++layout->cursor; }
        if (*layout->cursor++!=')') stop("Unclosed visual format predicate",layout->selector);
    }
    visual_skip(layout); if (*layout->cursor++!=']') stop("Unclosed visual format view",layout->selector); return item;
}
static Word constraint_visual_format(Word cls,const char *selector,Word format,Word options,struct GuestMap *metrics,struct GuestMap *views) {
    if (!format || !views) stop("Missing visual format arguments",selector);
    unsigned align=(unsigned)options&0x7fe,mode=(unsigned)options&0x30000;
    if (options&~(Word)0x307fe || mode==0x30000) stop("Unsupported visual format options",selector);
    struct VisualLayout layout={.cursor=string_bytes(format,selector),.selector=selector,.metrics=metrics,.views=views,.cls=cls,
        .constraints=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray")),.items=(void *)sequence_empty(external_class("_OBJC_CLASS_$_NSArray"))};
    int first=1;
    for (const char *scan=layout.cursor;*scan;++scan) if (*scan=='[') {
        const char *saved=layout.cursor; layout.cursor=scan+1; char name[256]; visual_name(&layout,name,sizeof(name)); layout.cursor=saved;
        Word item=visual_lookup(views,name,selector); if (!item) stop("Missing visual format view",name);
        if (first) { layout.owner=view_state(item)->parent; first=0; }
        else if (layout.owner) {
            Word common=0;
            for (Word a=layout.owner;a && !common;a=view_state(a)->parent) for (Word b=item;b;b=view_state(b)->parent) if (a==b) { common=a; break; }
            layout.owner=common;
        }
    }
    visual_skip(&layout);
    if (layout.cursor[0]=='H' && layout.cursor[1]==':') layout.cursor+=2;
    else if (layout.cursor[0]=='V' && layout.cursor[1]==':') { layout.vertical=1; layout.cursor+=2; }
    JNIEnv *env=attached_environment(selector); jclass type=(*env)->GetObjectClass(env,activity); jmethodID direction=(*env)->GetMethodID(env,type,"guestLayoutDirection","()I");
    layout.rtl=!layout.vertical && (mode==0x20000 || (!mode && (*env)->CallIntMethod(env,activity,direction)!=0)); (*env)->DeleteLocalRef(env,type);
    Word previous=visual_item(&layout); int leading=layout.vertical ? 3 : layout.rtl ? 2 : 1,trailing=layout.vertical ? 4 : layout.rtl ? 1 : 2;
    visual_skip(&layout);
    while (*layout.cursor) {
        double spacing=0; visual_skip(&layout);
        if (*layout.cursor=='-') {
            ++layout.cursor; visual_skip(&layout); spacing=8;
            if (*layout.cursor!='[' && *layout.cursor!='|') {
                int parenthesized=*layout.cursor=='('; if (parenthesized) ++layout.cursor;
                spacing=visual_number(&layout); visual_skip(&layout);
                if (parenthesized && *layout.cursor++!=')') stop("Unclosed visual format spacing",selector);
                if (*layout.cursor++!='-') stop("Expected visual format spacing separator",selector);
            }
        }
        Word next=visual_item(&layout);
        double constant=spacing*(layout.rtl ? -1 : 1);
        if (next==layout.owner) visual_constraint(&layout,previous,trailing,0,next,trailing,-constant,1000);
        else visual_constraint(&layout,next,leading,0,previous,previous==layout.owner ? leading : trailing,constant,1000);
        previous=next; visual_skip(&layout);
    }
    if (layout.items->count>1) for (unsigned attribute=1;attribute<=10;++attribute) if (align&(1U<<attribute))
        for (size_t i=1;i<layout.items->count;++i) visual_constraint(&layout,layout.items->items[i],attribute,0,layout.items->items[0],attribute,0,1000);
    free(layout.items->items); free(layout.items); return (Word)layout.constraints;
}
static void load_storyboard_view(void) {
    plist_t scene=NULL;
    if (plist_from_json(storyboard_scene,sizeof(storyboard_scene)-1,&scene)!=PLIST_ERR_SUCCESS) stop("Invalid generated storyboard","UIKit");
    plist_t views=plist_dict_get_item(scene,"views"),constraints=plist_dict_get_item(scene,"constraints");
    unsigned count=plist_array_get_size(views),variables=count*4;
    if (!count || count>32) stop("Unexpected startup scene size","UIKit");
    Word objects[32]={0};
    double *basis=calloc(variables*(variables+1),sizeof(double)),*row=calloc(variables+1,sizeof(double)),*values=calloc(variables,sizeof(double));
    if (!basis || !row || !values) stop("Allocation failed","Storyboard constraints");
    struct GuestRect screen=screen_bounds();
    double root_values[]={0,0,screen.width,screen.height};
    for (unsigned i=0;i<4;++i) { memset(row,0,(variables+1)*sizeof(double)); row[i]=1; row[variables]=root_values[i]; scene_equation(basis,variables,row,1); }
    for (int priority=1000;priority>=1;--priority) for (unsigned i=0;i<plist_array_get_size(constraints);++i) {
        plist_t constraint=plist_array_get_item(constraints,i);
        if ((int)scene_number(constraint,"priority",1000)!=priority) continue;
        if (scene_number(constraint,"relation",0)!=0) stop("Unsupported startup constraint relation","Storyboard");
        memset(row,0,(variables+1)*sizeof(double));
        scene_attribute(row,(int)scene_number(constraint,"first",-1),(int)scene_number(constraint,"attribute",0),1);
        scene_attribute(row,(int)scene_number(constraint,"second",-1),(int)scene_number(constraint,"otherAttribute",0),-scene_number(constraint,"multiplier",1));
        row[variables]=scene_number(constraint,"constant",0);
        scene_equation(basis,variables,row,priority==1000);
    }
    for (unsigned i=0;i<count;++i) {
        plist_t node=plist_array_get_item(views,i),frame=plist_dict_get_item(node,"frame");
        for (unsigned j=0;j<4;++j) {
            double fallback=0; plist_t item=plist_array_get_item(frame,j);
            if (plist_get_node_type(item)==PLIST_REAL) plist_get_real_val(item,&fallback);
            else { int64_t integer=0; plist_get_int_val(item,&integer); fallback=(double)integer; }
            memset(row,0,(variables+1)*sizeof(double)); row[i*4+j]=1; row[variables]=fallback;
            scene_equation(basis,variables,row,0);
        }
    }
    for (unsigned i=variables;i>0;--i) {
        double *pivot=basis+(i-1)*(variables+1);
        values[i-1]=pivot[variables];
        for (unsigned j=i;j<variables;++j) values[i-1]-=pivot[j]*values[j];
    }
    for (unsigned i=0;i<count;++i) {
        plist_t node=plist_array_get_item(views,i);
        const char *name=plist_get_string_ptr(plist_dict_get_item(node,"class"),NULL);
        Word cls=class_from_name(name);
        if (!cls) stop("Unknown storyboard view class",name);
        objects[i]=i ? host_allocate(cls) : (Word)&root_view;
        *(Word *)objects[i]=cls;
        struct GuestRect frame={values[i*4],values[i*4+1],values[i*4+2],values[i*4+3]};
        view_init_frame(objects[i],"Storyboard",frame);
        struct ViewState *state=view_state(objects[i]);
        state->tag=(long)scene_number(node,"tag",0); state->hidden=(int)scene_number(node,"hidden",0); state->alpha=scene_number(node,"alpha",1);
        state->interaction=(int)scene_number(node,"interaction",1); state->content_mode=(long)scene_number(node,"contentMode",0); state->autoresizing=(long)scene_number(node,"autoresizing",0);
        state->image_name=copy_string(plist_get_string_ptr(plist_dict_get_item(node,"image"),NULL));
        plist_t background=plist_dict_get_item(node,"background"); double rgba[4];
        if (plist_get_node_type(background)!=PLIST_ARRAY) background=NULL;
        for (unsigned j=0;j<4;++j) { rgba[j]=0; if (background) plist_get_real_val(plist_array_get_item(background,j),rgba+j); }
        ui_view_load_content(objects[i],string_bytes(state->image_name,"Storyboard image"),background ? rgba : NULL);
        int parent=(int)scene_number(node,"parent",-1);
        if (parent>=0) {
            if (parent!=0 || (unsigned)parent>=i) stop("Unsupported nested startup layout","Storyboard");
            view_add(objects[parent],"Storyboard",objects[i]);
        }
    }
    view_state(root_controller)->view=objects[0];
    view_init_frame((Word)&main_window,"Window",screen);
    window_set_controller((Word)&main_window,"Window",root_controller);
    LOG("Original storyboard restored: %u views, %u constraints, original images loaded",count,plist_array_get_size(constraints));
    free(basis); free(row); free(values); plist_free(scene);
}
static void load_application_view(void) {
    load_storyboard_view();
    const char *selector="viewDidLoad";
    Method load=find_implementation(root_controller,selector);
    if (!load) stop("Missing viewDidLoad",ROOT_CONTROLLER);
    snprintf(stage,sizeof(stage),"Original %s viewDidLoad 0x%lx",ROOT_CONTROLLER,(Word)load-aslr+GUEST_IMAGE_BASE);
    LOG("%s",stage);
    load(root_controller,selector,0,0,0,0);
    LOG("Original root viewDidLoad returned");
}
static void finish_application_lifecycle(void) {
    const char *selectors[]={"viewWillAppear:","viewDidLayoutSubviews","viewDidAppear:"};
    for (unsigned i=0;i<COUNT(selectors);++i) {
        Method callback=find_implementation(root_controller,selectors[i]);
        if (callback) callback(root_controller,selectors[i],0,0,0,0);
    }
    application_lifecycle_start();
}

static Method views_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver) return NULL;
    Word cls=class_receiver ? object : read_word(object);
    const char *base=external_base(cls);
    int view=class_inherits(cls,external_class("_OBJC_CLASS_$_UIView")) || (base && !strcmp(base,"_OBJC_CLASS_$__UILayoutGuide"));
    int navigation=class_inherits(cls,external_class("_OBJC_CLASS_$_UINavigationController"));
    int controller=class_inherits(cls,external_class("_OBJC_CLASS_$_UIViewController"));
    if (class_inherits(cls,external_class("_OBJC_CLASS_$_NSLayoutConstraint"))) {
        if (class_receiver) {
            if (!strcmp(selector,"constraintWithItem:attribute:relatedBy:toItem:attribute:multiplier:constant:")) return (Method)constraint_create;
            if (!strcmp(selector,"constraintsWithVisualFormat:options:metrics:views:")) return (Method)constraint_visual_format;
            if (!strcmp(selector,"activateConstraints:") || !strcmp(selector,"deactivateConstraints:")) return (Method)constraints_set_active;
        } else {
            if (!strcmp(selector,"constant")) return (Method)constraint_constant;
            if (!strcmp(selector,"setConstant:")) return (Method)constraint_set_constant;
            if (!strcmp(selector,"priority")) return (Method)constraint_priority;
            if (!strcmp(selector,"setPriority:")) return (Method)constraint_set_priority;
            if (!strcmp(selector,"multiplier")) return (Method)constraint_multiplier;
            if (!strcmp(selector,"setActive:")) return (Method)constraint_set_active;
            if (!strcmp(selector,"setIdentifier:")) return (Method)constraint_set_identifier;
            if (!strcmp(selector,"firstItem") || !strcmp(selector,"secondItem") || !strcmp(selector,"firstAttribute") || !strcmp(selector,"secondAttribute") || !strcmp(selector,"relation") || !strcmp(selector,"isActive") || !strcmp(selector,"identifier")) return (Method)constraint_property;
        }
    }
    if (class_receiver) {
        if (view && !strcmp(selector,"layerClass")) return (Method)default_layer_class;
        if (view && !strcmp(selector,"animateWithDuration:delay:options:animations:completion:")) return (Method)view_animate;
        if (view && !strcmp(selector,"animateWithDuration:animations:completion:")) return (Method)view_animate_plain;
        return NULL;
    }
    if (navigation) {
        if (!strcmp(selector,"navigationBar")) return (Method)controller_navigation_bar;
        if (!strcmp(selector,"pushViewController:animated:")) return (Method)navigation_push;
        if (!strcmp(selector,"popViewControllerAnimated:")) return (Method)navigation_pop;
        if (!strcmp(selector,"viewControllers")) return (Method)view_subviews;
        if (!strcmp(selector,"setViewControllers:animated:")) return (Method)navigation_set_controllers;
        if (!strcmp(selector,"setViewControllers:")) return (Method)navigation_set_controllers_plain;
        if (!strcmp(selector,"topViewController")) return (Method)navigation_top;
        if (!strcmp(selector,"visibleViewController")) return (Method)navigation_visible;
        if (!strcmp(selector,"initWithRootViewController:")) return (Method)navigation_init_root;
    }
    if (base && !strcmp(base,"_OBJC_CLASS_$_UINavigationItem")) {
        if (!strcmp(selector,"initWithTitle:")) return (Method)navigation_item_init;
        if (!strcmp(selector,"title")) return (Method)navigation_item_title;
        if (!strcmp(selector,"setTitle:")) return (Method)navigation_item_set_title;
        const char *getters[]={"leftBarButtonItem","rightBarButtonItem","backBarButtonItem","titleView","prompt"};
        const char *setters[]={"setLeftBarButtonItem:","setLeftBarButtonItem:animated:","setRightBarButtonItem:","setRightBarButtonItem:animated:","setBackBarButtonItem:","setTitleView:","setPrompt:"};
        for (unsigned i=0;i<COUNT(getters);++i) if (!strcmp(selector,getters[i])) return (Method)navigation_item_value;
        for (unsigned i=0;i<COUNT(setters);++i) if (!strcmp(selector,setters[i])) return (Method)navigation_item_set_value;
    }
    if (base && !strcmp(base,"_OBJC_CLASS_$_UINavigationBar")) {
        if (!strcmp(selector,"barStyle")) return (Method)navigation_bar_style;
        if (!strcmp(selector,"setBarStyle:")) return (Method)navigation_bar_set_style;
    }
    if (controller) {
        if (!strcmp(selector,"edgesForExtendedLayout") || !strcmp(selector,"automaticallyAdjustsScrollViewInsets") || !strcmp(selector,"definesPresentationContext") || !strcmp(selector,"extendedLayoutIncludesOpaqueBars") ||
            !strcmp(selector,"clearsSelectionOnViewWillAppear") || !strcmp(selector,"isMovingToParentViewController") || !strcmp(selector,"isMovingFromParentViewController")) return (Method)controller_layout_property;
        if (!strcmp(selector,"setEdgesForExtendedLayout:") || !strcmp(selector,"setAutomaticallyAdjustsScrollViewInsets:") || !strcmp(selector,"setDefinesPresentationContext:") || !strcmp(selector,"setExtendedLayoutIncludesOpaqueBars:") || !strcmp(selector,"setClearsSelectionOnViewWillAppear:")) return (Method)controller_set_layout_property;
        if (!strcmp(selector,"splitViewController")) return (Method)controller_split_view;
        if (!strcmp(selector,"willMoveToParentViewController:") || !strcmp(selector,"didMoveToParentViewController:")) return (Method)controller_move;
        if (!strcmp(selector,"init")) return class_inherits(cls,external_class("_OBJC_CLASS_$_UIAlertController")) ? (Method)ui_alert_initialize : (Method)controller_initialize;
        if (!strcmp(selector,"initWithNibName:bundle:")) return (Method)controller_init_nib;
        if (class_inherits(cls,external_class("_OBJC_CLASS_$_UITableViewController"))) {
            if (!strcmp(selector,"initWithStyle:")) return (Method)controller_init_style;
            if (!strcmp(selector,"tableView")) return (Method)controller_table_view;
            if (!strcmp(selector,"setTableView:")) return (Method)controller_set_view;
        }
        if (!strcmp(selector,"title")) return (Method)navigation_item_title;
        if (!strcmp(selector,"setTitle:")) return (Method)controller_set_title;
        if (!strcmp(selector,"navigationItem")) return (Method)controller_navigation_item;
        if (!strcmp(selector,"nibName")) return (Method)controller_nib_name;
        if (!strcmp(selector,"nibBundle")) return (Method)controller_nib_bundle;
        if (!strcmp(selector,"setModalInPopover:")) return (Method)controller_set_modal_popover;
        if (!strcmp(selector,"isModalInPopover")) return (Method)controller_modal_popover;
        if (!strcmp(selector,"setModalPresentationStyle:")) return (Method)controller_modal_style;
        if (!strcmp(selector,"parentViewController")) return (Method)view_superview;
        if (!strcmp(selector,"childViewControllers")) return (Method)view_subviews;
        if (!strcmp(selector,"addChildViewController:")) return (Method)controller_add_child;
        if (!strcmp(selector,"removeFromParentViewController")) return (Method)controller_remove_parent;
        if (!strcmp(selector,"isBeingPresented")) return (Method)controller_is_presenting;
        if (!strcmp(selector,"isBeingDismissed")) return (Method)controller_is_dismissing;
        if (!strcmp(selector,"modalPresentationStyle")) return (Method)controller_get_modal_style;
        if (!strcmp(selector,"setModalTransitionStyle:")) return (Method)controller_transition_style;
        if (!strcmp(selector,"modalTransitionStyle")) return (Method)controller_get_transition_style;
        if (!strcmp(selector,"popoverPresentationController")) return (Method)controller_popover;
        if (!strcmp(selector,"presentedViewController")) return (Method)controller_presented;
        if (!strcmp(selector,"presentingViewController")) return (Method)controller_presenting;
        if (!strcmp(selector,"view")) return (Method)controller_view;
        if (!strcmp(selector,"viewIfLoaded")) return (Method)controller_loaded_view;
        if (!strcmp(selector,"setView:")) return (Method)controller_set_view;
        if (!strcmp(selector,"isViewLoaded")) return (Method)controller_loaded;
        for (unsigned i=0;i<COUNT(view_methods);++i) if (!strcmp(selector,view_methods[i].name)) return view_methods[i].implementation;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_UIPopoverPresentationController")) {
        if (!strcmp(selector,"setSourceView:")) return (Method)popover_set_source;
        if (!strcmp(selector,"sourceView")) return (Method)popover_source;
        if (!strcmp(selector,"setSourceRect:")) return (Method)view_set_frame;
        if (!strcmp(selector,"sourceRect")) return (Method)view_frame;
        if (!strcmp(selector,"setDelegate:")) return (Method)popover_set_delegate;
        if (!strcmp(selector,"delegate")) return (Method)popover_delegate;
        if (!strcmp(selector,"setPermittedArrowDirections:")) return (Method)popover_set_arrows;
        if (!strcmp(selector,"permittedArrowDirections")) return (Method)popover_arrows;
    }
    if (!strcmp(receiver,"_OBJC_CLASS_$_CALayer")) {
        if (!strcmp(selector,"setMasksToBounds:")) return (Method)layer_set_masks;
        if (!strcmp(selector,"masksToBounds")) return (Method)view_clips;
        if (!strcmp(selector,"setCornerRadius:")) return (Method)layer_set_corner;
        if (!strcmp(selector,"cornerRadius")) return (Method)layer_corner;
        if (!strcmp(selector,"setZPosition:")) return (Method)layer_set_z;
        if (!strcmp(selector,"zPosition")) return (Method)layer_z;
    }
    if (!view) return NULL;
    if (!strcmp(selector,"drawRect:") && !class_inherits(cls,external_class("_OBJC_CLASS_$_UILabel"))) return (Method)view_draw_rect;
    if (!strcmp(selector,"setNeedsDisplay")) return (Method)ui_view_request_display;
    if (!strcmp(selector,"setOpaque:")) return (Method)view_set_opaque;
    if (!strcmp(selector,"isOpaque")) return (Method)view_get_opaque;
    if (class_inherits(cls,external_class("_OBJC_CLASS_$_UIWindow"))) {
        if (!strcmp(selector,"setWindowLevel:")) return (Method)window_set_level;
        if (!strcmp(selector,"windowLevel")) return (Method)window_get_level;
    }
    if (!strcmp(selector,"addConstraint:")) return (Method)view_add_constraint;
    if (!strcmp(selector,"removeConstraint:")) return (Method)view_remove_constraint;
    if (!strcmp(selector,"addConstraints:") || !strcmp(selector,"removeConstraints:")) return (Method)view_constraints_batch;
    if (!strcmp(selector,"constraints")) return (Method)view_get_constraints;
    if (!strcmp(selector,"layoutIfNeeded")) return (Method)view_layout_if_needed;
    if (!strcmp(selector,"setNeedsLayout")) return (Method)view_request_layout;
    if (!strcmp(selector,"setNeedsUpdateConstraints")) return (Method)view_needs_constraints;
    if (!strcmp(selector,"setTranslatesAutoresizingMaskIntoConstraints:")) return (Method)view_set_translates;
    if (!strcmp(selector,"translatesAutoresizingMaskIntoConstraints")) return (Method)view_get_translates;
    if (!strcmp(selector,"setLayoutMargins:")) return (Method)view_set_margins;
    if (!strcmp(selector,"layoutMargins")) return (Method)view_get_margins;
    if (!strcmp(selector,"init")) return (Method)view_initialize;
    if (!strcmp(selector,"setClipsToBounds:")) return (Method)view_set_clips;
    if (!strcmp(selector,"clipsToBounds")) return (Method)view_clips;
    if (!strcmp(selector,"initWithFrame:")) return (Method)view_init_frame;
    if (!strcmp(selector,"isDescendantOfView:")) return (Method)view_descendant;
    if (!strcmp(selector,"window")) return (Method)view_window;
    if (!strcmp(selector,"setBackgroundColor:")) return (Method)view_background;
    if (!strcmp(selector,"backgroundColor")) return (Method)view_get_background;
    if (!strcmp(selector,"frame")) return (Method)view_frame;
    if (!strcmp(selector,"bounds")) return (Method)view_bounds;
    if (!strcmp(selector,"center")) return (Method)view_center;
    if (!strcmp(selector,"setFrame:")) return (Method)view_set_frame;
    if (!strcmp(selector,"setBounds:")) return (Method)view_set_bounds;
    if (!strcmp(selector,"setCenter:")) return (Method)view_set_center;
    if (!strcmp(selector,"setHidden:")) return (Method)view_hidden;
    if (!strcmp(selector,"isHidden")) return (Method)view_is_hidden;
    if (!strcmp(selector,"setAlpha:")) return (Method)view_alpha;
    if (!strcmp(selector,"alpha")) return (Method)view_get_alpha;
    if (!strcmp(selector,"setContentScaleFactor:")) return (Method)view_scale;
    if (!strcmp(selector,"contentScaleFactor")) return (Method)view_get_scale;
    if (!strcmp(selector,"setUserInteractionEnabled:")) return (Method)view_interaction;
    if (!strcmp(selector,"isUserInteractionEnabled")) return (Method)view_get_interaction;
    if (!strcmp(selector,"setMultipleTouchEnabled:")) return (Method)view_multitouch;
    if (!strcmp(selector,"isMultipleTouchEnabled")) return (Method)view_get_multitouch;
    if (!strcmp(selector,"setExclusiveTouch:")) return (Method)view_exclusive_touch;
    if (!strcmp(selector,"isExclusiveTouch")) return (Method)view_get_exclusive_touch;
    if (!strcmp(selector,"setIsAccessibilityElement:") || !strcmp(selector,"setAccessibilityLabel:")) return (Method)view_accessibility;
    if (!strcmp(selector,"isAccessibilityElement") || !strcmp(selector,"accessibilityLabel")) return (Method)view_get_accessibility;
    if (!strcmp(selector,"setTag:")) return (Method)view_tag;
    if (!strcmp(selector,"tag")) return (Method)view_get_tag;
    if (!strcmp(selector,"viewWithTag:")) return (Method)view_find_tag;
    if (!strcmp(selector,"setContentMode:")) return (Method)view_content_mode;
    if (!strcmp(selector,"contentMode")) return (Method)view_get_content_mode;
    if (!strcmp(selector,"setAutoresizingMask:")) return (Method)view_set_autoresizing;
    if (!strcmp(selector,"autoresizingMask")) return (Method)view_autoresizing;
    if (!strcmp(selector,"setAutoresizesSubviews:")) return (Method)view_set_autoresizes;
    if (!strcmp(selector,"autoresizesSubviews")) return (Method)view_autoresizes;
    if (!strcmp(selector,"removeFromSuperview")) return (Method)view_remove;
    if (!strcmp(selector,"addSubview:")) return (Method)view_add;
    if (!strcmp(selector,"insertSubview:atIndex:")) return (Method)view_insert_index;
    if (!strcmp(selector,"insertSubview:aboveSubview:") || !strcmp(selector,"insertSubview:belowSubview:")) return (Method)view_insert_relative;
    if (!strcmp(selector,"bringSubviewToFront:") || !strcmp(selector,"sendSubviewToBack:")) return (Method)view_reorder;
    if (!strcmp(selector,"exchangeSubviewAtIndex:withSubviewAtIndex:")) return (Method)view_exchange;
    if (!strcmp(selector,"superview")) return (Method)view_superview;
    if (!strcmp(selector,"subviews")) return (Method)view_subviews;
    if (!strcmp(selector,"layer")) return (Method)view_layer;
    if (!strcmp(selector,"rootViewController")) return (Method)window_controller;
    if (!strcmp(selector,"setRootViewController:")) return (Method)window_set_controller;
    return NULL;
}
