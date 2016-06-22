/*
 * This library is a wrapper for stock's libatchannel.so and libsecril-client.so
 */

#define LOG_TAG "AtChannelWrapper"
#define LOG_NDEBUG 0

#include <android/log.h>
#include <binder/IServiceManager.h>
#include <utils/String8.h>
#include <utils/String16.h>
#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include <AtChannelWrapper.h>

#define LIBATCHANNEL_NAME  "libatchannel.so"
#define SENDAT_FUNC_NAME   "sendAt"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

using namespace std;
using namespace android;

typedef const char *(*ss_sendAt_f)(int modemId, int simId, const char *atCmd);

static ss_sendAt_f find_ss_sendAt();
static void init() __attribute__((constructor));

static ss_sendAt_f ss_sendAt_handle;

size_t sendAt(void *buf, size_t bufLen, int simId, const char* atCmd)
{
	ALOGI("atCmd=[%s]", atCmd);
	if (ss_sendAt_handle) {
		const int modemId = 0; /* XXX Is 0 for w modem, 1 for other modem? */
		const char *resp = ss_sendAt_handle(modemId, simId, atCmd);
		size_t outLen = MIN(bufLen, strlen(resp) + 1);
		memcpy(buf, resp, outLen);
		return outLen;
	} else {
		ALOGE("ss_sendAt_handle is not initilized!");
	}
	return 0;
}

ss_sendAt_f find_ss_sendAt()
{
	ss_sendAt_f result = 0;
	void *handle;
	char *error;
	ALOGD("%s", __FUNCTION__);
	if ((handle = dlopen(LIBATCHANNEL_NAME, RTLD_NOW))) {
		dlerror(); /* clear any previous errors */
		result = (ss_sendAt_f) dlsym(handle, SENDAT_FUNC_NAME);
		if ((error = (char *) dlerror())) {
			result = 0;
			dlclose(handle);
			ALOGE("Cannot find '%s' function. Error: %s", SENDAT_FUNC_NAME, error);
		}
	} else {
		ALOGE("Cannot load: %s. Error: %s", LIBATCHANNEL_NAME, dlerror());
	}
	return result;
}

void init()
{
	ALOGD("%s", __FUNCTION__);
	ss_sendAt_handle = find_ss_sendAt();
}
