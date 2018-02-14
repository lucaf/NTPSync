//
//  ByteOrder.h
//
//  Author: Filippin luca
//  luca.filippin@gmail.com
//

#ifndef __BYTEORDER_H__
#define __BYTEORDER_H__

#include <stdint.h>

#if !defined(BO_INLINE)
    #if defined(__GNUC__) && (__GNUC__ == 4)
        #define BO_INLINE static __inline__ __attribute__((always_inline))
    #elif defined(__GNUC__)
        #define BO_INLINE static __inline__
    #elif defined(_MSC_VER)
        #define BO_INLINE static __inline
    #elif defined(__WIN32__)
        #define BO_INLINE static __inline__
    #endif
#endif

BO_INLINE uint32_t SwapInt32(uint32_t arg) {
    uint32_t result;
    result = ((arg & 0xFF) << 24) | ((arg & 0xFF00) << 8) | ((arg >> 8) & 0xFF00) | ((arg >> 24) & 0xFF);
    return result;
}

BO_INLINE uint64_t SwapInt64(uint64_t arg) {
    union Swap {
        uint64_t sv;
        uint32_t ul[2];
    } tmp, result;

    tmp.sv = arg;
    result.ul[0] = SwapInt32(tmp.ul[1]);
    result.ul[1] = SwapInt32(tmp.ul[0]);
    return result.sv;
}

BO_INLINE uint32_t SwapInt32BigToHost(uint32_t arg) {
#if __BIG_ENDIAN__
    return arg;
#else
    return SwapInt32(arg);
#endif
}

BO_INLINE uint64_t SwapInt64BigToHost(uint64_t arg) {
#if __BIG_ENDIAN__
    return arg;
#else
    return SwapInt64(arg);
#endif
}


BO_INLINE uint32_t SwapInt32HostToBig(uint32_t arg) {
#if __BIG_ENDIAN__
    return arg;
#else
    return SwapInt32(arg);
#endif
}

BO_INLINE uint64_t SwapInt64HostToBig(uint64_t arg) {
#if __BIG_ENDIAN__
    return arg;
#else
    return SwapInt64(arg);
#endif
}

#endif
