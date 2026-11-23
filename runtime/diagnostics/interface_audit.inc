 
#include "interface_audit.h"
static void run_interface_preflight(void) {
    if (!interface_diagnostics_requested) return;
    JNIEnv *env=attached_environment("Interface preflight"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID directory=(*env)->GetMethodID(env,type,"compatibilityDirectory","()Ljava/lang/String;");
    jstring folder=(*env)->CallObjectMethod(env,activity,directory); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env) || !folder) { (*env)->ExceptionClear(env); LOG("Interface audit directory unavailable"); return; }
    const char *path=(*env)->GetStringUTFChars(env,folder,NULL); char output[PATH_MAX];
    if (!path) { (*env)->DeleteLocalRef(env,folder); return; }
    snprintf(output,sizeof(output),"%s/interface-dispatch.tsv",path);
    (*env)->ReleaseStringUTFChars(env,folder,path); (*env)->DeleteLocalRef(env,folder);
    FILE *file=fopen(output,"w"); if (!file) { LOG("Interface audit output unavailable"); return; }
    fputs("class\tselector\tclass_method\tcall\tdispatch\n",file);
    unsigned native=0,bridge=0,missing=0,unknown=0;
    for (unsigned i=0;i<COUNT(interface_probes);++i) {
        const struct InterfaceProbe *probe=interface_probes+i;
        Word cls=class_from_name(probe->class_name);
        struct HostObject prototype={0}; prototype.isa=cls;
        Word object=probe->class_method ? cls : (Word)&prototype;
        if (!probe->class_method) {
            struct HostObject *singletons[]={(void *)application_object,&defaults,&process_info,&bundle,&file_manager,&main_thread,&screen_object,&device,&game_center_player,&payment_queue};
            for (unsigned j=0;j<COUNT(singletons);++j) if (singletons[j]->isa==cls && cls) { object=(Word)singletons[j]; break; }
        }
        Method method=cls ? find_implementation(object,probe->selector) : NULL;
        const char *result;
        if (!cls) { result="unknown-class"; ++unknown; }
        else if (!method) { result="missing"; ++missing; }
        else if (in_image((Word)method)) { result="bound-native"; ++native; }
        else { result="bound-bridge"; ++bridge; }
        fprintf(file,"%s\t%s\t%u\t0x%lx\t%s\n",probe->class_name,probe->selector,probe->class_method,probe->call,result);
    }
    fclose(file);
    LOG("Interface dispatch preflight: %u pairs, %u native, %u bridge, %u missing, %u unknown classes; no methods invoked",(unsigned)COUNT(interface_probes),native,bridge,missing,unknown);
}
