#ifndef ANDROID_ATCHANNEL_WRAPPER_H
#define ANDROID_ATCHANNEL_WRAPPER_H

#ifdef __cplusplus
extern "C"
{
#endif

size_t sendAt(void *buf, size_t bufLen, int simId, const char* atCmd);

#ifdef __cplusplus
}
#endif

#endif /* ANDROID_ATCHANNEL_WRAPPER_H */
