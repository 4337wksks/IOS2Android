static int32_t common_crypt(uint32_t operation,uint32_t algorithm,uint32_t options,const void *key,size_t key_size,
                            const void *iv,const void *input,size_t input_size,void *output,size_t capacity,size_t *moved) {
    if (moved) *moved=0;
    if (operation>1 || (options&~3U) || !key || (!input && input_size) || (!output && capacity) ||
        key_size>INT32_MAX || input_size>INT32_MAX) return -4300;
    size_t block=0;
    switch (algorithm) {
        case 0: if (key_size!=16 && key_size!=24 && key_size!=32) return -4300; block=16; break;
        case 1: if (key_size!=8) return -4300; block=8; break;
        case 2: if (key_size!=24) return -4300; block=8; break;
        case 4: if (!key_size || key_size>512 || options) return -4300; break;
        case 6: if (key_size<8 || key_size>56) return -4300; block=8; break;
        default: return -4305;
    }
    if (block && (!(options&1) || operation==1) && input_size%block) return -4303;
    JNIEnv *env=attached_environment("CCCrypt");
    jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"crypt","(III[B[B[B[I)[B");
    jbyteArray key_bytes=(*env)->NewByteArray(env,(jsize)key_size),iv_bytes=NULL,data=NULL,result=NULL;
    jintArray state=NULL; int32_t status=-4302;
    if (block && !(options&2)) iv_bytes=(*env)->NewByteArray(env,(jsize)block);
    data=(*env)->NewByteArray(env,(jsize)input_size); state=(*env)->NewIntArray(env,1);
    if (!method || !key_bytes || !data || !state || (block && !(options&2) && !iv_bytes)) goto cleanup;
    (*env)->SetByteArrayRegion(env,key_bytes,0,(jsize)key_size,key);
    if (iv_bytes && iv) (*env)->SetByteArrayRegion(env,iv_bytes,0,(jsize)block,iv);
    if (input_size) (*env)->SetByteArrayRegion(env,data,0,(jsize)input_size,input);
    result=(*env)->CallObjectMethod(env,activity,method,(jint)operation,(jint)algorithm,(jint)options,key_bytes,iv_bytes,data,state);
    if ((*env)->ExceptionCheck(env)) goto cleanup;
    jint reported; (*env)->GetIntArrayRegion(env,state,0,1,&reported); status=reported;
    if (!status) {
        if (!result) { status=-4302; goto cleanup; }
        size_t length=(size_t)(*env)->GetArrayLength(env,result);
        if (length>capacity) { if (moved) *moved=length; status=-4301; }
        else {
            if (length) (*env)->GetByteArrayRegion(env,result,0,(jsize)length,output);
            if (moved) *moved=length;
        }
    }
cleanup:
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); status=-4302; }
    if (result) (*env)->DeleteLocalRef(env,result);
    if (state) (*env)->DeleteLocalRef(env,state);
    if (data) (*env)->DeleteLocalRef(env,data);
    if (iv_bytes) (*env)->DeleteLocalRef(env,iv_bytes);
    if (key_bytes) (*env)->DeleteLocalRef(env,key_bytes);
    (*env)->DeleteLocalRef(env,cls);
    return status;
}
static Word resolve_crypto_import(const char *name) {
    return !strcmp(name,"_CCCrypt") ? (Word)common_crypt : 0;
}
