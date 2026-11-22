 

static void put_big_endian32(unsigned char *bytes,uint32_t value) {
    bytes[0]=value>>24; bytes[1]=value>>16; bytes[2]=value>>8; bytes[3]=value;
}

static unsigned char *png_chunk(unsigned char *dest,const char *type,const void *bytes,uint32_t length) {
    put_big_endian32(dest,length);
    memcpy(dest+4,type,4);
    if (length) memcpy(dest+8,bytes,length);
    put_big_endian32(dest+8+length,(uint32_t)crc32(0,dest+4,length+4));
    return dest+length+12;
}

static unsigned char png_paeth(unsigned char left,unsigned char up,unsigned char corner) {
    int estimate=(int)left+up-corner;
    int a=abs(estimate-left), b=abs(estimate-up), c=abs(estimate-corner);
    return a<=b && a<=c ? left : b<=c ? up : corner;
}

static Word image_png(struct HostObject *image) {
    if (!image) return 0;
    if (image->content[3]) return image->content[3];
    struct HostObject *original=(void *)image->content[0];
    if (!original) return 0;
    const unsigned char *source=(const void *)data_buffer(original);
    size_t size=original->content[0], offset=8, compressed_size=0;
    static const unsigned char signature[]={137,80,78,71,13,10,26,10};
    if (size<8 || !source || memcmp(source,signature,sizeof(signature))) return 0;
    int optimized=0;
    unsigned char header[13]={0};
    while (size-offset>=12) {
        uint32_t length=big_endian32(source+offset);
        if (length>size-offset-12) return 0;
        const unsigned char *type=source+offset+4;
        if (!memcmp(type,"CgBI",4)) optimized=1;
        if (!memcmp(type,"IHDR",4) && length==13) memcpy(header,source+offset+8,13);
        if (!memcmp(type,"IDAT",4)) compressed_size+=length;
        offset+=length+12;
    }
    if (!optimized) return image->content[3]=(Word)original;
    uint32_t width=big_endian32(header), height=big_endian32(header+4);
    unsigned channels=header[9]==6 ? 4 : header[9]==2 ? 3 : 0;
    if (!width || !height || header[8]!=8 || !channels || header[10] || header[11] || header[12])
        stop("Unsupported CgBI PNG layout","UIImagePNGRepresentation");
    uint64_t row64=(uint64_t)width*channels+1, total64=row64*height;
    if (total64>UINT32_MAX || compressed_size>UINT32_MAX || !compressed_size)
        stop("PNG too large","UIImagePNGRepresentation");
    size_t row=(size_t)row64, total=(size_t)total64;
    unsigned char *compressed=malloc(compressed_size), *pixels=malloc(total);
    if (!compressed || !pixels) stop("Allocation failed","PNG normalization");
    offset=8;
    size_t copied=0;
    while (size-offset>=12) {
        uint32_t length=big_endian32(source+offset);
        if (!memcmp(source+offset+4,"IDAT",4)) {
            memcpy(compressed+copied,source+offset+8,length);
            copied+=length;
        }
        offset+=length+12;
    }
    z_stream decoder={.next_in=compressed,.avail_in=(uInt)compressed_size,.next_out=pixels,.avail_out=(uInt)total};
    if (inflateInit2(&decoder,-15)!=Z_OK) stop("PNG inflater failed","CgBI");
    int result=inflate(&decoder,Z_FINISH);
    int valid=result==Z_STREAM_END && decoder.total_out==total;
    inflateEnd(&decoder);
    free(compressed);
    if (!valid) { free(pixels); return 0; }
    for (uint32_t y=0;y<height;++y) {
        unsigned char *line=pixels+y*row;
        unsigned filter=line[0];
        if (filter>4) { free(pixels); return 0; }
        for (size_t x=1;x<row;++x) {
            unsigned char left=x>channels ? line[x-channels] : 0;
            unsigned char up=y ? (line-row)[x] : 0;
            unsigned char corner=y && x>channels ? (line-row)[x-channels] : 0;
            unsigned char prediction=filter==1 ? left : filter==2 ? up : filter==3 ? ((unsigned)left+up)/2 : filter==4 ? png_paeth(left,up,corner) : 0;
            line[x]=(unsigned char)(line[x]+prediction);
        }
        line[0]=0;
    }
     
    for (uint32_t y=0;y<height;++y) {
        unsigned char *line=pixels+y*row+1;
        for (uint32_t x=0;x<width;++x) {
            unsigned char *pixel=line+x*channels;
            unsigned char blue=pixel[0]; pixel[0]=pixel[2]; pixel[2]=blue;
            if (channels==4) {
                unsigned alpha=pixel[3];
                for (unsigned index=0;index<3;++index) {
                    unsigned value=alpha ? ((unsigned)pixel[index]*255+alpha/2)/alpha : 0;
                    pixel[index]=value>255 ? 255 : value;
                }
            }
        }
    }
    uLongf encoded_size=compressBound(total);
    unsigned char *encoded=malloc(encoded_size);
    if (!encoded) stop("Allocation failed","PNG compression");
    result=compress2(encoded,&encoded_size,pixels,total,Z_BEST_SPEED);
    free(pixels);
    if (result!=Z_OK || encoded_size>UINT32_MAX-57) { free(encoded); return 0; }
    struct HostObject *data=calloc(1,sizeof(*data)+encoded_size+57);
    if (!data) stop("Allocation failed","PNG output");
    data->isa=external_class("_OBJC_CLASS_$_NSData");
    data->content[0]=encoded_size+57;
    unsigned char *dest=(void *)(data+1);
    memcpy(dest,source,8);
    dest=png_chunk(dest+8,"IHDR",header,13);
    dest=png_chunk(dest,"IDAT",encoded,(uint32_t)encoded_size);
    png_chunk(dest,"IEND",NULL,0);
    free(encoded);
    image->content[3]=(Word)data;
    LOG("CgBI converted to standard PNG %ux%u (%lu bytes)",width,height,data->content[0]);
    return (Word)data;
}
