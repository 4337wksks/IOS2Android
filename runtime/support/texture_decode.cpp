/* ABI boundary for the MIT-licensed Imagination reference PVRTC decoder.
 * This runs at texture upload, never while submitting a draw. */
#include "PVRTDecompress.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" __attribute__((visibility("hidden"))) int i2a_decode_pvrtc(
    const void *source, size_t source_size, uint32_t bits, uint32_t width,
    uint32_t height, int rgb, void *rgba) {
    if (!source || !rgba || (bits!=2 && bits!=4) || !width || !height ||
        (width&(width-1)) || (height&(height-1)) || uint64_t(width)*height>UINT32_MAX/4) return 0;
    uint32_t min_width=bits==2 ? 16 : 8;
    uint32_t stored_width=width<min_width ? min_width : width,stored_height=height<8 ? 8 : height;
    size_t encoded_size=size_t(stored_width)*stored_height*bits/8;
    if (source_size<encoded_size) return 0;
    try {
        std::vector<uint32_t> aligned;
        if (reinterpret_cast<uintptr_t>(source)&3) {
            aligned.resize(encoded_size/4); std::memcpy(aligned.data(),source,encoded_size); source=aligned.data();
        }
        uint32_t consumed=pvr::PVRTDecompressPVRTC(source,bits==2,width,height,static_cast<uint8_t *>(rgba));
        if (consumed!=encoded_size) return 0;
        if (rgb) for (size_t i=0;i<size_t(width)*height;++i) static_cast<uint8_t *>(rgba)[i*4+3]=255;
        return 1;
    } catch (...) { return 0; }
}
