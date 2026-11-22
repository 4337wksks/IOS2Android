 
#define VKD(name) ({ static PFN_##name cached; PFN_##name fn=__atomic_load_n(&cached,__ATOMIC_ACQUIRE); if (!fn) { fn=(PFN_##name)vulkan.get_device_proc(vulkan.device,#name); __atomic_store_n(&cached,fn,__ATOMIC_RELEASE); } fn; })
struct MetalDiagnosticCapture { struct MetalBuffer buffer; uint32_t width,height,format; const char *name; struct MetalDiagnosticCapture *next; };
static struct MetalBuffer *metal_math_buffer(struct MetalCommand *command) {
#if I2A_DEVELOPMENT && I2A_SHADER_DIAGNOSTIC_COUNT
    if (!vulkan.features.fragmentStoresAndAtomics) stop("Shader diagnostics require fragment storage atomics","diagnostic build");
    if (!command->math_diagnostics) {
        struct MetalBuffer *buffer=command->pool_owner->math_diagnostics;
        if (!buffer) {
            buffer=calloc(1,sizeof(*buffer));
            size_t size=((1+I2A_SHADER_DIAGNOSTIC_COUNT*8)*sizeof(uint32_t)+15)&~(size_t)15;
            if (!buffer || metal_buffer_allocate(buffer,size,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0)!=VK_SUCCESS)
                stop("Cannot allocate shader diagnostic buffer","diagnostic build");
            command->pool_owner->math_diagnostics=buffer;
        }
        memset(buffer->mapped,0,buffer->length);
        *(uint32_t *)buffer->mapped=__atomic_load_n(&shader_diagnostics_requested,__ATOMIC_ACQUIRE)!=0;
        command->math_diagnostics=buffer;
    }
    return command->math_diagnostics;
#else
    stop("Diagnostic shader in normal package","shader binding");
#endif
}
static void metal_math_complete(struct MetalCommand *command) {
#if I2A_DEVELOPMENT && I2A_SHADER_DIAGNOSTIC_COUNT
    static unsigned observed[I2A_SHADER_DIAGNOSTIC_COUNT],reported[I2A_SHADER_DIAGNOSTIC_COUNT];
    struct MetalBuffer *buffer=command->math_diagnostics;
    if (!buffer) return;
    uint32_t *words=buffer->mapped;
    for (unsigned i=0;words[0] && i<I2A_SHADER_DIAGNOSTIC_COUNT;++i) {
        uint32_t *row=words+1+i*8;
        if (!row[0]) continue;
        unsigned first=!__atomic_exchange_n(&observed[i],1,__ATOMIC_RELAXED);
        unsigned issue=(row[1] || row[2]) && __atomic_fetch_add(&reported[i],1,__ATOMIC_RELAXED)<4;
        if (first || issue) {
            float x,y,value; memcpy(&x,row+4,4); memcpy(&y,row+5,4); memcpy(&value,row+6,4);
            const struct ShaderDiagnosticSite *site=&shader_diagnostic_sites[i];
            LOG("[BUSINESS-shader] site=%u shader=%s op=%s line=%u samples=%u domain=%u nonfinite=%u x=%g y=%g value=%g",i,site->shader,site->operation,site->line,row[0],row[1],row[2],x,y,value);
        }
    }
    command->math_diagnostics=NULL;
#endif
}
static void metal_diagnostic_command_begin(struct MetalCommand *command) {
    if (!metal_diagnostic_enabled || !vulkan.timestamp_bits) return;
    struct MetalCommandPool *pool=command->pool_owner;
    if (!pool->timestamps) {
        VkQueryPoolCreateInfo info={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,.queryType=VK_QUERY_TYPE_TIMESTAMP,.queryCount=2};
        if (VKD(vkCreateQueryPool)(vulkan.device,&info,NULL,&pool->timestamps)!=VK_SUCCESS) return;
    }
    VKD(vkCmdResetQueryPool)(command->command,pool->timestamps,0,2);
    VKD(vkCmdWriteTimestamp)(command->command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool->timestamps,0);
    command->diagnostic_timestamps=1;
}
static void metal_diagnostic_command_end(struct MetalCommand *command) {
    if (command->math_diagnostics) {
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=command->math_diagnostics->buffer,.size=VK_WHOLE_SIZE};
        VKD(vkCmdPipelineBarrier)(command->command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);
    }
    if (command->diagnostic_timestamps)
        VKD(vkCmdWriteTimestamp)(command->command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,command->pool_owner->timestamps,1);
}
static void metal_diagnostic_command_complete(struct MetalCommand *command) {
    metal_math_complete(command);
    if (command->diagnostic_timestamps) {
        uint64_t ticks[2];
        if (VKD(vkGetQueryPoolResults)(vulkan.device,command->pool_owner->timestamps,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS) {
            uint64_t mask=vulkan.timestamp_bits>=64 ? UINT64_MAX : (1ULL<<vulkan.timestamp_bits)-1;
            metal_diagnostic_add(2,(uint64_t)(((ticks[1]-ticks[0])&mask)*vulkan.properties.limits.timestampPeriod));
        }
    }
    while (command->captures) {
        struct MetalDiagnosticCapture *capture=command->captures; command->captures=capture->next;
        char path[PATH_MAX]; snprintf(path,sizeof(path),"%s/%s-%ux%u-vk%u.raw",metal_diagnostic_directory,capture->name,capture->width,capture->height,capture->format);
        FILE *output=fopen(path,"wb");
        if (output) { size_t size=fwrite(capture->buffer.mapped,1,capture->buffer.length,output); fclose(output); LOG("[DIAG-render] GPU target captured: %s bytes=%zu",path,size); }
        metal_buffer_dispose(&capture->buffer); free(capture);
    }
}
static void metal_diagnostic_capture(struct MetalRenderEncoder *encoder) {
    if (!metal_diagnostic_readback || __atomic_load_n(&metal_diagnostic_frames,__ATOMIC_RELAXED)<60 || !encoder->state->pipeline || !encoder->state->pipeline->fragment || !encoder->state->color_count) return;
    const struct ShaderSpec *spec=encoder->state->pipeline->fragment->spec;
    unsigned index=(unsigned)(spec-shader_specs);
    static unsigned taken[COUNT(shader_specs)],total;
    if (__atomic_load_n(&total,__ATOMIC_RELAXED)>=16 || __atomic_exchange_n(&taken[index],1,__ATOMIC_RELAXED)) return;
    unsigned attachment=encoder->state->colors[0].attachment;
    if (attachment==VK_ATTACHMENT_UNUSED) return;
    struct MetalTexture *texture=encoder->state->attachments[attachment];
    if (texture->mips!=1 || texture->layers!=1) return;
    unsigned pixel_bytes=texture->format.format==VK_FORMAT_R16G16B16A16_SFLOAT ? 8 :
        texture->format.format==VK_FORMAT_R8G8B8A8_UNORM || texture->format.format==VK_FORMAT_R8G8B8A8_SRGB ||
        texture->format.format==VK_FORMAT_B8G8R8A8_UNORM || texture->format.format==VK_FORMAT_B8G8R8A8_SRGB ? 4 : 0;
    if (!pixel_bytes) { LOG("[BUSINESS-capture] Unsupported format: %s format=%u",spec->name,texture->format.format); return; }
    if (__atomic_fetch_add(&total,1,__ATOMIC_RELAXED)>=16) return;
    struct MetalDiagnosticCapture *capture=calloc(1,sizeof(*capture)); if (!capture) return;
    size_t size=texture->width*texture->height*pixel_bytes;
    if (metal_buffer_allocate(&capture->buffer,size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,VK_MEMORY_PROPERTY_HOST_CACHED_BIT)!=VK_SUCCESS) { free(capture); return; }
    capture->width=(uint32_t)texture->width; capture->height=(uint32_t)texture->height; capture->format=texture->format.format; capture->name=spec->name;
    metal_render_transition(encoder,texture,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={capture->width,capture->height,1}};
    VKD(vkCmdCopyImageToBuffer)(encoder->command->command,texture->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,capture->buffer.buffer,1,&copy);
    VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=capture->buffer.buffer,.size=VK_WHOLE_SIZE};
    VKD(vkCmdPipelineBarrier)(encoder->command->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&barrier,0,NULL);
    capture->next=encoder->command->captures; encoder->command->captures=capture;
}
static void metal_diagnostic_present(void) {
    if (!metal_diagnostic_enabled) return;
    __atomic_fetch_add(&metal_diagnostic_frames,1,__ATOMIC_RELAXED);
    static uint64_t start; static unsigned frames; uint64_t now=metal_diagnostic_time();
    if (!start) start=now;
    ++frames; if (now-start<5000000000ULL) return;
    uint64_t memory[6]; for (unsigned i=0;i<6;++i) memory[i]=__atomic_load_n(&metal_memory_census[i],__ATOMIC_RELAXED);
    LOG("[DIAG-memory] bufferBacking=%llu textureBacking=%llu descriptorObjects=%llu renderEncoders=%llu commandObjects=%llu guestBuffers=%llu",
        (unsigned long long)memory[0],(unsigned long long)memory[1],(unsigned long long)memory[2],(unsigned long long)memory[3],(unsigned long long)memory[4],(unsigned long long)memory[5]);
    uint64_t ns[3],calls[3]; for (unsigned i=0;i<3;++i) {
        ns[i]=__atomic_exchange_n(&metal_diagnostic_ns[i],0,__ATOMIC_RELAXED);
        calls[i]=__atomic_exchange_n(&metal_diagnostic_calls[i],0,__ATOMIC_RELAXED);
    }
    LOG("[DIAG-render] Work per presented frame: frames=%u CPU pipeline=%.3fms descriptors=%.3fms draws=%llu GPU command spans=%.3fms commands=%llu timestampBits=%u",frames,
        ns[0]*1e-6/frames,ns[1]*1e-6/frames,(unsigned long long)calls[1],ns[2]*1e-6/frames,(unsigned long long)calls[2],vulkan.timestamp_bits);
#if I2A_DEVELOPMENT
    uint64_t reused=__atomic_exchange_n(&metal_descriptor_cache_counts[0],0,__ATOMIC_RELAXED);
    uint64_t created=__atomic_exchange_n(&metal_descriptor_cache_counts[1],0,__ATOMIC_RELAXED);
    LOG("[DIAG-render] Descriptor snapshots: reused=%llu created=%llu",(unsigned long long)reused,(unsigned long long)created);
    uint64_t planned=__atomic_exchange_n(&metal_layout_counts[0],0,__ATOMIC_RELAXED);
    uint64_t passes=__atomic_exchange_n(&metal_layout_counts[1],0,__ATOMIC_RELAXED);
    LOG("[DIAG-render] Render recording: plannedInitialLayouts=%llu passBegins=%llu",(unsigned long long)planned,(unsigned long long)passes);
#endif
    start=now; frames=0;
}
#undef VKD
