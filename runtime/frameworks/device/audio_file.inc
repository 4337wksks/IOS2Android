#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaDataSource.h>

typedef int32_t (*AudioFileRead)(void *,int64_t,uint32_t,void *,uint32_t *);
typedef int64_t (*AudioFileSize)(void *);
struct AudioPacketDescription { int64_t offset; uint32_t frames,size; };
struct AudioFileBridge {
    pthread_mutex_t io_mutex,mutex;
    void *context;
    AudioFileRead read;
    AudioFileSize get_size;
    AMediaDataSource *source;
    AMediaExtractor *extractor;
    AMediaFormat *media_format;
    struct GuestAudioFormat format;
    int64_t *packet_times;
    uint64_t packet_count,packet_capacity,current_packet,data_bytes;
    uint32_t max_packet,priming,remainder;
    int64_t valid_frames;
    double duration;
    int closed,read_failed;
};
static ssize_t audio_source_read(void *context,off64_t offset,void *buffer,size_t size) {
    struct AudioFileBridge *file=context;
    if (!size) return 0;
    if (offset<0 || __atomic_load_n(&file->closed,__ATOMIC_ACQUIRE)) return -1;
    if (size>UINT32_MAX) size=UINT32_MAX;
    pthread_mutex_lock(&file->io_mutex);
    uint32_t actual=0; int32_t result=-1;
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};
    char previous_stage[sizeof(stage)]; memcpy(previous_stage,stage,sizeof(stage));
    active_guest_call_boundary=&call;
    if (!setjmp(call.environment)) {
        snprintf(stage,sizeof(stage),"AudioFile read callback");
        result=file->read(file->context,offset,(uint32_t)size,buffer,&actual);
    } else LOG("AudioFile read callback stopped: %s",status);
    active_guest_call_boundary=call.previous; memcpy(stage,previous_stage,sizeof(stage));
    pthread_mutex_unlock(&file->io_mutex);
    if (result || actual>size) { __atomic_store_n(&file->read_failed,1,__ATOMIC_RELEASE); return -1; }
    return actual ? (ssize_t)actual : -1;
}
static ssize_t audio_source_size(void *context) {
    struct AudioFileBridge *file=context;
    pthread_mutex_lock(&file->io_mutex);
    int64_t size=-1;
    struct GuestCallBoundary call={.previous=active_guest_call_boundary};
    char previous_stage[sizeof(stage)]; memcpy(previous_stage,stage,sizeof(stage));
    active_guest_call_boundary=&call;
    if (!setjmp(call.environment)) {
        snprintf(stage,sizeof(stage),"AudioFile size callback");
        size=file->get_size(file->context);
    } else LOG("AudioFile size callback stopped: %s",status);
    active_guest_call_boundary=call.previous; memcpy(stage,previous_stage,sizeof(stage));
    pthread_mutex_unlock(&file->io_mutex);
    return size>=0 && (uint64_t)size<=SSIZE_MAX ? (ssize_t)size : -1;
}
static void audio_source_close(void *context) { __atomic_store_n(&((struct AudioFileBridge *)context)->closed,1,__ATOMIC_RELEASE); }
static int32_t audio_file_close(struct AudioFileBridge *file) {
    if (!file) return -50;
    media_status_t deleted=file->extractor ? AMediaExtractor_delete(file->extractor) : AMEDIA_OK;
    if (file->source) AMediaDataSource_delete(file->source);
    if (file->media_format) AMediaFormat_delete(file->media_format);
    free(file->packet_times); pthread_mutex_destroy(&file->io_mutex); pthread_mutex_destroy(&file->mutex); free(file);
    return deleted==AMEDIA_OK ? 0 : 0x6f703f3f;
}
static int32_t audio_file_open(void *context,AudioFileRead read,Word write,AudioFileSize get_size,Word set_size,
                               uint32_t hint,struct AudioFileBridge **output) {
    if (!output) return -50;
    *output=NULL;
    if (!read || !get_size) return -50;
    if (write || set_size) return 0x6f703f3f;
    struct AudioFileBridge *file=calloc(1,sizeof(*file)); if (!file) return -108;
    pthread_mutex_init(&file->io_mutex,NULL); pthread_mutex_init(&file->mutex,NULL);
    file->context=context; file->read=read; file->get_size=get_size;
    file->source=AMediaDataSource_new(); file->extractor=AMediaExtractor_new();
    int32_t error=0x7479703f;
    if (!file->source || !file->extractor) { error=-108; goto failed; }
    AMediaDataSource_setUserdata(file->source,file);
    AMediaDataSource_setReadAt(file->source,audio_source_read);
    AMediaDataSource_setGetSize(file->source,audio_source_size);
    AMediaDataSource_setClose(file->source,audio_source_close);
    if (AMediaExtractor_setDataSourceCustom(file->extractor,file->source)!=AMEDIA_OK) goto failed;
    for (size_t track=0;track<AMediaExtractor_getTrackCount(file->extractor);++track) {
        AMediaFormat *format=AMediaExtractor_getTrackFormat(file->extractor,track); const char *mime=NULL;
        if (!format) continue;
        uint32_t type=0,frames=0;
        if (AMediaFormat_getString(format,"mime",&mime)) {
            if (!strcmp(mime,"audio/mp4a-latm")) { type=0x61616320; frames=1024; }
            else if (!strcmp(mime,"audio/mpeg")) { type=0x2e6d7033; frames=1152; }
        }
        int32_t rate=0,channels=0,profile=0;
        if (!type || !AMediaFormat_getInt32(format,"sample-rate",&rate) || !AMediaFormat_getInt32(format,"channel-count",&channels) ||
            rate<8000 || rate>192000 || channels<1 || channels>2 || AMediaExtractor_selectTrack(file->extractor,track)!=AMEDIA_OK) {
            AMediaFormat_delete(format); continue;
        }
        if (type==0x61616320) {
            if (!AMediaFormat_getInt32(format,"aac-profile",&profile)) {
                void *cookie=NULL; size_t length=0;
                if (AMediaFormat_getBuffer(format,"csd-0",&cookie,&length) && length) profile=(*(const uint8_t *)cookie>>3)&31;
            }
            if (profile==5 || profile==29) { type=0x61616368; frames=2048; }
        }
        if (type==0x2e6d7033 && rate<32000) frames=576;
        file->media_format=format; file->format=(struct GuestAudioFormat){rate,type,0,0,frames,0,(uint32_t)channels,0,0}; break;
    }
    if (!file->media_format) goto failed;
    int64_t duration=0; int32_t delay=0,padding=0;
    AMediaFormat_getInt64(file->media_format,"durationUs",&duration);
    AMediaFormat_getInt32(file->media_format,"encoder-delay",&delay);
    AMediaFormat_getInt32(file->media_format,"encoder-padding",&padding);
    file->priming=delay>0 ? (uint32_t)delay : 0; file->remainder=padding>0 ? (uint32_t)padding : 0;
    while (AMediaExtractor_getSampleTrackIndex(file->extractor)>=0) {
        ssize_t size=AMediaExtractor_getSampleSize(file->extractor);
        int64_t time=AMediaExtractor_getSampleTime(file->extractor);
        if (size<=0 || (uint64_t)size>UINT32_MAX || time<0 || (AMediaExtractor_getSampleFlags(file->extractor)&AMEDIAEXTRACTOR_SAMPLE_FLAG_ENCRYPTED)) goto failed;
        if (file->packet_count==file->packet_capacity) {
            if (file->packet_capacity>SIZE_MAX/sizeof(int64_t)/2) { error=-108; goto failed; }
            uint64_t capacity=file->packet_capacity ? file->packet_capacity*2 : 256;
            if (capacity>SIZE_MAX/sizeof(int64_t)) { error=-108; goto failed; }
            int64_t *times=realloc(file->packet_times,(size_t)capacity*sizeof(*times)); if (!times) { error=-108; goto failed; }
            file->packet_times=times; file->packet_capacity=capacity;
        }
        file->packet_times[file->packet_count++]=time;
        file->data_bytes+=(uint64_t)size; if ((uint32_t)size>file->max_packet) file->max_packet=(uint32_t)size;
        if (!AMediaExtractor_advance(file->extractor)) break;
    }
    if (!file->packet_count || AMediaExtractor_seekTo(file->extractor,file->packet_times[0],AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC)!=AMEDIA_OK ||
        __atomic_load_n(&file->read_failed,__ATOMIC_ACQUIRE)) goto failed;
    file->duration=duration>0 ? (double)duration*1e-6 : (double)file->packet_count*file->format.frames_per_packet/file->format.rate;
    file->valid_frames=(int64_t)llround(file->duration*file->format.rate);
    LOG("AudioFile opened: %.0f Hz, %u channels, %lu packets, max packet=%u",file->format.rate,file->format.channels,file->packet_count,file->max_packet);
    *output=file; return 0;
failed:
    audio_file_close(file); return error;
}
static int32_t audio_file_property_value(struct AudioFileBridge *file,uint32_t property,const void **value,uint32_t *size,
                                         unsigned char storage[64]) {
    memset(storage,0,64); *value=storage;
    switch (property) {
        case 0x64666d74: *value=&file->format; *size=40; break;
        case 0x666c7374: {
            uint32_t layout=file->format.channels==1 ? 0x00640001 : 0x00650002;
            memcpy(storage,&file->format,40); memcpy(storage+40,&layout,4); *size=48; break;
        }
        case 0x70737a65: case 0x706b7562: memcpy(storage,&file->max_packet,4); *size=4; break;
        case 0x62636e74: memcpy(storage,&file->data_bytes,8); *size=8; break;
        case 0x70636e74: memcpy(storage,&file->packet_count,8); *size=8; break;
        case 0x65647572: memcpy(storage,&file->duration,8); *size=8; break;
        case 0x706e666f:
            memcpy(storage,&file->valid_frames,8); memcpy(storage+8,&file->priming,4); memcpy(storage+12,&file->remainder,4); *size=16; break;
        case 0x6d676963: {
            void *cookie=NULL; size_t length=0;
            AMediaFormat_getBuffer(file->media_format,"csd-0",&cookie,&length);
            if (length>UINT32_MAX) return 0x7074793f;
            *value=cookie; *size=(uint32_t)length; break;
        }
        default: return 0x7074793f;
    }
    return 0;
}
static int32_t audio_file_property_info(struct AudioFileBridge *file,uint32_t property,uint32_t *size,uint32_t *writable) {
    if (!file) return -50;
    pthread_mutex_lock(&file->mutex);
    unsigned char storage[64]; const void *value; uint32_t length=0;
    int32_t result=audio_file_property_value(file,property,&value,&length,storage);
    if (!result) { if (size) *size=length; if (writable) *writable=0; }
    pthread_mutex_unlock(&file->mutex); return result;
}
static int32_t audio_file_property(struct AudioFileBridge *file,uint32_t property,uint32_t *size,void *output) {
    if (!file || !size || (!output && *size)) return -50;
    pthread_mutex_lock(&file->mutex);
    unsigned char storage[64]; const void *value; uint32_t length=0;
    int32_t result=audio_file_property_value(file,property,&value,&length,storage);
    if (!result) {
        if (*size<length) { *size=length; result=0x2173697a; }
        else { if (length) memcpy(output,value,length); *size=length; }
    }
    pthread_mutex_unlock(&file->mutex); return result;
}
static int32_t audio_file_packets(struct AudioFileBridge *file,unsigned char cache,uint32_t *bytes,struct AudioPacketDescription *descriptions,
                                  int64_t starting,uint32_t *packets,void *output) {
    if (!file || !bytes || !packets || starting<0 || (!output && *bytes)) return -50;
    uint32_t capacity=*bytes,wanted=*packets; *bytes=*packets=0;
    pthread_mutex_lock(&file->mutex);
    int32_t result=0;
    if ((uint64_t)starting>=file->packet_count || !wanted) goto done;
    if ((uint64_t)starting!=file->current_packet) {
        if (AMediaExtractor_seekTo(file->extractor,file->packet_times[starting],AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC)!=AMEDIA_OK ||
            AMediaExtractor_getSampleTime(file->extractor)!=file->packet_times[starting]) { result=0x70636b3f; goto done; }
        file->current_packet=(uint64_t)starting;
    }
    while (*packets<wanted && file->current_packet<file->packet_count) {
        ssize_t size=AMediaExtractor_getSampleSize(file->extractor);
        if (size<=0 || (uint64_t)size>UINT32_MAX) { result=0x70636b3f; break; }
        if ((uint64_t)size>capacity-*bytes) { if (!*packets) result=0x2173697a; break; }
        ssize_t read=AMediaExtractor_readSampleData(file->extractor,(uint8_t *)output+*bytes,(size_t)size);
        if (read!=size) { result=0x70636b3f; break; }
        if (descriptions) descriptions[*packets]=(struct AudioPacketDescription){*bytes,0,(uint32_t)size};
        *bytes+=(uint32_t)size; ++*packets; ++file->current_packet;
        if (!AMediaExtractor_advance(file->extractor)) break;
    }
done:
    pthread_mutex_unlock(&file->mutex); return result;
}
static Word resolve_audio_file_import(const char *name) {
    if (!strcmp(name,"_AudioFileOpenWithCallbacks")) return (Word)audio_file_open;
    if (!strcmp(name,"_AudioFileClose")) return (Word)audio_file_close;
    if (!strcmp(name,"_AudioFileGetPropertyInfo")) return (Word)audio_file_property_info;
    if (!strcmp(name,"_AudioFileGetProperty")) return (Word)audio_file_property;
    if (!strcmp(name,"_AudioFileReadPacketData")) return (Word)audio_file_packets;
    return 0;
}
