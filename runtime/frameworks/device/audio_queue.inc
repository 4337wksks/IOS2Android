struct AudioQueueBridge;
struct AudioQueueBufferBridge {
    uint32_t capacity;
    void *data;
    uint32_t size;
    void *user;
    uint32_t description_capacity;
    struct AudioPacketDescription *descriptions;
    uint32_t description_count;
    struct AudioQueueBridge *owner;
    struct AudioQueueBufferBridge *next,*queued_next;
    uint32_t packet,byte_offset;
    int queued;
};
typedef void (*AudioQueueOutput)(void *,struct AudioQueueBridge *,struct AudioQueueBufferBridge *);
struct AudioCodecOutput { int32_t index; AMediaCodecBufferInfo info; };
struct AudioQueueBridge {
    pthread_mutex_t mutex,codec_mutex;
    pthread_cond_t codec_condition;
    struct GuestAudioFormat input,output;
    AudioQueueOutput callback;
    void *context;
    struct AudioQueueBufferBridge *buffers,*head,*tail;
    AMediaCodec *codec;
    unsigned char *cookie,*pcm;
    size_t cookie_size,pcm_capacity,pcm_size,pcm_offset;
    int32_t inputs[64]; struct AudioCodecOutput outputs[64];
    unsigned input_read,input_count,output_read,output_count;
    uint32_t policy,pcm_encoding;
    int32_t codec_error;
    double input_time;
    int offline,running,end_input,eos_sent,eos,logged,callback_depth;
};
static void audio_codec_input(AMediaCodec *codec,void *context,int32_t index) {
    struct AudioQueueBridge *queue=context;
    pthread_mutex_lock(&queue->codec_mutex);
    if (codec==queue->codec) {
        if (queue->input_count==COUNT(queue->inputs)) queue->codec_error=-66681;
        else { queue->inputs[(queue->input_read+queue->input_count)%COUNT(queue->inputs)]=index; ++queue->input_count; }
        pthread_cond_signal(&queue->codec_condition);
    }
    pthread_mutex_unlock(&queue->codec_mutex);
}
static void audio_codec_output(AMediaCodec *codec,void *context,int32_t index,AMediaCodecBufferInfo *info) {
    struct AudioQueueBridge *queue=context;
    pthread_mutex_lock(&queue->codec_mutex);
    if (codec==queue->codec) {
        if (queue->output_count==COUNT(queue->outputs)) queue->codec_error=-66681;
        else { queue->outputs[(queue->output_read+queue->output_count)%COUNT(queue->outputs)]=(struct AudioCodecOutput){index,*info}; ++queue->output_count; }
        pthread_cond_signal(&queue->codec_condition);
    }
    pthread_mutex_unlock(&queue->codec_mutex);
}
static void audio_codec_format(AMediaCodec *codec,void *context,AMediaFormat *format) {
    struct AudioQueueBridge *queue=context;
    int32_t rate=0,channels=0,encoding=2;
    AMediaFormat_getInt32(format,"sample-rate",&rate); AMediaFormat_getInt32(format,"channel-count",&channels);
    AMediaFormat_getInt32(format,"pcm-encoding",&encoding);
    pthread_mutex_lock(&queue->codec_mutex);
    if (codec==queue->codec) {
        if (rate!=(int32_t)queue->output.rate || channels!=(int32_t)queue->output.channels || (encoding!=2 && encoding!=4)) queue->codec_error=-66673;
        else queue->pcm_encoding=(uint32_t)encoding;
        pthread_cond_signal(&queue->codec_condition);
    }
    pthread_mutex_unlock(&queue->codec_mutex);
}
static void audio_codec_error(AMediaCodec *codec,void *context,media_status_t error,int32_t action,const char *detail) {
    struct AudioQueueBridge *queue=context;
    pthread_mutex_lock(&queue->codec_mutex);
    if (codec==queue->codec) { queue->codec_error=-66681; pthread_cond_signal(&queue->codec_condition); }
    pthread_mutex_unlock(&queue->codec_mutex);
    LOG("AudioQueue decoder error=%d action=%d",error,action);
}
static int32_t audio_queue_drop_codec(struct AudioQueueBridge *queue) {
    pthread_mutex_lock(&queue->codec_mutex);
    AMediaCodec *codec=queue->codec; queue->codec=NULL;
    queue->input_read=queue->input_count=queue->output_read=queue->output_count=0; queue->codec_error=0;
    pthread_mutex_unlock(&queue->codec_mutex);
    int32_t result=0;
    if (codec) {
        media_status_t stopped=AMediaCodec_stop(codec),deleted=AMediaCodec_delete(codec);
        if (stopped!=AMEDIA_OK || deleted!=AMEDIA_OK) result=-66681;
    }
    queue->pcm_size=queue->pcm_offset=0; queue->eos=queue->eos_sent=0;
    return result;
}
static const char *audio_queue_mime(uint32_t type) {
    if (type==0x61616320 || type==0x61616368 || type==0x61616370 || type==0x6161636c) return "audio/mp4a-latm";
    if (type==0x2e6d7033) return "audio/mpeg";
    return NULL;
}
static AMediaCodec *audio_queue_decoder(const char *mime,uint32_t policy) {
    if (!policy) return AMediaCodec_createDecoderByType(mime);
    JNIEnv *env=attached_environment("AudioQueue decoder selection"); jclass cls=(*env)->GetObjectClass(env,activity);
    jmethodID method=(*env)->GetMethodID(env,cls,"audioDecoderName","(Ljava/lang/String;I)Ljava/lang/String;");
    jstring type=(*env)->NewStringUTF(env,mime);
    jstring name=method && type ? (*env)->CallObjectMethod(env,activity,method,type,(jint)policy) : NULL;
    AMediaCodec *codec=NULL;
    if (!(*env)->ExceptionCheck(env) && name) {
        const char *bytes=(*env)->GetStringUTFChars(env,name,NULL);
        if (bytes) { codec=AMediaCodec_createCodecByName(bytes); (*env)->ReleaseStringUTFChars(env,name,bytes); }
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (name) (*env)->DeleteLocalRef(env,name);
    if (type) (*env)->DeleteLocalRef(env,type);
    (*env)->DeleteLocalRef(env,cls); return codec;
}
static int32_t audio_queue_make_codec(struct AudioQueueBridge *queue) {
    if (queue->input.format==0x6c70636d) return 0;
    const char *mime=audio_queue_mime(queue->input.format); if (!mime) return -66673;
    AMediaFormat *format=AMediaFormat_new(); if (!format) return -108;
    AMediaFormat_setString(format,"mime",mime); AMediaFormat_setInt32(format,"sample-rate",(int32_t)queue->input.rate);
    AMediaFormat_setInt32(format,"channel-count",(int32_t)queue->input.channels); AMediaFormat_setInt32(format,"pcm-encoding",2);
    if (queue->cookie_size) AMediaFormat_setBuffer(format,"csd-0",queue->cookie,queue->cookie_size);
    else if (!strcmp(mime,"audio/mp4a-latm")) { AMediaFormat_delete(format); return -66673; }
    AMediaCodec *codec=audio_queue_decoder(mime,queue->policy);
    if (!codec) { AMediaFormat_delete(format); return queue->policy==2 ? -66672 : -66673; }
    const AMediaCodecOnAsyncNotifyCallback callbacks={audio_codec_input,audio_codec_output,audio_codec_format,audio_codec_error};
    pthread_mutex_lock(&queue->codec_mutex); queue->codec=codec; queue->pcm_encoding=2; pthread_mutex_unlock(&queue->codec_mutex);
    media_status_t result=AMediaCodec_setAsyncNotifyCallback(codec,callbacks,queue);
    if (result==AMEDIA_OK) result=AMediaCodec_configure(codec,format,NULL,NULL,0);
    if (result==AMEDIA_OK) result=AMediaCodec_start(codec);
    AMediaFormat_delete(format);
    if (result!=AMEDIA_OK) { audio_queue_drop_codec(queue); return -66681; }
    return 0;
}
static int32_t audio_queue_new(const struct GuestAudioFormat *format,AudioQueueOutput callback,void *context,
                               Word runloop,Word mode,uint32_t flags,struct AudioQueueBridge **output) {
    _Static_assert(offsetof(struct AudioQueueBufferBridge,data)==8 && offsetof(struct AudioQueueBufferBridge,size)==16 &&
                   offsetof(struct AudioQueueBufferBridge,descriptions)==40 && offsetof(struct AudioQueueBufferBridge,description_count)==48,
                   "Darwin AudioQueueBuffer layout");
    if (!output) return -50;
    *output=NULL;
    if (!format || !callback || !isfinite(format->rate) || floor(format->rate)!=format->rate || format->rate<8000 || format->rate>192000 ||
        !format->channels || format->channels>2) return -50;
    if (runloop || mode || flags || (!audio_queue_mime(format->format) && !audio_format_supported(format))) return -66673;
    struct AudioQueueBridge *queue=calloc(1,sizeof(*queue)); if (!queue) return -108;
    pthread_mutexattr_t attr; pthread_mutexattr_init(&attr); pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&queue->mutex,&attr); pthread_mutexattr_destroy(&attr);
    pthread_mutex_init(&queue->codec_mutex,NULL); pthread_cond_init(&queue->codec_condition,NULL);
    queue->input=*format; queue->callback=callback; queue->context=context;
    *output=queue; return 0;
}
static int32_t audio_queue_allocate_descriptions(struct AudioQueueBridge *queue,uint32_t size,uint32_t count,struct AudioQueueBufferBridge **output) {
    if (!queue || !output || !size) return -50;
    *output=NULL;
    struct AudioQueueBufferBridge *buffer=calloc(1,sizeof(*buffer)); if (!buffer) return -108;
    buffer->data=malloc(size); buffer->descriptions=count ? calloc(count,sizeof(*buffer->descriptions)) : NULL;
    if (!buffer->data || (count && !buffer->descriptions)) { free(buffer->data); free(buffer->descriptions); free(buffer); return -108; }
    buffer->capacity=size; buffer->description_capacity=count; buffer->owner=queue;
    pthread_mutex_lock(&queue->mutex); buffer->next=queue->buffers; queue->buffers=buffer; pthread_mutex_unlock(&queue->mutex);
    *output=buffer; return 0;
}
static int32_t audio_queue_allocate(struct AudioQueueBridge *queue,uint32_t size,struct AudioQueueBufferBridge **output) {
    return audio_queue_allocate_descriptions(queue,size,0,output);
}
static int32_t audio_queue_free_buffer(struct AudioQueueBridge *queue,struct AudioQueueBufferBridge *buffer) {
    if (!queue || !buffer) return -50;
    pthread_mutex_lock(&queue->mutex);
    struct AudioQueueBufferBridge **slot=&queue->buffers;
    while (*slot && *slot!=buffer) slot=&(*slot)->next;
    int32_t result=!*slot ? -66687 : buffer->queued ? -66679 : 0;
    if (!result) { *slot=buffer->next; free(buffer->data); free(buffer->descriptions); free(buffer); }
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_enqueue(struct AudioQueueBridge *queue,struct AudioQueueBufferBridge *buffer,uint32_t count,const struct AudioPacketDescription *descriptions) {
    if (!queue || !buffer || buffer->owner!=queue || !buffer->size || buffer->size>buffer->capacity || (count && !descriptions)) return -50;
    pthread_mutex_lock(&queue->mutex);
    int32_t result=0;
    if (buffer->queued) result=-66679;
    else if (queue->end_input) result=-66678;
    else if (count>buffer->description_capacity) result=-66687;
    else {
        if (count) { memcpy(buffer->descriptions,descriptions,(size_t)count*sizeof(*descriptions)); buffer->description_count=count; }
        uint32_t packets=buffer->description_count;
        if (packets>buffer->description_capacity) result=-66687;
        else if (packets) {
            for (uint32_t i=0;i<packets;++i) {
                struct AudioPacketDescription packet=buffer->descriptions[i];
                if (packet.offset<0 || !packet.size || (uint64_t)packet.offset>buffer->size || packet.size>buffer->size-(uint64_t)packet.offset) { result=-66687; break; }
            }
        } else if (!queue->input.bytes_per_packet || buffer->size%queue->input.bytes_per_packet) result=-66687;
        if (!result) {
            buffer->queued=1; buffer->packet=buffer->byte_offset=0; buffer->queued_next=NULL;
            if (queue->tail) queue->tail->queued_next=buffer; else queue->head=buffer; queue->tail=buffer;
        }
    }
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_property(struct AudioQueueBridge *queue,uint32_t property,const void *data,uint32_t size) {
    if (!queue || (!data && size)) return -50;
    pthread_mutex_lock(&queue->mutex); int32_t result=0;
    if (queue->running) result=-66678;
    else if (property==0x61716d63) {
        if (size>64) result=-66675;
        else {
            unsigned char *cookie=size ? malloc(size) : NULL;
            if (size && !cookie) result=-108;
            else { if (size) memcpy(cookie,data,size); free(queue->cookie); queue->cookie=cookie; queue->cookie_size=size; }
        }
    } else if (property==0x61716370) {
        uint32_t value=0;
        if (size!=4) result=-66683;
        else { memcpy(&value,data,4); if (value>4) result=-66675; else queue->policy=value; }
    } else if (property==0x6171636c) {
        uint32_t tag=0;
        if (size<12) result=-66683;
        else { memcpy(&tag,data,4); if (tag!=(queue->input.channels==1 ? 0x00640001U : 0x00650002U)) result=-66675; }
    } else result=-66684;
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_offline_format(struct AudioQueueBridge *queue,const struct GuestAudioFormat *format,const void *layout) {
    if (!queue) return -50;
    pthread_mutex_lock(&queue->mutex); int32_t result=0;
    if (queue->running) result=-66678;
    else if (!format) queue->offline=0;
    else if (!audio_format_supported(format) || (format->flags&32) || format->bits!=16 || (format->flags&2) ||
        format->rate!=queue->input.rate || format->channels!=queue->input.channels) result=-66673;
    else if (layout && *(const uint32_t *)layout!=(format->channels==1 ? 0x00640001U : 0x00650002U)) result=-66675;
    else { queue->output=*format; queue->offline=1; }
    pthread_mutex_unlock(&queue->mutex); return result;
}
static void audio_queue_clear(struct AudioQueueBridge *queue) {
    for (struct AudioQueueBufferBridge *buffer=queue->head;buffer;) {
        struct AudioQueueBufferBridge *next=buffer->queued_next; buffer->queued=0; buffer->queued_next=NULL; buffer=next;
    }
    queue->head=queue->tail=NULL; queue->end_input=0;
}
static int32_t audio_queue_stop(struct AudioQueueBridge *queue,unsigned char immediate) {
    if (!queue) return -50;
    pthread_mutex_lock(&queue->mutex);
    if (!immediate && (queue->head || queue->pcm_offset<queue->pcm_size || (queue->codec && !queue->eos))) { pthread_mutex_unlock(&queue->mutex); return -66678; }
    queue->running=0; int32_t result=audio_queue_drop_codec(queue); audio_queue_clear(queue);
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_start(struct AudioQueueBridge *queue,const struct GuestAudioTimestamp *timestamp) {
    if (!queue) return -50;
    pthread_mutex_lock(&queue->mutex); int32_t result=0;
    if (!queue->offline) result=-66677;
    else if (!queue->running) {
        if (timestamp && (!(timestamp->flags&1) || !isfinite(timestamp->sample_time) || timestamp->sample_time<0)) result=-50;
        else { queue->input_time=timestamp ? timestamp->sample_time : 0; result=audio_queue_make_codec(queue); if (!result) queue->running=1; }
    }
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_flush(struct AudioQueueBridge *queue) {
    if (!queue) return -50;
    pthread_mutex_lock(&queue->mutex);
    int32_t result=queue->offline ? 0 : -66677;
    if (!result) queue->end_input=1;
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_return_buffer(struct AudioQueueBridge *queue) {
    struct AudioQueueBufferBridge *buffer=queue->head;
    queue->head=buffer->queued_next; if (!queue->head) queue->tail=NULL;
    buffer->queued=0; buffer->queued_next=NULL;
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};
    char previous_stage[sizeof(stage)]; memcpy(previous_stage,stage,sizeof(stage));
    active_guest_call_boundary=&call; ++queue->callback_depth;
    int32_t result=0;
    if (!setjmp(call.environment)) {
        snprintf(stage,sizeof(stage),"AudioQueue output callback"); queue->callback(queue->context,queue,buffer);
    } else { result=-66681; LOG("AudioQueue output callback stopped: %s",status); }
    --queue->callback_depth; active_guest_call_boundary=call.previous; memcpy(stage,previous_stage,sizeof(stage));
    return result;
}
static int32_t audio_queue_feed(struct AudioQueueBridge *queue,int32_t index) {
    struct AudioQueueBufferBridge *buffer=queue->head;
    size_t capacity=0; uint8_t *input=AMediaCodec_getInputBuffer(queue->codec,(size_t)index,&capacity);
    if (!input) return -66681;
    if (!buffer) {
        if (!queue->end_input || queue->eos_sent) return -66678;
        media_status_t result=AMediaCodec_queueInputBuffer(queue->codec,(size_t)index,0,0,(uint64_t)llround(queue->input_time*1e6/queue->input.rate),AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        if (result!=AMEDIA_OK) return -66681;
        queue->eos_sent=1; return 0;
    }
    struct AudioPacketDescription packet=buffer->description_count ? buffer->descriptions[buffer->packet] :
        (struct AudioPacketDescription){buffer->byte_offset,queue->input.frames_per_packet,queue->input.bytes_per_packet};
    if (packet.size>capacity) return -66687;
    memcpy(input,(const char *)buffer->data+packet.offset,packet.size);
    media_status_t result=AMediaCodec_queueInputBuffer(queue->codec,(size_t)index,0,packet.size,
        (uint64_t)llround(queue->input_time*1e6/queue->input.rate),0);
    if (result!=AMEDIA_OK) return -66681;
    queue->input_time+=packet.frames ? packet.frames : queue->input.frames_per_packet;
    ++buffer->packet; buffer->byte_offset+=packet.size;
    if (buffer->description_count ? buffer->packet==buffer->description_count : buffer->byte_offset==buffer->size) {
        int32_t returned=audio_queue_return_buffer(queue);
        return returned ? returned : queue->running ? 0 : -66678;
    }
    return 0;
}
static int32_t audio_queue_take_pcm(struct AudioQueueBridge *queue,struct AudioCodecOutput event,uint32_t encoding) {
    if (event.info.flags&AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)
        return AMediaCodec_releaseOutputBuffer(queue->codec,(size_t)event.index,false)==AMEDIA_OK ? 0 : -66681;
    size_t capacity=0; uint8_t *data=AMediaCodec_getOutputBuffer(queue->codec,(size_t)event.index,&capacity);
    int32_t result=0;
    if (event.info.offset<0 || event.info.size<0 || (size_t)event.info.offset>capacity || (size_t)event.info.size>capacity-(size_t)event.info.offset || (!data && event.info.size)) result=-66681;
    else {
        size_t count=(size_t)event.info.size/(encoding==4 ? 4 : 2),bytes=count*2;
        if ((size_t)event.info.size%(encoding==4 ? 4 : 2) || count%queue->output.channels) result=-66673;
        else {
            if (bytes>queue->pcm_capacity) {
                unsigned char *pcm=realloc(queue->pcm,bytes);
                if (!pcm) result=-108; else { queue->pcm=pcm; queue->pcm_capacity=bytes; }
            }
            if (!result) {
                if (encoding==2 && bytes) memcpy(queue->pcm,data+event.info.offset,bytes);
                else for (size_t i=0;i<count;++i) {
                    float sample; memcpy(&sample,data+event.info.offset+4*i,4);
                    int16_t value=isnan(sample) ? 0 : sample>=1 ? INT16_MAX : sample<=-1 ? INT16_MIN : (int16_t)lrintf(sample*32768);
                    memcpy(queue->pcm+2*i,&value,2);
                }
                queue->pcm_offset=0; queue->pcm_size=bytes;
            }
        }
    }
    if (event.info.flags&AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) queue->eos=1;
    if (AMediaCodec_releaseOutputBuffer(queue->codec,(size_t)event.index,false)!=AMEDIA_OK && !result) result=-66681;
    return result;
}
static int32_t audio_queue_pcm_render(struct AudioQueueBridge *queue,struct AudioQueueBufferBridge *output,uint32_t bytes) {
    if (queue->input.flags&32) return -66673;
    while (output->size<bytes && queue->head) {
        struct AudioQueueBufferBridge *buffer=queue->head;
        unsigned input_frame=queue->input.bytes_per_frame,output_frame=queue->output.bytes_per_frame;
        uint32_t available=(buffer->size-buffer->byte_offset)/input_frame,wanted=(bytes-output->size)/output_frame;
        uint32_t frames=available<wanted ? available : wanted;
        if (!frames) return -66687;
        const unsigned char *input=(const unsigned char *)buffer->data+buffer->byte_offset;
        unsigned char *target=(unsigned char *)output->data+output->size;
        if (queue->input.bits==16) memcpy(target,input,(size_t)frames*output_frame);
        else for (size_t i=0;i<(size_t)frames*queue->input.channels;++i) {
            float sample; memcpy(&sample,input+4*i,4);
            int16_t value=isnan(sample) ? 0 : sample>=1 ? INT16_MAX : sample<=-1 ? INT16_MIN : (int16_t)lrintf(sample*32768);
            memcpy(target+2*i,&value,2);
        }
        output->size+=frames*output_frame; buffer->byte_offset+=frames*input_frame;
        if (buffer->byte_offset==buffer->size) { int32_t result=audio_queue_return_buffer(queue); if (result) return result; }
    }
    return 0;
}
static int32_t audio_queue_render(struct AudioQueueBridge *queue,const struct GuestAudioTimestamp *timestamp,struct AudioQueueBufferBridge *output,uint32_t frames) {
    if (!queue || !output || output->owner!=queue || !timestamp || !(timestamp->flags&1) || !isfinite(timestamp->sample_time) || timestamp->sample_time<0) return -50;
    pthread_mutex_lock(&queue->mutex); int32_t result=0;
    output->size=0;
    if (!queue->offline || !queue->running) { result=-66678; goto done; }
    if (output->queued || (uint64_t)frames*queue->output.bytes_per_frame>output->capacity) { result=-66687; goto done; }
    if (!frames) { queue->input_time=timestamp->sample_time; goto done; }
    uint32_t bytes=frames*queue->output.bytes_per_frame;
    if (queue->input.format==0x6c70636d) { result=audio_queue_pcm_render(queue,output,bytes); goto done; }
    while (output->size<bytes) {
        if (queue->pcm_offset<queue->pcm_size) {
            size_t count=queue->pcm_size-queue->pcm_offset;
            if (count>bytes-output->size) count=bytes-output->size;
            memcpy((char *)output->data+output->size,queue->pcm+queue->pcm_offset,count);
            queue->pcm_offset+=count; output->size+=(uint32_t)count; continue;
        }
        if (queue->eos) break;
        pthread_mutex_lock(&queue->codec_mutex);
        if (queue->codec_error) { result=queue->codec_error; pthread_mutex_unlock(&queue->codec_mutex); break; }
        if (queue->output_count) {
            struct AudioCodecOutput event=queue->outputs[queue->output_read]; uint32_t encoding=queue->pcm_encoding;
            queue->output_read=(queue->output_read+1)%COUNT(queue->outputs); --queue->output_count;
            pthread_mutex_unlock(&queue->codec_mutex);
            result=audio_queue_take_pcm(queue,event,encoding); if (result) break; continue;
        }
        if (queue->input_count && (queue->head || (queue->end_input && !queue->eos_sent))) {
            int32_t index=queue->inputs[queue->input_read]; queue->input_read=(queue->input_read+1)%COUNT(queue->inputs); --queue->input_count;
            pthread_mutex_unlock(&queue->codec_mutex);
            result=audio_queue_feed(queue,index); if (result) break; continue;
        }
        if (!queue->head && !queue->end_input && !queue->eos_sent) { pthread_mutex_unlock(&queue->codec_mutex); break; }
        struct timespec deadline; clock_gettime(CLOCK_REALTIME,&deadline); deadline.tv_sec+=2;
        int waited=pthread_cond_timedwait(&queue->codec_condition,&queue->codec_mutex,&deadline);
        pthread_mutex_unlock(&queue->codec_mutex);
        if (waited) { result=-66674; break; }
    }
    if (!result && output->size && !queue->logged++) LOG("AudioQueue offline decoder produced PCM: %u bytes, %.0f Hz, %u channels",output->size,queue->output.rate,queue->output.channels);
done:
    pthread_mutex_unlock(&queue->mutex); return result;
}
static int32_t audio_queue_dispose(struct AudioQueueBridge *queue,unsigned char immediate) {
    if (!queue) return -50;
    pthread_mutex_lock(&queue->mutex);
    int pending=queue->callback_depth; pthread_mutex_unlock(&queue->mutex);
    if (pending) return -66685;
    int32_t result=audio_queue_stop(queue,immediate); if (result) return result;
    while (queue->buffers) {
        struct AudioQueueBufferBridge *buffer=queue->buffers; queue->buffers=buffer->next;
        free(buffer->data); free(buffer->descriptions); free(buffer);
    }
    free(queue->cookie); free(queue->pcm); pthread_cond_destroy(&queue->codec_condition);
    pthread_mutex_destroy(&queue->codec_mutex); pthread_mutex_destroy(&queue->mutex); free(queue); return 0;
}
static int32_t audio_queue_reset(struct AudioQueueBridge *queue) {
    if (!queue) return -50;
    pthread_mutex_lock(&queue->mutex); int running=queue->running;
    int32_t result=audio_queue_stop(queue,1);
    if (!result && running) result=audio_queue_start(queue,NULL);
    pthread_mutex_unlock(&queue->mutex); return result;
}
static Word resolve_audio_queue_import(const char *name) {
#define AUDIO_QUEUE(api,function) if (!strcmp(name,"_" #api)) return (Word)function
    AUDIO_QUEUE(AudioQueueNewOutput,audio_queue_new); AUDIO_QUEUE(AudioQueueAllocateBuffer,audio_queue_allocate);
    AUDIO_QUEUE(AudioQueueAllocateBufferWithPacketDescriptions,audio_queue_allocate_descriptions); AUDIO_QUEUE(AudioQueueFreeBuffer,audio_queue_free_buffer);
    AUDIO_QUEUE(AudioQueueEnqueueBuffer,audio_queue_enqueue); AUDIO_QUEUE(AudioQueueSetProperty,audio_queue_property);
    AUDIO_QUEUE(AudioQueueSetOfflineRenderFormat,audio_queue_offline_format); AUDIO_QUEUE(AudioQueueOfflineRender,audio_queue_render);
    AUDIO_QUEUE(AudioQueueStart,audio_queue_start); AUDIO_QUEUE(AudioQueueStop,audio_queue_stop); AUDIO_QUEUE(AudioQueueFlush,audio_queue_flush);
    AUDIO_QUEUE(AudioQueueReset,audio_queue_reset); AUDIO_QUEUE(AudioQueueDispose,audio_queue_dispose);
#undef AUDIO_QUEUE
    return 0;
}
