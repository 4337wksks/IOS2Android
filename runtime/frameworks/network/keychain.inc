 
static pthread_mutex_t keychain_mutex=PTHREAD_MUTEX_INITIALIZER;
static Word security_key_from_item(plist_t);
static int keychain_boolean(plist_t node) { uint8_t value=0; if (node && plist_get_node_type(node)==PLIST_BOOLEAN) plist_get_bool_val(node,&value); return value!=0; }
static int keychain_text(plist_t node,const char *value) {
    uint64_t length=0; const char *text=node && plist_get_node_type(node)==PLIST_STRING ? plist_get_string_ptr(node,&length) : NULL;
    return text && strlen(value)==length && !memcmp(text,value,length);
}
static int keychain_options(plist_t query,int update) {
    if (!query || plist_get_node_type(query)!=PLIST_DICT) return -50;
    int key=keychain_text(plist_dict_get_item(query,"class"),"keys");
    if (!update && !key && !keychain_text(plist_dict_get_item(query,"class"),"genp")) return -4;
    if (keychain_boolean(plist_dict_get_item(query,"sync")) || (!key && keychain_boolean(plist_dict_get_item(query,"r_Ref"))) || keychain_boolean(plist_dict_get_item(query,"r_PersistentRef"))) return -4;
    plist_t group=plist_dict_get_item(query,"agrp");
    if (group && !keychain_text(group,BUNDLE_ID)) return -34018;
    plist_dict_iter iterator=NULL; plist_dict_new_iter(query,&iterator);
    const char *const allowed[]={"class","acct","svce","gena","pdmn","sync","agrp","mdat","cdat","v_Data","r_Data","r_Attributes","r_Ref","r_PersistentRef","m_Limit","atag","type","bsiz","kcls","perm"};
    int result=0; char *name=NULL; plist_t value=NULL;
    while (iterator) {
        plist_dict_next_item(query,iterator,&name,&value); if (!name) break;
        int found=0; for (unsigned i=0;i<COUNT(allowed);++i) if (!strcmp(name,allowed[i])) found=1;
        free(name); if (!found) { result=-4; break; }
    }
    free(iterator);
    plist_t limit=plist_dict_get_item(query,"m_Limit");
    if (limit && !keychain_text(limit,"m_LimitOne") && !keychain_text(limit,"m_LimitAll")) result=-50;
    plist_t protection=plist_dict_get_item(query,"pdmn");
    if (protection && !keychain_text(protection,"ak") && !keychain_text(protection,"aku") &&
        !keychain_text(protection,"akpu") && !keychain_text(protection,"ck") && !keychain_text(protection,"cku") &&
        !keychain_text(protection,"dk") && !keychain_text(protection,"dku")) result=-50;
    return result;
}
static plist_t keychain_load(JNIEnv *env,jclass type) {
    jmethodID read=(*env)->GetMethodID(env,type,"keychainRead","()[B");
    jbyteArray data=(*env)->CallObjectMethod(env,activity,read);
    if ((*env)->ExceptionCheck(env) || !data) return NULL;
    jsize length=(*env)->GetArrayLength(env,data);
    plist_t root=NULL;
    if (!length) root=plist_new_array();
    else {
        jbyte *bytes=(*env)->GetByteArrayElements(env,data,NULL);
        plist_from_memory((const char *)bytes,length,&root,NULL);
        (*env)->ReleaseByteArrayElements(env,data,bytes,JNI_ABORT);
    }
    (*env)->DeleteLocalRef(env,data);
    if (root && plist_get_node_type(root)!=PLIST_ARRAY) { plist_free(root); root=NULL; }
    return root;
}
static int keychain_save(JNIEnv *env,jclass type,plist_t root) {
    char *bytes=NULL; uint32_t size=0;
    if (plist_to_bin(root,&bytes,&size)!=PLIST_ERR_SUCCESS) return -2070;
    jbyteArray data=(*env)->NewByteArray(env,size);
    if (!data) { free(bytes); return -2070; }
    (*env)->SetByteArrayRegion(env,data,0,size,(void *)bytes); free(bytes);
    jmethodID write=(*env)->GetMethodID(env,type,"keychainWrite","([B)V");
    (*env)->CallVoidMethod(env,activity,write,data); (*env)->DeleteLocalRef(env,data);
    return (*env)->ExceptionCheck(env) ? -2070 : 0;
}
static int keychain_match(plist_t item,plist_t query,int identity) {
    const char *const fields[]={"class","acct","svce","agrp","sync","gena","pdmn","mdat","cdat","v_Data","atag","type","bsiz","kcls","perm"};
    const char *const key_identity[]={"class","atag","type","kcls","agrp"};
    const char *const *names=identity && keychain_text(plist_dict_get_item(query,"class"),"keys") ? key_identity : fields;
    for (unsigned i=0;i<(identity ? 5 : COUNT(fields));++i) {
        plist_t wanted=plist_dict_get_item(query,names[i]),value=plist_dict_get_item(item,names[i]);
        if (!wanted) continue;
        if (!value || !plist_compare_node_value(value,wanted)) return 0;
    }
    return 1;
}
static void keychain_default(plist_t item,const char *key,plist_t value) {
    if (plist_dict_get_item(item,key)) plist_free(value); else plist_dict_set_item(item,key,value);
}
static plist_t keychain_attributes(plist_t item,int include_data) {
    plist_t result=plist_copy(item);
    if (!include_data) plist_dict_remove_item(result,"v_Data");
    return result;
}
static Word keychain_return_object(plist_t item,plist_t query) {
    if (!keychain_boolean(plist_dict_get_item(query,"r_Ref"))) return plist_object(item,0);
    if (plist_get_node_type(item)==PLIST_ARRAY) {
        Word result=sequence_empty(external_class("_OBJC_CLASS_$_NSArray"));
        for (uint32_t i=0;i<plist_array_get_size(item);++i) { Word key=keychain_return_object(plist_array_get_item(item,i),query); if (!key) return 0; sequence_add((void *)result,"Keychain references",key); }
        return result;
    }
    Word key=security_key_from_item(item); if (!key) return 0;
    int attributes=keychain_boolean(plist_dict_get_item(query,"r_Attributes")),data=keychain_boolean(plist_dict_get_item(query,"r_Data"));
    if (!attributes && !data) return key;
    plist_t fields=attributes ? keychain_attributes(item,data) : plist_new_dict();
    if (data && !attributes) plist_dict_set_item(fields,"v_Data",plist_copy(plist_dict_get_item(item,"v_Data")));
    struct GuestMap *result=(void *)plist_object(fields,0); plist_free(fields); map_set(result,"Keychain result",key,copy_string("v_Ref")); return (Word)result;
}
static int keychain_access(JNIEnv *env,jclass type,plist_t item) {
    plist_t protection=plist_dict_get_item(item,"pdmn");
    if (!protection || keychain_text(protection,"ak") || keychain_text(protection,"aku") || keychain_text(protection,"akpu")) {
        jmethodID unlocked=(*env)->GetMethodID(env,type,"keychainUnlocked","()Z");
        if (!(*env)->CallBooleanMethod(env,activity,unlocked)) return -25308;
    } else if (keychain_text(protection,"ck") || keychain_text(protection,"cku")) {
        jmethodID unlocked=(*env)->GetMethodID(env,type,"keychainAfterFirstUnlock","()Z");
        if (!(*env)->CallBooleanMethod(env,activity,unlocked)) return -25308;
    }
    return 0;
}
static int keychain_execute(Word query_object,Word updates_object,Word *output,int operation) {
    if (output) *output=0;
    Word ancestors[64];
    plist_t query=object_property_list(query_object,ancestors,0),updates=updates_object ? object_property_list(updates_object,ancestors,0) : NULL;
    int result=keychain_options(query,0);
    if (!result && operation==3) result=keychain_options(updates,1);
    if (result) { plist_free(query); plist_free(updates); return result; }
    JNIEnv *env=attached_environment("SecItem"); jclass type=(*env)->GetObjectClass(env,activity);
    pthread_mutex_lock(&keychain_mutex);
    plist_t root=keychain_load(env,type),returned=NULL;
    if (!root) result=-2070;
    else if (operation==1) {
        plist_t value_data=plist_dict_get_item(query,"v_Data");
        if (value_data && plist_get_node_type(value_data)!=PLIST_DATA) result=-50;
        if (!result) result=keychain_access(env,type,query);
        const char *const control[]={"r_Data","r_Attributes","r_Ref","r_PersistentRef","m_Limit"};
        plist_t item=plist_copy(query);
        keychain_default(item,"v_Data",plist_new_data("",0));
        for (unsigned i=0;i<COUNT(control);++i) plist_dict_remove_item(item,control[i]);
        if (!keychain_text(plist_dict_get_item(item,"class"),"keys")) { keychain_default(item,"acct",plist_new_string("")); keychain_default(item,"svce",plist_new_string("")); }
        keychain_default(item,"agrp",plist_new_string(BUNDLE_ID)); keychain_default(item,"sync",plist_new_bool(0));
        keychain_default(item,"pdmn",plist_new_string("ak"));
        for (uint32_t i=0;!result && i<plist_array_get_size(root);++i) if (keychain_match(plist_array_get_item(root,i),item,1)) result=-25299;
        if (!result) {
            plist_dict_set_item(item,"cdat",property_list_new_date((double)time(NULL)));
            plist_dict_set_item(item,"mdat",property_list_new_date((double)time(NULL)));
            if (keychain_boolean(plist_dict_get_item(query,"r_Ref"))) returned=plist_copy(item);
            else if (keychain_boolean(plist_dict_get_item(query,"r_Attributes"))) returned=keychain_attributes(item,keychain_boolean(plist_dict_get_item(query,"r_Data")));
            else if (keychain_boolean(plist_dict_get_item(query,"r_Data"))) returned=plist_copy(plist_dict_get_item(item,"v_Data"));
            plist_array_append_item(root,item); item=NULL;
            result=keychain_save(env,type,root);
        }
        plist_free(item);
    } else if (!result) {
        int all=keychain_text(plist_dict_get_item(query,"m_Limit"),"m_LimitAll"),matched=0;
        int attributes=keychain_boolean(plist_dict_get_item(query,"r_Attributes")),data=keychain_boolean(plist_dict_get_item(query,"r_Data")),reference=keychain_boolean(plist_dict_get_item(query,"r_Ref"));
        if (!operation && all && (attributes || data || reference)) returned=plist_new_array();
        for (uint32_t i=0;!result && i<plist_array_get_size(root);) {
            plist_t item=plist_array_get_item(root,i);
            if (!keychain_match(item,query,0)) { ++i; continue; }
            result=keychain_access(env,type,item); if (result) break;
            ++matched;
            if (!operation) {
                plist_t value=reference ? plist_copy(item) : attributes ? keychain_attributes(item,data) : data ? plist_copy(plist_dict_get_item(item,"v_Data")) : NULL;
                if (value) { if (all) plist_array_append_item(returned,value); else returned=value; }
                if (!all) break;
            } else if (operation==2) { plist_array_remove_item(root,i); continue; }
            else {
                plist_dict_merge(&item,updates);
                plist_dict_set_item(item,"mdat",property_list_new_date((double)time(NULL)));
            }
            ++i;
        }
        if (!result && !matched) result=-25300;
        if (!result && operation) {
            for (uint32_t i=0;!result && i<plist_array_get_size(root);++i)
                for (uint32_t j=0;j<i;++j) if (keychain_match(plist_array_get_item(root,j),plist_array_get_item(root,i),1)) { result=-25299; break; }
            if (!result) result=keychain_save(env,type,root);
        }
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); if (!result) result=-2070; }
    pthread_mutex_unlock(&keychain_mutex);
    if (!result && output && returned) { *output=keychain_return_object(returned,query); if (!*output) result=-26275; }
    plist_free(returned); plist_free(root); plist_free(query); plist_free(updates); (*env)->DeleteLocalRef(env,type);
    LOG("SecItem %s result=%d",operation==0 ? "copy" : operation==1 ? "add" : operation==2 ? "delete" : "update",result);
    return result;
}
static int keychain_copy(Word query,Word *output) { return keychain_execute(query,0,output,0); }
static int keychain_add(Word query,Word *output) { return keychain_execute(query,0,output,1); }
static int keychain_delete(Word query) { return keychain_execute(query,0,NULL,2); }
static int keychain_update(Word query,Word updates) { return keychain_execute(query,updates,NULL,3); }
static int security_random(Word random,size_t count,void *bytes) { if (random || (count && !bytes)) return -50; arc4random_buf(bytes,count); return 0; }
static Word resolve_keychain_import(const char *name) {
    if (!strcmp(name,"_SecItemCopyMatching")) return (Word)keychain_copy;
    if (!strcmp(name,"_SecItemAdd")) return (Word)keychain_add;
    if (!strcmp(name,"_SecItemDelete")) return (Word)keychain_delete;
    if (!strcmp(name,"_SecItemUpdate")) return (Word)keychain_update;
    if (!strcmp(name,"_SecRandomCopyBytes")) return (Word)security_random;
    return 0;
}
