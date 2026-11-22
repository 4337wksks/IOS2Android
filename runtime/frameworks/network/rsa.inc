struct SecurityKey { Word tag,data; jobject java; size_t bits; int private_key; };
static Word security_key_create(Word data,int private_key) {
    if (!data || read_word(data+8)>INT32_MAX) return 0;
    JNIEnv *env=attached_environment("SecKey import"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID import=(*env)->GetMethodID(env,type,"importRsaKey","([BZ)Ljava/security/Key;");
    jbyteArray bytes=(*env)->NewByteArray(env,read_word(data+8));
    (*env)->SetByteArrayRegion(env,bytes,0,read_word(data+8),(void *)data_buffer((void *)data));
    jobject java=(*env)->CallObjectMethod(env,activity,import,bytes,(jboolean)private_key);
    (*env)->DeleteLocalRef(env,bytes);
    if ((*env)->ExceptionCheck(env) || !java) { (*env)->ExceptionClear(env); (*env)->DeleteLocalRef(env,type); return 0; }
    jmethodID bits=(*env)->GetMethodID(env,type,"rsaKeyBits","(Ljava/security/Key;)I");
    jint size=(*env)->CallIntMethod(env,activity,bits,java);
    struct SecurityKey *key=calloc(1,sizeof(*key)); if (!key) stop("Allocation failed","SecKey");
    *key=(struct SecurityKey){(Word)&security_key_tag,data,(*env)->NewGlobalRef(env,java),(size_t)size,private_key};
    (*env)->DeleteLocalRef(env,java); (*env)->DeleteLocalRef(env,type); return (Word)key;
}
static Word security_key_from_item(plist_t item) {
    plist_t data=plist_dict_get_item(item,"v_Data"),type=plist_dict_get_item(item,"kcls");
    if (plist_get_node_type(data)!=PLIST_DATA || (!keychain_text(type,"0") && !keychain_text(type,"1"))) return 0;
    return security_key_create(plist_object(data,0),keychain_text(type,"1"));
}
static int rsa_generate_pair(struct GuestMap *parameters,Word *public_key,Word *private_key) {
    if (public_key) *public_key=0; if (private_key) *private_key=0;
    Word attributes=map_get(parameters,"SecKeyGeneratePair",copy_string("type"));
    if (!attributes || strcmp(string_bytes(attributes,"Key type"),"42")) return -4;
    Word size_value=map_get(parameters,"SecKeyGeneratePair",copy_string("bsiz"));
    size_t bits=size_value ? numeric_integer((void *)size_value) : 2048;
    if (bits<512 || bits>8192 || bits%8) return -50;
    JNIEnv *env=attached_environment("SecKeyGeneratePair"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID generate=(*env)->GetMethodID(env,type,"generateRsaPair","(I)[[B");
    jobjectArray pair=(*env)->CallObjectMethod(env,activity,generate,(jint)bits);
    if ((*env)->ExceptionCheck(env) || !pair) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); (*env)->DeleteLocalRef(env,type); return -2070; }
    plist_t items[2]={0,0}; Word data[2]={0,0}; int permanent[2]={0,0};
    int result=0;
    for (unsigned i=0;i<2;++i) {
        jbyteArray bytes=(*env)->GetObjectArrayElement(env,pair,i);
        jbyte *source=(*env)->GetByteArrayElements(env,bytes,NULL);
        data[i]=data_with_bytes(external_class("_OBJC_CLASS_$_NSData"),"Generated RSA DER",source,(*env)->GetArrayLength(env,bytes));
        (*env)->ReleaseByteArrayElements(env,bytes,source,JNI_ABORT); (*env)->DeleteLocalRef(env,bytes);
        Word child=map_get(parameters,"SecKey attributes",copy_string(i ? "private" : "public"));
        Word ancestors[64]; items[i]=child ? object_property_list(child,ancestors,0) : plist_new_dict();
        if (!items[i] || plist_get_node_type(items[i])!=PLIST_DICT) { result=-50; break; }
        permanent[i]=keychain_boolean(plist_dict_get_item(items[i],"perm"));
        plist_dict_set_item(items[i],"class",plist_new_string("keys")); plist_dict_set_item(items[i],"type",plist_new_string("42"));
        plist_dict_set_item(items[i],"kcls",plist_new_string(i ? "1" : "0")); plist_dict_set_item(items[i],"bsiz",plist_new_uint(bits));
        plist_dict_set_item(items[i],"v_Data",plist_new_data((void *)data_buffer((void *)data[i]),read_word(data[i]+8)));
        keychain_default(items[i],"agrp",plist_new_string(BUNDLE_ID)); keychain_default(items[i],"pdmn",plist_new_string("ak"));
        plist_dict_set_item(items[i],"cdat",property_list_new_date(scheduling_now())); plist_dict_set_item(items[i],"mdat",property_list_new_date(scheduling_now()));
        result=keychain_options(items[i],0); if (result) break;
        if (permanent[i] && !plist_dict_get_item(items[i],"atag")) { result=-4; break; }
    }
    (*env)->DeleteLocalRef(env,pair);
    if (!result && (permanent[0] || permanent[1])) {
        pthread_mutex_lock(&keychain_mutex);
        plist_t root=keychain_load(env,type);
        if (!root) result=-2070;
        for (unsigned i=0;!result && i<2;++i) if (permanent[i]) {
            for (uint32_t j=0;j<plist_array_get_size(root);++j) if (keychain_match(plist_array_get_item(root,j),items[i],1)) { result=-25299; break; }
            if (!result) plist_array_append_item(root,plist_copy(items[i]));
        }
        if (!result) result=keychain_save(env,type,root);
        plist_free(root); pthread_mutex_unlock(&keychain_mutex);
    }
    if (!result) {
        Word pub=security_key_create(data[0],0),priv=security_key_create(data[1],1);
        if (!pub || !priv) result=-26275;
        else { if (public_key) *public_key=pub; if (private_key) *private_key=priv; }
    }
    plist_free(items[0]); plist_free(items[1]); (*env)->DeleteLocalRef(env,type);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); result=-2070; }
    LOG("SecKey RSA generation bits=%zu persistent=%d/%d result=%d",bits,permanent[0],permanent[1],result);
    return result;
}
static size_t rsa_block_size(struct SecurityKey *key) { return key && key->tag==(Word)&security_key_tag ? (key->bits+7)/8 : 0; }
static int rsa_sign(struct SecurityKey *key,uint32_t padding,const void *data,size_t length,void *signature,size_t *signature_length) {
    if (!key || key->tag!=(Word)&security_key_tag || !key->private_key || !signature_length || (!data && length) || length>INT32_MAX) return -50;
    size_t required=rsa_block_size(key),capacity=*signature_length; *signature_length=required;
    if (!signature || capacity<required) return -25301;
    if (padding!=0 && padding!=1 && (padding<0x8001 || padding>0x8006)) return -4;
    JNIEnv *env=attached_environment("SecKeyRawSign"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID sign=(*env)->GetMethodID(env,type,"rsaSign","(Ljava/security/Key;I[B)[B");
    jbyteArray input=(*env)->NewByteArray(env,length); (*env)->SetByteArrayRegion(env,input,0,length,data);
    jbyteArray output=(*env)->CallObjectMethod(env,activity,sign,key->java,(jint)padding,input);
    int result=0;
    if ((*env)->ExceptionCheck(env) || !output) { (*env)->ExceptionClear(env); result=-2070; }
    else {
        size_t count=(*env)->GetArrayLength(env,output); *signature_length=count;
        if (count>capacity) result=-25301; else (*env)->GetByteArrayRegion(env,output,0,count,signature);
        (*env)->DeleteLocalRef(env,output);
    }
    (*env)->DeleteLocalRef(env,input); (*env)->DeleteLocalRef(env,type); return result;
}
static int rsa_verify(struct SecurityKey *key,uint32_t padding,const void *data,size_t length,const void *signature,size_t signature_length) {
    if (!key || key->tag!=(Word)&security_key_tag || key->private_key || length>INT32_MAX || signature_length>INT32_MAX) return -50;
    if (padding!=0 && padding!=1 && (padding<0x8001 || padding>0x8006)) return -4;
    JNIEnv *env=attached_environment("SecKeyRawVerify"); jclass type=(*env)->GetObjectClass(env,activity);
    jmethodID verify=(*env)->GetMethodID(env,type,"rsaVerify","(Ljava/security/Key;I[B[B)Z");
    jbyteArray input=(*env)->NewByteArray(env,length),signed_data=(*env)->NewByteArray(env,signature_length);
    (*env)->SetByteArrayRegion(env,input,0,length,data); (*env)->SetByteArrayRegion(env,signed_data,0,signature_length,signature);
    int accepted=(*env)->CallBooleanMethod(env,activity,verify,key->java,(jint)padding,input,signed_data);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); accepted=0; }
    (*env)->DeleteLocalRef(env,input); (*env)->DeleteLocalRef(env,signed_data); (*env)->DeleteLocalRef(env,type);
    return accepted ? 0 : -67808;
}
static Word resolve_rsa_import(const char *name) {
    if (!strcmp(name,"_SecKeyGeneratePair")) return (Word)rsa_generate_pair;
    if (!strcmp(name,"_SecKeyGetBlockSize")) return (Word)rsa_block_size;
    if (!strcmp(name,"_SecKeyRawSign")) return (Word)rsa_sign;
    if (!strcmp(name,"_SecKeyRawVerify")) return (Word)rsa_verify;
    return 0;
}
