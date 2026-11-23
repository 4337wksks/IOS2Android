/* Controller identities come from the original archives. */
#include "storyboard_catalogue.h"
struct StoryboardResource { Word isa,bundle,metadata,name,scenes,path; };
static Word storyboard_catalogue;
static pthread_once_t storyboard_catalogue_once=PTHREAD_ONCE_INIT;
static void storyboard_catalogue_load(void) {
    plist_t root=NULL;
    if (plist_from_json(storyboard_catalogue_json,sizeof(storyboard_catalogue_json)-1,&root)!=PLIST_ERR_SUCCESS)
        stop("Invalid generated storyboard catalogue","UIKit");
    storyboard_catalogue=plist_object(root,0); plist_free(root);
}
static Word storyboard_scenes_for_path(Word path,const char *selector) {
    pthread_once(&storyboard_catalogue_once,storyboard_catalogue_load);
    const char *file=string_bytes(path,selector);
    size_t length=strlen(file);
    struct GuestMap *catalogue=(void *)storyboard_catalogue;
    for (size_t i=0;i<catalogue->count;++i) {
        const char *folder=string_bytes(catalogue->entries[i].key,selector);
        size_t size=strlen(folder);
        const char suffix[]="/Info.plist";
        if (length>=size+sizeof(suffix) && file[length-size-sizeof(suffix)]=='/' &&
            !memcmp(file+length-size-(sizeof(suffix)-1),folder,size) &&
            !memcmp(file+length-(sizeof(suffix)-1),suffix,sizeof(suffix)-1)) return catalogue->entries[i].value;
    }
    return 0;
}
static Word storyboard_create(Word cls,const char *selector,Word name,Word resource_bundle) {
    if (!name) stop("Nil storyboard name",selector);
    if (!resource_bundle) resource_bundle=(Word)&bundle;
    Word folder=append_path_extension(name,selector,copy_string("storyboardc"));
    Word file=append_path(folder,selector,copy_string("Info.plist"));
    Word path=bundle_resource(resource_bundle,selector,file,0);
    if (!path) path=bundle_resource(resource_bundle,selector,append_path(copy_string("Base.lproj"),selector,file),0);
    if (!path) stop("Storyboard resource was not found",string_bytes(name,selector));
    Word metadata=dictionary_from_file(external_class("_OBJC_CLASS_$_NSDictionary"),selector,path);
    if (!metadata) stop("Invalid storyboard metadata",selector);
    Word scenes=storyboard_scenes_for_path(path,selector);
    if (!scenes) stop("Storyboard controller metadata is not generated for this resource",string_bytes(name,selector));
    struct StoryboardResource *board=calloc(1,sizeof(*board));
    if (!board) stop("Allocation failed",selector);
    *board=(struct StoryboardResource){cls,resource_bundle,metadata,text_copy(name,"copy"),scenes,path};
    return (Word)board;
}
static void storyboard_attach_controller(struct StoryboardResource *board,Word controller,struct GuestMap *scene) {
    struct ViewState *state=view_state(controller);
    state->storyboard=(Word)board; state->nib_bundle=board->bundle;
    state->archived_graph=map_get(scene,"Storyboard view",copy_string("viewGraph"));
    state->nib_name=map_get(scene,"Storyboard nib",copy_string("nib"));
    if (state->nib_name && !string_length(state->nib_name,"Storyboard nib")) state->nib_name=0;
    Word style=map_get(scene,"Storyboard navigation bar",copy_string("barStyle"));
    if (style) navigation_bar_set_style(controller_navigation_bar(controller),"Storyboard navigation bar",(long)numeric_integer((void *)style));
}
static Word storyboard_instantiate(struct StoryboardResource *board,const char *selector,Word identifier) {
    Word mappings=map_get((void *)board->metadata,selector,copy_string("UIViewControllerIdentifiersToNibNames"));
    Word nib=identifier && mappings ? map_get((void *)mappings,selector,identifier) : 0;
    struct GuestMap *scene=(void *)(identifier ? map_get((void *)board->scenes,selector,identifier) : 0);
    if (!nib || !scene) stop("Storyboard scene identifier was not found",identifier ? string_bytes(identifier,selector) : selector);
    Word class_name_value=map_get(scene,selector,copy_string("class"));
    Word cls=class_from_name(string_bytes(class_name_value,selector));
    if (!cls) stop("Unknown archived storyboard controller class",string_bytes(class_name_value,selector));
    Word controller=(in_image(cls) || dynamic_class(cls)) ? guest_object(cls) : host_allocate(cls);
    storyboard_attach_controller(board,controller,scene);
    view_state(controller)->deferred_storyboard=1;
#if I2A_DEVELOPMENT
    LOG("[DIAG-storyboard] resource=%s scene=%s class=%s view-archive=%s",string_bytes(board->name,selector),string_bytes(identifier,selector),string_bytes(class_name_value,selector),view_state(controller)->archived_graph ? "supported" : "unavailable");
#endif
    return controller;
}
static Word storyboard_initial(struct StoryboardResource *board,const char *selector) {
    Word identifier=map_get((void *)board->metadata,selector,copy_string("UIStoryboardDesignatedEntryPointIdentifier"));
    return identifier ? storyboard_instantiate(board,selector,identifier) : 0;
}
static Word storyboard_startup(Word name){struct StoryboardResource *board=(void *)storyboard_create(external_class("_OBJC_CLASS_$_UIStoryboard"),"Main storyboard",name,(Word)&bundle);return storyboard_initial(board,"instantiateInitialViewController");}
static Word controller_storyboard(Word object) {
    return view_state(object)->storyboard;
}
static struct UIKitAlert *storyboard_capability_alert(Word controller) {
     
    Word alert=ui_alert_create(external_class("_OBJC_CLASS_$_UIAlertController"),"Storyboard presentation",
        copy_string("UI unavailable"),copy_string("This interface is not available yet. Please go back."),1);
    Word action=ui_alert_action(external_class("_OBJC_CLASS_$_UIAlertAction"),"Storyboard presentation",copy_string("Back"),1,0);
    ui_alert_add((void *)alert,"Storyboard presentation",(void *)action);
    return (void *)alert;
}
static void storyboard_capability_close(Word controller) {
    struct StoryboardResource *board=(void *)view_state(controller)->storyboard;
    if (board && runtime_extension.close_unavailable_controller)
        runtime_extension.close_unavailable_controller(controller,string_bytes(board->path,"Storyboard close"));
}
static Method storyboards_method(Word object,const char *selector,const char *receiver,int class_receiver) {
    if (!receiver || strcmp(receiver,"_OBJC_CLASS_$_UIStoryboard")) return NULL;
    if (class_receiver && !strcmp(selector,"storyboardWithName:bundle:")) return (Method)storyboard_create;
    if (!class_receiver && !strcmp(selector,"instantiateViewControllerWithIdentifier:")) return (Method)storyboard_instantiate;
    if (!class_receiver && !strcmp(selector,"instantiateInitialViewController")) return (Method)storyboard_initial;
    return NULL;
}
