 
#include <aaudio/AAudio.h>
struct GuestAudioDescription { uint32_t type,subtype,manufacturer,flags,mask; };
struct GuestAudioFormat {
    double rate;
    uint32_t format,flags,bytes_per_packet,frames_per_packet,bytes_per_frame,channels,bits,reserved;
};
struct GuestAudioTimestamp {
    double sample_time;
    uint64_t host_time;
    double rate_scalar;
    uint64_t word_clock_time;
    unsigned char smpte[24];
    uint32_t flags,reserved;
};
struct GuestAudioBuffer { uint32_t channels,size; void *data; };
struct GuestAudioBufferList { uint32_t count,padding; struct GuestAudioBuffer buffers[8]; };
typedef int32_t (*GuestAudioRender)(void *,uint32_t *,const struct GuestAudioTimestamp *,uint32_t,uint32_t,struct GuestAudioBufferList *);
struct GuestAudioCallback { GuestAudioRender function; void *context; };
struct GuestAudioUnit {
    pthread_mutex_t mutex;
    struct GuestAudioFormat format;
    struct GuestAudioCallback callback;
    AAudioStream *stream;
    void *planar;
    uint32_t max_frames,callback_frames;
    uint64_t frames;
    int initialized,running,failed,logged,nonzero_logged;
};
static const struct GuestAudioDescription remote_io_component={0x61756f75,0x72696f63,0x6170706c,0,0};
static _Thread_local struct GuestAudioUnit *active_audio_callback;
static unsigned guest_audio_outputs;
JNIEXPORT jint JNICALL HOST_JNI(AudioSession_ownedOutputs)(JNIEnv *env,jclass cls) { return __atomic_load_n(&guest_audio_outputs,__ATOMIC_ACQUIRE); }
static Word guest_audio_find_component(Word previous,const struct GuestAudioDescription *wanted) {
    if (previous || !wanted) return 0;
    if ((wanted->type && wanted->type!=remote_io_component.type) || (wanted->subtype && wanted->subtype!=remote_io_component.subtype) ||
        (wanted->manufacturer && wanted->manufacturer!=remote_io_component.manufacturer) || (wanted->flags&wanted->mask)) return 0;
    return (Word)&remote_io_component;
}
static int32_t guest_audio_component_new(Word component,struct GuestAudioUnit **output) {
    if (component!=(Word)&remote_io_component || !output) return -50;
    *output=NULL;
    struct GuestAudioUnit *unit=calloc(1,sizeof(*unit)); if (!unit) return -108;
    pthread_mutex_init(&unit->mutex,NULL);
    unit->max_frames=4096; unit->callback_frames=256;
    unit->format=(struct GuestAudioFormat){48000,0x6c70636d,12,4,1,4,2,16,0};
    *output=unit; return 0;
}
static int audio_format_supported(const struct GuestAudioFormat *format) {
    unsigned planar=(format->flags&32)!=0,bytes=format->bits/8;
    int pcm=(format->bits==16 && (format->flags&13)==12) || (format->bits==32 && (format->flags&13)==9);
    return format->format==0x6c70636d && pcm && !(format->flags&~109U) &&
        isfinite(format->rate) && format->rate>=8000 && format->rate<=192000 && floor(format->rate)==format->rate &&
        format->channels>=1 && format->channels<=8 && format->frames_per_packet==1 &&
        format->bytes_per_frame==bytes*(planar ? 1 : format->channels) && format->bytes_per_packet==format->bytes_per_frame;
}
static int32_t guest_audio_set_property(struct GuestAudioUnit *unit,uint32_t property,uint32_t scope,uint32_t element,const void *data,uint32_t size) {
    if (!unit || !data) return -50;
    pthread_mutex_lock(&unit->mutex);
    if (unit->initialized) { pthread_mutex_unlock(&unit->mutex); return -10849; }
    int32_t result=0;
    if (property==8) {
        if (scope!=1 || element!=0) result=-10866;
        else if (size!=sizeof(unit->format) || !audio_format_supported(data)) result=-10868;
        else memcpy(&unit->format,data,size);
    } else if (property==23) {
        if ((scope!=0 && scope!=1) || element!=0) result=-10866;
        else if (size!=sizeof(unit->callback)) result=-10878;
        else memcpy(&unit->callback,data,size);
    } else if (property==14) {
        if (scope!=0 || element!=0 || size!=4) result=-10878;
        else { uint32_t value; memcpy(&value,data,4); if (!value || value>65536) result=-10878; else { unit->max_frames=value; if (unit->callback_frames>value) unit->callback_frames=value; } }
    } else if (property==2003 && size==4) {
        uint32_t enabled; memcpy(&enabled,data,4);
        if (!((scope==2 && element==0 && enabled) || (scope==1 && element==1 && !enabled))) result=-10879;
    } else result=-10879;
    pthread_mutex_unlock(&unit->mutex);
    if (result) LOG("AudioUnit property unsupported/invalid: property=%u scope=%u element=%u size=%u status=%d",property,scope,element,size,result);
    return result;
}
static aaudio_data_callback_result_t guest_audio_render(AAudioStream *stream,void *context,void *output,int32_t frames) {
    struct GuestAudioUnit *unit=context;
    unsigned channels=unit->format.channels,bytes=unit->format.bits/8;
    size_t total=(size_t)(frames>0 ? frames : 0)*channels*bytes;
    if (frames<=0 || (unsigned)frames>unit->max_frames || __atomic_load_n(&unit->failed,__ATOMIC_ACQUIRE)) {
        if (total) memset(output,0,total); return AAUDIO_CALLBACK_RESULT_STOP;
    }
    memset(output,0,total);
    struct GuestAudioBufferList list={0};
    int planar=(unit->format.flags&32)!=0;
    list.count=planar ? channels : 1;
    for (unsigned i=0;i<list.count;++i) list.buffers[i]=(struct GuestAudioBuffer){planar ? 1 : channels,
        (uint32_t)((size_t)frames*bytes*(planar ? 1 : channels)),planar ? (char *)unit->planar+(size_t)i*unit->max_frames*bytes : output};
    if (planar) for (unsigned i=0;i<channels;++i) memset(list.buffers[i].data,0,list.buffers[i].size);
    struct GuestAudioTimestamp timestamp={.sample_time=(double)unit->frames,.rate_scalar=1,.flags=1};
    int64_t position,nanoseconds;
    if (AAudioStream_getTimestamp(stream,CLOCK_MONOTONIC,&position,&nanoseconds)==AAUDIO_OK) {
        int64_t delta=AAudioStream_getFramesWritten(stream)-position;
        double predicted=(double)nanoseconds+(double)delta*1000000000.0/unit->format.rate;
        if (predicted>=0 && predicted<(double)UINT64_MAX) { timestamp.host_time=(uint64_t)predicted; timestamp.flags|=2; }
    }
    uint32_t flags=0;
    active_audio_callback=unit;
    if (setjmp(boundary)!=0) {
        may_unwind=0; active_audio_callback=NULL; memset(output,0,total);
        __atomic_store_n(&unit->failed,1,__ATOMIC_RELEASE);
        LOG("AudioUnit original callback stopped: %s",status); return AAUDIO_CALLBACK_RESULT_STOP;
    }
    may_unwind=1;
    memcpy(stage,"AudioUnit render callback",sizeof("AudioUnit render callback"));
    int32_t result=unit->callback.function ? unit->callback.function(unit->callback.context,&flags,&timestamp,0,(uint32_t)frames,&list) : -10876;
    may_unwind=0; active_audio_callback=NULL;
    if (result) {
        memset(output,0,total); __atomic_store_n(&unit->failed,1,__ATOMIC_RELEASE);
        LOG("AudioUnit render callback returned error=%d",result); return AAUDIO_CALLBACK_RESULT_STOP;
    }
    if (list.count!=(planar ? channels : 1)) {
        memset(output,0,total); __atomic_store_n(&unit->failed,1,__ATOMIC_RELEASE);
        LOG("AudioUnit callback returned an invalid buffer count"); return AAUDIO_CALLBACK_RESULT_STOP;
    }
    if (flags&16) memset(output,0,total);
    else if (planar) {
        for (int32_t frame=0;frame<frames;++frame) for (unsigned channel=0;channel<channels;++channel)
            if (list.buffers[channel].data && list.buffers[channel].size>=(size_t)(frame+1)*bytes)
                memcpy((char *)output+((size_t)frame*channels+channel)*bytes,(char *)list.buffers[channel].data+(size_t)frame*bytes,bytes);
    } else {
        size_t valid=list.buffers[0].size<total ? list.buffers[0].size : total;
        valid-=valid%(channels*bytes);
        if (list.buffers[0].data && list.buffers[0].data!=output) memmove(output,list.buffers[0].data,valid);
        if (!list.buffers[0].data) valid=0;
        if (valid<total) memset((char *)output+valid,0,total-valid);
    }
    unit->frames+=(unsigned)frames;
    if (!unit->logged++) LOG("Original AudioUnit render callback running: %d frames, %.0f Hz, %u channels",frames,unit->format.rate,channels);
    if (!unit->nonzero_logged) {
        const unsigned char *data=output;
        for (size_t i=0;i<total;++i) if (data[i]) { unit->nonzero_logged=1; LOG("Original AudioUnit produced nonzero PCM output"); break; }
    }
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}
static void guest_audio_error(AAudioStream *stream,void *context,aaudio_result_t error) {
    struct GuestAudioUnit *unit=context;
    __atomic_store_n(&unit->failed,1,__ATOMIC_RELEASE);
    LOG("AAudio output error=%d (%s)",error,AAudio_convertResultToText(error));
}
static int32_t guest_audio_initialize(struct GuestAudioUnit *unit) {
    _Static_assert(sizeof(struct GuestAudioFormat)==40,"Darwin ASBD layout");
    _Static_assert(sizeof(struct GuestAudioTimestamp)==64,"Darwin AudioTimeStamp layout");
    _Static_assert(offsetof(struct GuestAudioBufferList,buffers)==8,"Darwin AudioBufferList layout");
    if (!unit) return -50;
    pthread_mutex_lock(&unit->mutex);
    if (unit->initialized) { pthread_mutex_unlock(&unit->mutex); return 0; }
    if (!unit->callback.function || !audio_format_supported(&unit->format)) { pthread_mutex_unlock(&unit->mutex); return -10868; }
    AAudioStreamBuilder *builder=NULL;
    aaudio_result_t result=AAudio_createStreamBuilder(&builder);
    if (result==AAUDIO_OK) {
        AAudioStreamBuilder_setDirection(builder,AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setSharingMode(builder,AAUDIO_SHARING_MODE_SHARED);
        AAudioStreamBuilder_setPerformanceMode(builder,AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setUsage(builder,AAUDIO_USAGE_GAME);
        AAudioStreamBuilder_setContentType(builder,AAUDIO_CONTENT_TYPE_MUSIC);
        AAudioStreamBuilder_setFormat(builder,(unit->format.flags&1) ? AAUDIO_FORMAT_PCM_FLOAT : AAUDIO_FORMAT_PCM_I16);
        AAudioStreamBuilder_setChannelCount(builder,(int32_t)unit->format.channels);
        AAudioStreamBuilder_setSampleRate(builder,(int32_t)unit->format.rate);
        AAudioStreamBuilder_setFramesPerDataCallback(builder,(int32_t)unit->callback_frames);
        AAudioStreamBuilder_setDataCallback(builder,guest_audio_render,unit);
        AAudioStreamBuilder_setErrorCallback(builder,guest_audio_error,unit);
        result=AAudioStreamBuilder_openStream(builder,&unit->stream);
        AAudioStreamBuilder_delete(builder);
    }
    if (result==AAUDIO_OK && (AAudioStream_getSampleRate(unit->stream)!=(int32_t)unit->format.rate ||
        AAudioStream_getChannelCount(unit->stream)!=(int32_t)unit->format.channels ||
        AAudioStream_getFormat(unit->stream)!=((unit->format.flags&1) ? AAUDIO_FORMAT_PCM_FLOAT : AAUDIO_FORMAT_PCM_I16))) result=AAUDIO_ERROR_INVALID_FORMAT;
    if (result==AAUDIO_OK && (unit->format.flags&32)) {
        unit->planar=malloc((size_t)unit->max_frames*unit->format.channels*(unit->format.bits/8));
        if (!unit->planar) result=AAUDIO_ERROR_NO_MEMORY;
    }
    if (result!=AAUDIO_OK) {
        if (unit->stream) AAudioStream_close(unit->stream);
        unit->stream=NULL; free(unit->planar); unit->planar=NULL;
        pthread_mutex_unlock(&unit->mutex);
        LOG("AAudio initialization failed: %d (%s)",result,AAudio_convertResultToText(result)); return -10875;
    }
    unit->initialized=1; unit->frames=0; unit->failed=0;
    __atomic_fetch_add(&guest_audio_outputs,1,__ATOMIC_RELEASE);
    LOG("AudioUnit RemoteIO initialized via AAudio: %.0f Hz, %u channels, %u-bit, callback=%u, burst=%d",unit->format.rate,unit->format.channels,unit->format.bits,unit->callback_frames,AAudioStream_getFramesPerBurst(unit->stream));
    pthread_mutex_unlock(&unit->mutex); return 0;
}
static int32_t guest_audio_start(struct GuestAudioUnit *unit) {
    if (!unit) return -50;
    pthread_mutex_lock(&unit->mutex);
    if (!unit->initialized) { pthread_mutex_unlock(&unit->mutex); return -10867; }
    if (unit->running) { pthread_mutex_unlock(&unit->mutex); return 0; }
    aaudio_result_t result=AAudioStream_requestStart(unit->stream);
    if (result==AAUDIO_OK) unit->running=1;
    pthread_mutex_unlock(&unit->mutex);
    LOG("AudioUnit output start: AAudio result=%d",result);
    return result==AAUDIO_OK ? 0 : -10875;
}
static int32_t guest_audio_stop(struct GuestAudioUnit *unit) {
    if (!unit) return -50;
    if (active_audio_callback==unit) return -10863;
    pthread_mutex_lock(&unit->mutex);
    if (!unit->stream || !unit->running) { pthread_mutex_unlock(&unit->mutex); return 0; }
    aaudio_result_t result=AAudioStream_requestStop(unit->stream);
    if (result==AAUDIO_OK) {
        for (unsigned i=0;i<20;++i) {
            aaudio_stream_state_t state=AAudioStream_getState(unit->stream),next;
            if (state!=AAUDIO_STREAM_STATE_STARTING && state!=AAUDIO_STREAM_STATE_STARTED && state!=AAUDIO_STREAM_STATE_STOPPING) break;
            result=AAudioStream_waitForStateChange(unit->stream,state,&next,100000000);
            if (result!=AAUDIO_OK && result!=AAUDIO_ERROR_TIMEOUT) break;
        }
        aaudio_stream_state_t final_state=AAudioStream_getState(unit->stream);
        if (final_state==AAUDIO_STREAM_STATE_STARTING || final_state==AAUDIO_STREAM_STATE_STARTED || final_state==AAUDIO_STREAM_STATE_STOPPING) result=AAUDIO_ERROR_TIMEOUT;
        else if (result==AAUDIO_ERROR_TIMEOUT) result=AAUDIO_OK;
        if (result==AAUDIO_OK) unit->running=0;
    }
    pthread_mutex_unlock(&unit->mutex); return result==AAUDIO_OK ? 0 : -10863;
}
static int32_t guest_audio_uninitialize(struct GuestAudioUnit *unit) {
    if (!unit) return -50;
    if (active_audio_callback==unit) return -10863;
    pthread_mutex_lock(&unit->mutex);
    aaudio_result_t result=AAUDIO_OK;
    if (unit->stream) result=AAudioStream_close(unit->stream);
    if (result==AAUDIO_OK) {
        if (unit->initialized) __atomic_fetch_sub(&guest_audio_outputs,1,__ATOMIC_RELEASE);
        unit->stream=NULL; unit->initialized=0; unit->running=0;
        free(unit->planar); unit->planar=NULL;
    }
    pthread_mutex_unlock(&unit->mutex); return result==AAUDIO_OK ? 0 : -10863;
}
static int32_t guest_audio_dispose(struct GuestAudioUnit *unit) {
    int32_t result=guest_audio_uninitialize(unit);
    if (!result) { pthread_mutex_destroy(&unit->mutex); free(unit); }
    return result;
}
static Word resolve_audio_unit_import(const char *name) {
    if (!strcmp(name,"_AudioComponentFindNext")) return (Word)guest_audio_find_component;
    if (!strcmp(name,"_AudioComponentInstanceNew")) return (Word)guest_audio_component_new;
    if (!strcmp(name,"_AudioComponentInstanceDispose")) return (Word)guest_audio_dispose;
    if (!strcmp(name,"_AudioUnitSetProperty")) return (Word)guest_audio_set_property;
    if (!strcmp(name,"_AudioUnitInitialize")) return (Word)guest_audio_initialize;
    if (!strcmp(name,"_AudioUnitUninitialize")) return (Word)guest_audio_uninitialize;
    if (!strcmp(name,"_AudioOutputUnitStart")) return (Word)guest_audio_start;
    if (!strcmp(name,"_AudioOutputUnitStop")) return (Word)guest_audio_stop;
    return 0;
}
