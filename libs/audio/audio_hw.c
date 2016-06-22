/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "audio_hw_primary"
#define LOG_NDEBUG 0
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <expat.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include "audio_pga.h"
#include "vb_effect_if.h"
#include "vb_pga.h"

#include "eng_audio.h"
#include "aud_proc.h"
#include "vb_control_parameters.h"
#include "string_exchange_bin.h"

#include "dumpdata.h"

#ifdef AUDIO_MUX_PCM
#include "audio_mux_pcm.h"
#endif

//#define XRUN_DEBUG
//#define VOIP_DEBUG

#ifdef XRUN_DEBUG
#define XRUN_TRACE  ALOGW
#else
#define XRUN_TRACE
#endif
#define BLUE_TRACE  ALOGW

#ifdef VOIP_DEBUG
#define VOIP_TRACE  ALOGW
#else
#define VOIP_TRACE
#endif

/**
  * container_of - cast a member of a structure out to the containing structure
  * @ptr:    the pointer to the member.
  * @type:   the type of the container struct this is embedded in.
  * @member: the name of the member within the struct.
  *
  */
#define container_of(ptr, type, member) ({      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

//#define AUDIO_DUMP
#define AUDIO_DUMP_EX

#define AUDIO_OUT_FILE_PATH  "data/audio_out.pcm"

//make sure this device is not used by android
#define SPRD_AUDIO_IN_DUALMIC_VOICE  0x81000000 //in:0x80000000

#define PRIVATE_NAME_LEN 60

#define CTL_TRACE(exp) ALOGW(#exp" is %s", ((exp) != NULL) ? "successful" : "failure")

#define PRIVATE_MIC_BIAS                  "mic bias"
#define PRIVATE_VBC_CONTROL              "vb control"
#define PRIVATE_VBC_EQ_SWITCH            "eq switch"
#define PRIVATE_VBC_EQ_UPDATE            "eq update"
#define PRIVATE_VBC_EQ_PROFILE            "eq profile"
#define PRIVATE_INTERNAL_PA              "internal PA"
#define FM_DIGITAL_SUPPORT_PROPERTY  "ro.digital.fm.support"

#define PRIVATE_VBC_DA_EQ_SWITCH            "da eq switch"
#define PRIVATE_VBC_AD01_EQ_SWITCH            "ad01 eq switch"
#define PRIVATE_VBC_AD23_EQ_SWITCH            "ad23 eq switch"

#define PRIVATE_VBC_DA_EQ_PROFILE            "da eq profile"
#define PRIVATE_VBC_AD01_EQ_PROFILE            "ad01 eq profile"
#define PRIVATE_VBC_AD23_EQ_PROFILE            "ad23 eq profile"
#define PRIVATE_SPEAKER_MUTE            "spk mute"
#define PRIVATE_SPEAKER2_MUTE           "spk2 mute"
#define PRIVATE_EARPIECE_MUTE           "earpiece mute"
#define PRIVATE_HEADPHONE_MUTE           "hp mute"
#define PRIVATE_AUD_LOOP_VBC  "Aud Loop in VBC Switch"
#define PRIVATE_AUD1_LOOP_VBC  "Aud1 Loop in VBC Switch"
/* ALSA cards for sprd */
#define CARD_SPRDPHONE "sprdphone"
#define CARD_VAUDIO    "VIRTUAL AUDIO"
#define CARD_VAUDIO_W  "VIRTUAL AUDIO W"
#define CARD_SCO    "saudiovoip"
#define CARD_BT_SCO    "all-i2s"


/* ALSA ports for sprd */
#define PORT_MM 0
#define PORT_MODEM 1
#define PORT_FM 4

/* constraint imposed by VBC: all period sizes must be multiples of 160 */
#define VBC_BASE_FRAME_COUNT 160
/* number of base blocks in a short period (low latency) */
#define SHORT_PERIOD_MULTIPLIER 8 /* 29 ms */
/* number of frames per short period (low latency) */
#define SHORT_PERIOD_SIZE (VBC_BASE_FRAME_COUNT * SHORT_PERIOD_MULTIPLIER)
/* number of short periods in a long period (low power) */
#ifdef _LPA_IRAM
#define LONG_PERIOD_MULTIPLIER 3 /* 87 ms */
#else
#define LONG_PERIOD_MULTIPLIER 6  /* 174 ms */
#endif
/* number of frames per long period (low power) */
#define LONG_PERIOD_SIZE (SHORT_PERIOD_SIZE * LONG_PERIOD_MULTIPLIER)
/* number of periods for low power playback */
#define PLAYBACK_LONG_PERIOD_COUNT 2
/* number of pseudo periods for low latency playback */
#define PLAYBACK_SHORT_PERIOD_COUNT 4
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 2
/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define RESAMPLER_BUFFER_FRAMES (SHORT_PERIOD_SIZE * 2)
#define RESAMPLER_BUFFER_SIZE (4 * RESAMPLER_BUFFER_FRAMES)

#define DEFAULT_OUT_SAMPLING_RATE 44100
#define DEFAULT_IN_SAMPLING_RATE  8000
#define DEFAULT_FM_SRC_SAMPLING_RATE 32000

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE 44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 48000
/* sampling rate when using VX port for narrow band */
#define VX_NB_SAMPLING_RATE 8000
/* sampling rate when using VX port for wide band */
#define VX_WB_SAMPLING_RATE 16000

#define RECORD_POP_MIN_TIME    150   // ms

#define BT_SCO_UPLINK_IS_STARTED        (1 << 0)
#define BT_SCO_DOWNLINK_IS_EXIST        (1 << 1)
#define BT_SCO_DOWNLINK_OPEN_FAIL       (1 << 8)
#define AUDFIFO "/data/local/media/audiopara_tuning"

#define MAX_STOP_THRESHOLD ((unsigned int)-1)/2-1

struct pcm_config pcm_config_mm = {
    .channels = 2,
    .rate = DEFAULT_OUT_SAMPLING_RATE,
    .period_size = LONG_PERIOD_SIZE,
    .period_count = PLAYBACK_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = SHORT_PERIOD_SIZE,
    .avail_min = SHORT_PERIOD_SIZE,
};


struct pcm_config pcm_config_mm_fast = {
    .channels = 2,
    .rate = DEFAULT_OUT_SAMPLING_RATE,
    .period_size = SHORT_PERIOD_SIZE,
    .period_count = PLAYBACK_SHORT_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = SHORT_PERIOD_SIZE/2,
    .avail_min = SHORT_PERIOD_SIZE/2,
};

struct pcm_config pcm_config_mm_ul = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = SHORT_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_vx = {
    .channels = 2,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = VBC_BASE_FRAME_COUNT,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = MAX_STOP_THRESHOLD,
};

struct pcm_config pcm_config_vrec_vx = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 320,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = MAX_STOP_THRESHOLD,
};
struct pcm_config pcm_config_record_incall = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 320,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_vx_voip = {
    .channels = 2,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = VBC_BASE_FRAME_COUNT,
    .period_count = 2,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = MAX_STOP_THRESHOLD,
};
struct pcm_config pcm_config_vrec_vx_voip = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 320,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
    .stop_threshold = MAX_STOP_THRESHOLD,
};

#define VOIP_CAPTURE_STREAM     0x1
#define VOIP_PLAYBACK_STREAM    0x2



struct pcm_config pcm_config_vplayback = {
    .channels = 2,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 320,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_scoplayback = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    //.period_size = 320,
    .period_size = 640,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
};


struct pcm_config pcm_config_btscoplayback = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    //.period_size = 320,
    .period_size = 640,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
};


struct pcm_config pcm_config_scocapture = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 640,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_btscocapture = {
    .channels = 1,
    .rate = VX_NB_SAMPLING_RATE,
    .period_size = 640,
    .period_count = 8,
    .format = PCM_FORMAT_S16_LE,
};

struct pcm_config pcm_config_fm_dl = {
    .channels = 2,
    .rate = DEFAULT_FM_SRC_SAMPLING_RATE,
    .period_size = LONG_PERIOD_SIZE,
    .period_count = PLAYBACK_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};
struct pcm_config pcm_config_fm_ul = {
    .channels = 2,
    .rate = DEFAULT_FM_SRC_SAMPLING_RATE,
    .period_size = SHORT_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
};


#define MIN(x, y) ((x) > (y) ? (y) : (x))

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};

struct tiny_dev_cfg {
    int mask;

    struct route_setting *on;
    unsigned int on_len;

    struct route_setting *off;
    unsigned int off_len;
};

struct tiny_private_ctl {
    struct mixer_ctl *mic_bias_switch;
    struct mixer_ctl *vbc_switch;
    struct mixer_ctl *vbc_eq_switch;
    struct mixer_ctl *vbc_eq_update;
    struct mixer_ctl *vbc_eq_profile_select;
    struct mixer_ctl *internal_pa;
    struct mixer_ctl *vbc_da_eq_switch;
    struct mixer_ctl *vbc_ad01_eq_switch;
    struct mixer_ctl *vbc_ad23_eq_switch;
    struct mixer_ctl *vbc_da_eq_profile_select;
    struct mixer_ctl *vbc_ad01_eq_profile_select;
    struct mixer_ctl *vbc_ad23_eq_profile_select;
    struct mixer_ctl *speaker_mute;
    struct mixer_ctl *speaker2_mute;
    struct mixer_ctl *earpiece_mute;
    struct mixer_ctl *headphone_mute;
    struct mixer_ctl *fm_loop_vbc;
    struct mixer_ctl *ad1_fm_loop_vbc;
};

struct stream_routing_manager {
    pthread_t        routing_switch_thread;
    bool             is_exit;
    sem_t           device_switch_sem;
};

struct bt_sco_thread_manager {
    bool             thread_is_exit;
    pthread_t        dup_thread;
    pthread_mutex_t  dup_mutex;
    pthread_mutex_t  cond_mutex;
    pthread_cond_t   cond;
    sem_t            dup_sem;
    volatile bool    dup_count;
    volatile bool    dup_need_start;
};

#define VOIP_PIPE_NAME_MAX    16
#define MAX_AT_CMD_LENGTH   32
#define MAX_AT_CMD_TYPE  8
/*
typedef struct{
   char  s_at_cmd_route[MAX_CMD_LENGTH];
   char  s_at_cmd_voluem[MAX_CMD_LENGTH];
   char  s_at_cmd_micmute[MAX_CMD_LENGTH];
   char  s_at_cmd_downlinkmute[MAX_CMD_LENGTH];
   char  s_at_cmd_audioloop[MAX_CMD_LENGTH];
   char  s_at_cmd_usecase[MAX_CMD_LENGTH];
   char  s_at_cmd_extra_volume[MAX_CMD_LENGTH];
   char  s_at_cmd_bt_sample[MAX_CMD_LENGTH];
}T_AT_CMD;
*/

typedef struct{
   char  at_cmd[MAX_AT_CMD_TYPE][MAX_AT_CMD_LENGTH];
   uint32_t   at_cmd_priority[MAX_AT_CMD_TYPE];
   uint32_t   at_cmd_dirty;
}T_AT_CMD;

struct tiny_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct mixer *mixer;
    audio_mode_t mode;
    int out_devices;
    int in_devices;
    int prev_out_devices;
    int prev_in_devices;
    volatile int cur_vbpipe_fd;  /*current vb pipe id, if all pipes is closed this is -1.*/
    cp_type_t  cp_type;
    struct pcm *pcm_modem_dl;
    struct pcm *pcm_modem_ul;
    struct pcm *pcm_fm_dl;
    volatile int call_start;
    volatile int call_connected;
    volatile int call_prestop;
    volatile int vbc_2arm;
    pthread_mutex_t vbc_lock;/*for multiple vb pipe.*/
    float voice_volume;
    struct tiny_stream_in *active_input;
    struct tiny_stream_out *active_output;
    bool mic_mute;
    bool bluetooth_nrec;
    int  bluetooth_type;
    bool low_power;

    struct tiny_dev_cfg *dev_cfgs;
    unsigned int num_dev_cfgs;

    struct tiny_private_ctl private_ctl;
    struct audio_pga *pga;
    bool eq_available;

    audio_modem_t *cp;
    AUDIO_TOTAL_T *audio_para;
    pthread_t        audiopara_tuning_thread;

    volatile int bt_sco_state;
    struct bt_sco_thread_manager bt_sco_manager;

    struct stream_routing_manager  routing_mgr;
    struct stream_routing_manager  voice_command_mgr;

    int voip_state;
    int voip_start;
    bool master_mute;
    bool cache_mute;
    int fm_volume;

    int requested_channel_cnt;
    int  input_source;
    T_AT_CMD  *at_cmd_vectors;
};

struct tiny_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct pcm *pcm_vplayback;
    struct pcm *pcm_voip;
    struct pcm *pcm_bt_sco;
    int is_voip;
    int is_bt_sco;
    struct resampler_itfe  *resampler_vplayback;
    struct resampler_itfe  *resampler_sco;
    struct resampler_itfe  *resampler_bt_sco;
    struct resampler_itfe *resampler;
    char *buffer;
    char * buffer_vplayback;
    char * buffer_voip;
    char * buffer_bt_sco;
    int standby;
    struct tiny_audio_device *dev;
    unsigned int devices;
    int write_threshold;
    bool low_power;
    FILE * out_dump_fd;
    audio_output_flags_t flags;
};

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */

struct tiny_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct pcm * mux_pcm;
    int is_voip;
    int is_bt_sco;
    int device;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    unsigned int requested_channels;
    int standby;
    int source;
    int num_preprocessors;
    int16_t *proc_buf;
    size_t proc_buf_size;
    size_t proc_frames_in;
    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;
    int read_status;
    bool pop_mute;
    int pop_mute_count;

    struct tiny_audio_device *dev;
    int active_rec_proc;
};

struct config_parse_state {
    struct tiny_audio_device *adev;
    struct tiny_dev_cfg *dev;
    bool on;

    struct route_setting *path;
    unsigned int path_len;

    char private_name[PRIVATE_NAME_LEN];
};

typedef struct {
    int mask;
    const char *name;
}dev_names_para_t;

static const dev_names_para_t dev_names_linein[] = {
    { AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_FM_SPEAKER, "speaker" },
    { AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE | AUDIO_DEVICE_OUT_FM_HEADSET,
        "headphone" },
    { AUDIO_DEVICE_OUT_EARPIECE, "earpiece" },
    /* ANLG for voice call via linein*/
    { AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET | AUDIO_DEVICE_OUT_ALL_FM, "line" },
    { AUDIO_DEVICE_OUT_FM_HEADSET, "line-headphone" },
    { AUDIO_DEVICE_OUT_FM_SPEAKER, "line-speaker" },

    { AUDIO_DEVICE_IN_COMMUNICATION, "comms" },
    { AUDIO_DEVICE_IN_AMBIENT, "ambient" },
    { AUDIO_DEVICE_IN_BUILTIN_MIC, "builtin-mic" },
    { AUDIO_DEVICE_IN_WIRED_HEADSET, "headset-in" },
    { AUDIO_DEVICE_IN_AUX_DIGITAL, "digital" },
    { AUDIO_DEVICE_IN_BACK_MIC, "back-mic" },
    { SPRD_AUDIO_IN_DUALMIC_VOICE, "dual-mic-voice" },
    //{ "linein-capture"},
};
static const dev_names_para_t dev_names_digitalfm[] = {
    { AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_FM_SPEAKER, "speaker" },
    { AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE | AUDIO_DEVICE_OUT_FM_HEADSET,
        "headphone" },
    { AUDIO_DEVICE_OUT_EARPIECE, "earpiece" },
    /* ANLG for voice call via linein*/
    { AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET, "line" },
    { AUDIO_DEVICE_OUT_ALL_FM, "digital-fm" },


    { AUDIO_DEVICE_IN_COMMUNICATION, "comms" },
    { AUDIO_DEVICE_IN_AMBIENT, "ambient" },
    { AUDIO_DEVICE_IN_BUILTIN_MIC, "builtin-mic" },
    { AUDIO_DEVICE_IN_WIRED_HEADSET, "headset-in" },
    { AUDIO_DEVICE_IN_AUX_DIGITAL, "digital" },
    { AUDIO_DEVICE_IN_BACK_MIC, "back-mic" },
    { SPRD_AUDIO_IN_DUALMIC_VOICE, "dual-mic-voice" },
    //{ "linein-capture"},
};
#define FM_VOLUME_MAX 15
static const int fm_volume_tbl[FM_VOLUME_MAX] = {
    80,76,72,68,64,60,56,52,48,44,40,36,32,28,24};

static dev_names_para_t *dev_names = NULL;

/* this enum is for use-case type processed in cp side */
typedef enum {
    AUDIO_CP_USECASE_VOICE  = 0,
    AUDIO_CP_USECASE_VT,
    AUDIO_CP_USECASE_VOIP_1,
    AUDIO_CP_USECASE_VOIP_2,
    AUDIO_CP_USECASE_VOIP_3,
    AUDIO_CP_USECASE_VOIP_4,
    AUDIO_CP_USECASE_MAX,
} audio_cp_usecase_t;


/*
 * card define
 */
 /*
 * s_tinycard  is used to playback/capture of pcm data when vbc is controlled by ap part.
 *             for example
 *                        a.  playback in non-call.
 *                        b.  capture in non-call.
 *                        c.  voip in ap part.
 */
static int s_tinycard = -1;
 /*
 * s_vaudio  used in td mode
 * s_vaudio_w used in w mode
 *           is used to playback/capture of pcm data when vbc is controlled by cp part.
 *                 for example: playback/capture in call.
 */
static int s_vaudio = -1;
static int s_vaudio_w = -1;

 /*
 * s_voip  is used to voip call in cp part when vbc is controlled by cp dsp.
 */
static int s_voip= -1;

 /*
 * s_bt_sco  is used to playback/capture of pcm data when bt device of single channel is running.
 */
static int s_bt_sco = -1;

static AUDIO_TOTAL_T *audio_para_ptr = NULL;
static struct tiny_audio_device *s_adev= NULL;

static dump_data_info_t dump_info;

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the following order:
 *        hw device > in stream > out stream
 */
extern int get_snd_card_number(const char *card_name);
int set_call_route(struct tiny_audio_device *adev, int device, int on);
static void select_devices_signal(struct tiny_audio_device *adev);
static void do_select_devices(struct tiny_audio_device *adev);
static int set_route_by_array(struct mixer *mixer, struct route_setting *route,unsigned int len);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int adev_set_master_mute(struct audio_hw_device *dev, bool mute);
static int set_codec_mute(struct tiny_audio_device *adev);
static void set_codec_mute_forFM(struct tiny_audio_device *adev ,bool mute);
static int do_input_standby(struct tiny_stream_in *in);
static int do_output_standby(struct tiny_stream_out *out);
static void force_all_standby(struct tiny_audio_device *adev);
static struct route_setting * get_route_setting (
        struct tiny_audio_device *adev,
        int devices,
        int on);

static int get_route_depth (
        struct tiny_audio_device *adev,
        int devices,
        int on);

static int init_rec_process(int rec_mode, int sample_rate);
static int aud_rec_do_process(void * buffer, size_t bytes,void * tmp_buffer, size_t tmp_buffer_size);

static void *stream_routing_thread_entry(void * adev);
static int stream_routing_manager_create(struct tiny_audio_device *adev);
static void stream_routing_manager_close(struct tiny_audio_device *adev);

static void *voice_command_thread_entry(void * adev);
static int voice_command_manager_create(struct tiny_audio_device *adev);
static void voice_command_manager_close(struct tiny_audio_device *adev);

static int audio_bt_sco_duplicate_start(struct tiny_audio_device *adev, bool enable);
static int audiopara_get_compensate_phoneinfo(void* pmsg);
/*
 * NOTE: audio stream(playback, capture) dump just for debug.
 */
static int out_dump_create(FILE **out_fd, const char *path);
static int out_dump_doing(FILE *out_fd, const void* buffer, size_t bytes);
static int out_dump_release(FILE **fd);

#include "vb_control_parameters.c"

#include "at_commands_generic.c"
#include "mmi_audio_loop.c"

static long getCurrentTimeUs()
{
   struct timeval tv;
   gettimeofday(&tv,NULL);
   return tv.tv_sec* 1000000 + tv.tv_usec;
}

int i2s_pin_mux_sel(struct tiny_audio_device *adev, int type)
{
    int count = 0;
    audio_modem_t  *modem;
    uint8_t *ctl_on = "1";
    uint8_t *ctl_off = "0";
    uint8_t cur_state[2] = {0};
    uint8_t *ctl_str = "1";

    ALOGW("i2s_pin_mux_sel in type is %d",type);

    modem = adev->cp;

    if(modem->i2s_bt.fd_sys_cp0 < 0) {
        modem->i2s_bt.fd_sys_cp0 = open(modem->i2s_bt.fd_sys_cp0_path,O_RDWR | O_SYNC);
    }
    if(modem->i2s_bt.fd_sys_cp1 < 0) {
        modem->i2s_bt.fd_sys_cp1 = open(modem->i2s_bt.fd_sys_cp1_path,O_RDWR | O_SYNC);
    }
    if(modem->i2s_bt.fd_sys_cp2 < 0) {
        modem->i2s_bt.fd_sys_cp2 = open(modem->i2s_bt.fd_sys_cp2_path,O_RDWR | O_SYNC);
    }
    if(modem->i2s_bt.fd_sys_ap < 0) {
        modem->i2s_bt.fd_sys_ap = open(modem->i2s_bt.fd_sys_ap_path,O_RDWR | O_SYNC);
    }
    if(!modem->i2s_bt.is_ext) {
        if(modem->i2s_bt.fd_bt_cp0 < 0) {
            modem->i2s_bt.fd_bt_cp0 = open(modem->i2s_bt.fd_bt_cp0_path,O_RDWR | O_SYNC);
        }
        if(modem->i2s_bt.fd_bt_cp1 < 0) {
            modem->i2s_bt.fd_bt_cp1 = open(modem->i2s_bt.fd_bt_cp1_path,O_RDWR | O_SYNC);
        }
        if(modem->i2s_bt.fd_bt_cp2 < 0) {
            modem->i2s_bt.fd_bt_cp2 = open(modem->i2s_bt.fd_bt_cp2_path,O_RDWR | O_SYNC);
        }
        if(modem->i2s_bt.fd_bt_ap < 0) {
            modem->i2s_bt.fd_bt_ap = open(modem->i2s_bt.fd_bt_ap_path,O_RDWR | O_SYNC);
        }
    }
    if(modem->i2s_extspk.fd_sys_cp0 < 0) {
        modem->i2s_extspk.fd_sys_cp0 = open(modem->i2s_extspk.fd_sys_cp0_path,O_RDWR | O_SYNC);
    }
    if(modem->i2s_extspk.fd_sys_cp1 < 0) {
        modem->i2s_extspk.fd_sys_cp1 = open(modem->i2s_extspk.fd_sys_cp1_path,O_RDWR | O_SYNC);
    }
    if(modem->i2s_extspk.fd_sys_cp2 < 0) {
        modem->i2s_extspk.fd_sys_cp2 = open(modem->i2s_extspk.fd_sys_cp2_path,O_RDWR | O_SYNC);
    }
    if(modem->i2s_extspk.fd_sys_ap < 0) {
        modem->i2s_extspk.fd_sys_ap = open(modem->i2s_extspk.fd_sys_ap_path,O_RDWR | O_SYNC);
    }
    if(type == 0) {
       if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
            if(modem->i2s_bt.is_ext) {
                if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_sys_cp0 >= 0)){
                        count = write(modem->i2s_bt.fd_sys_cp0,ctl_on,1);
                    }
            }
            else {
                if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_sys_cp0 >= 0)) {
                    count = read(modem->i2s_bt.fd_sys_cp0,cur_state,1);
                    if(strcmp(cur_state,"1") == 0) {
                        count = write(modem->i2s_bt.fd_sys_cp2,ctl_on,1);
                    }
                }
                if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
                    if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_bt_cp0 >= 0))
                        count = write(modem->i2s_bt.fd_bt_cp0,ctl_on,1);
                }
            }
        }

        if(adev->out_devices & AUDIO_DEVICE_OUT_SPEAKER) {
            if(modem->i2s_extspk.is_switch && (modem->i2s_extspk.fd_sys_cp0 >= 0))
                count = write(modem->i2s_extspk.fd_sys_cp0,ctl_str,1);
        }

    }
    else if (type == 1){
        if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
            if(modem->i2s_bt.is_ext) {
                if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_sys_cp1 >= 0)) {
                       count = write(modem->i2s_bt.fd_sys_cp1,ctl_on,1);
                   }
            }
            else {
                if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_sys_cp1 >= 0)) {
                    count = read(modem->i2s_bt.fd_sys_cp1,cur_state,1);
                    if(strcmp(cur_state,"1") == 0) {
			if( modem->i2s_bt.fd_sys_cp2 >= 0)
                        count = write(modem->i2s_bt.fd_sys_cp2,ctl_on,1);
                    }
                }
                if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
                    if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_bt_cp1 >= 0))
                        count = write(modem->i2s_bt.fd_bt_cp1,ctl_on,1);
                }
            }
        }
        if(adev->out_devices& AUDIO_DEVICE_OUT_SPEAKER) {
            if(modem->i2s_extspk.is_switch && (modem->i2s_extspk.fd_sys_cp1 >= 0))
                count = write(modem->i2s_extspk.fd_sys_cp1,ctl_str,1);
        }
    }
    else if(type == 2) {
        {
            if(modem->i2s_bt.is_ext) {
                if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_sys_ap >= 0))
                    count = write(modem->i2s_bt.fd_sys_ap,ctl_on,1);
            }
            else {
                if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_sys_ap >= 0)) {
                    count = read(modem->i2s_bt.fd_sys_ap,cur_state,1);
                    if(strcmp(cur_state,"1") == 0) {
			if( modem->i2s_bt.fd_sys_cp2 >= 0)
                        count = write(modem->i2s_bt.fd_sys_cp2,ctl_on,1);
                    }
                }
                {
                    if(modem->i2s_bt.is_switch && (modem->i2s_bt.fd_bt_ap >= 0))
                        count = write(modem->i2s_bt.fd_bt_ap,ctl_on,1);//bt iis select
                }
            }
        }
        if(adev->out_devices & AUDIO_DEVICE_OUT_SPEAKER) {
            if(modem->i2s_extspk.is_switch && (modem->i2s_extspk.fd_sys_ap >= 0))
                count = write(modem->i2s_extspk.fd_sys_ap,ctl_off,1);
        }
    }
    else {
        return 0;
    }

    return (count==1);
}

static cp_type_t get_cur_cp_type( struct tiny_audio_device *adev )
{
    return adev->cp_type;
}

static  int  pcm_mixer(int16_t  *buffer, uint32_t samples)
{
    int i=0;
    int16_t * tmp_buf=buffer;
    for(i=0;i<(samples/2);i++){
        tmp_buf[i]=(buffer[2*i+1]+buffer[2*i])/2;
    }
    return 0;
}

static int out_dump_create(FILE **out_fd, const char *path)
{
    if (path == NULL) {
        ALOGE("path not assigned.");
        return -1;
    }
    *out_fd = (FILE *)fopen(path, "wb");
    if (*out_fd == NULL ) {
        ALOGE("cannot create file.");
        return -1;
    }
    ALOGI("path %s created successfully.", path);
    return 0;
}

static int out_dump_doing(FILE *out_fd, const void* buffer, size_t bytes)
{
    int ret;
    if (out_fd) {
        ret = fwrite((uint8_t *)buffer, bytes, 1, out_fd);
        if (ret < 0) ALOGW("%d, fwrite failed.", bytes);
    } else {
        ALOGW("out_fd is NULL, cannot write.");
    }
    return 0;
}

static int out_dump_release(FILE **fd)
{
    if(*fd > 0) {
	    fclose(*fd);
	    *fd = NULL;
    }
    return 0;
}


int set_call_route(struct tiny_audio_device *adev, int device, int on)
{
    struct route_setting *cur_setting;
    int cur_depth = 0;

    cur_setting = get_route_setting(adev, device, on);
    cur_depth = get_route_depth(adev, device, on);
    if (adev->mixer && cur_setting)
        set_route_by_array(adev->mixer, cur_setting, cur_depth);
    return 0;
}

static struct route_setting * get_route_setting(
        struct tiny_audio_device *adev,
        int devices,
        int on)
{
    unsigned int i = 0;
    for (i=0; i<adev->num_dev_cfgs; i++) {
        if ((devices & AUDIO_DEVICE_BIT_IN) && (adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)) {
            if ((devices & ~AUDIO_DEVICE_BIT_IN) & adev->dev_cfgs[i].mask) {
                if (on)
                    return adev->dev_cfgs[i].on;
                else
                    return adev->dev_cfgs[i].off;
            }
        } else if (!(devices & AUDIO_DEVICE_BIT_IN) && !(adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)){
            if (devices & adev->dev_cfgs[i].mask) {
                if (on)
                    return adev->dev_cfgs[i].on;
                else
                    return adev->dev_cfgs[i].off;
            }
        }
    }
    ALOGW("[get_route_setting], warning: devices(0x%08x) NOT found.", devices);
    return NULL;
}

static int get_route_depth (
        struct tiny_audio_device *adev,
        int devices,
        int on)
{
    unsigned int i = 0;

    for (i=0; i<adev->num_dev_cfgs; i++) {
        if ((devices & AUDIO_DEVICE_BIT_IN) && (adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)) {
            if ((devices & ~AUDIO_DEVICE_BIT_IN) & adev->dev_cfgs[i].mask) {
                if (on)
                    return adev->dev_cfgs[i].on_len;
                else
                    return adev->dev_cfgs[i].off_len;
            }
        } else if (!(devices & AUDIO_DEVICE_BIT_IN) && !(adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)){
            if (devices & adev->dev_cfgs[i].mask) {
                if (on)
                    return adev->dev_cfgs[i].on_len;
                else
                    return adev->dev_cfgs[i].off_len;
            }
        }
    }
    ALOGW("[get_route_setting], warning: devices(0x%08x) NOT found.", devices);
    return 0;
}

/* The enable flag when 0 makes the assumption that enums are disabled by
 * "Off" and integers/booleans by 0 */
static int set_route_by_array(struct mixer *mixer, struct route_setting *route,
        unsigned int len)
{
    struct mixer_ctl *ctl;
    unsigned int i, j, ret;

    /* Go through the route array and set each value */
    for (i = 0; i < len; i++) {
        ctl = mixer_get_ctl_by_name(mixer, route[i].ctl_name);
        if (!ctl) {
            ALOGE("Unknown control '%s'\n", route[i].ctl_name);
            continue;
        }

        if (route[i].strval) {
            ret = mixer_ctl_set_enum_by_string(ctl, route[i].strval);
            if (ret != 0) {
                ALOGE("Failed to set '%s' to '%s'\n",
                        route[i].ctl_name, route[i].strval);
            } else {
                ALOGI("Set '%s' to '%s'\n",
                        route[i].ctl_name, route[i].strval);
            }
        } else {
            /* This ensures multiple (i.e. stereo) values are set jointly */
            for (j = 0; j < mixer_ctl_get_num_values(ctl); j++) {
                ret = mixer_ctl_set_value(ctl, j, route[i].intval);
                if (ret != 0) {
                    ALOGE("Failed to set '%s'.%d to %d\n",
                            route[i].ctl_name, j, route[i].intval);
                } else {
                    ALOGI("Set '%s'.%d to %d\n",
                            route[i].ctl_name, j, route[i].intval);
                }
            }
        }
    }

    return 0;
}

static void do_select_devices(struct tiny_audio_device *adev)
{
    unsigned int i;
    int fm_open = 0;

    if(adev->voip_state) {
        int ret;
        ALOGI("do_select_devices  in %x,but voip is on so send at to cp in",adev->out_devices);
        ret = at_cmd_route(adev);  //send at command to cp
        ALOGI("do_select_devices in %x,but voip is on so send at to cp out ret is %d",adev->out_devices,
ret);
        if (ret < 0) {
            ALOGE("do_seletc devices at_cmd_route error(%d) ",ret);
        }
        return;
    }
    if (adev->cache_mute == adev->master_mute) {
        ALOGI("Not to change mute: %d", adev->cache_mute);
    } else {
        adev->cache_mute = adev->master_mute;
        /* mute codec PA */
        set_codec_mute(adev);
    }
    if (adev->prev_out_devices == adev->out_devices
            && adev->prev_in_devices == adev->in_devices) {
        ALOGI("Not to change devices: OUT=0x%08x, IN=0x%08x",
                adev->prev_out_devices, adev->prev_in_devices);
        return;
    }
    ALOGI("Changing out_devices: from (0x%08x) to (0x%08x)",
            adev->prev_out_devices, adev->out_devices);
    ALOGI("Changing in_devices: from (0x%08x) to (0x%08x)",
            adev->prev_in_devices, adev->in_devices);
    adev->prev_out_devices = adev->out_devices;
    adev->prev_in_devices = adev->in_devices;
    if(adev->eq_available)
        vb_effect_sync_devices(adev->out_devices, adev->in_devices);

    /* Turn on new devices first so we don't glitch due to powerdown... */
    for (i = 0; i < adev->num_dev_cfgs; i++) {
	/* separate INPUT/OUTPUT case for some common bit used. */
        if ((adev->out_devices & adev->dev_cfgs[i].mask)
	    && !(adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)) {
        if(AUDIO_DEVICE_OUT_ALL_FM == adev->dev_cfgs[i].mask && adev->pcm_fm_dl == NULL){
#if 0
            ALOGE("%s:open FM device",__func__);
            pthread_mutex_lock(&adev->lock);
            //force_all_standby(adev);
            adev->pcm_fm_dl= pcm_open(s_tinycard, PORT_FM, PCM_OUT, &pcm_config_fm_dl);
            if (!pcm_is_ready(adev->pcm_fm_dl)) {
            ALOGE("%s:cannot open pcm_fm_dl : %s", __func__,pcm_get_error(adev->pcm_fm_dl));
            pcm_close(adev->pcm_fm_dl);
            adev->pcm_fm_dl= NULL;
            } else {
              if( 0 != pcm_start(adev->pcm_fm_dl)){
                  ALOGE("%s:pcm_fm_dl start unsucessfully: %s", __func__,pcm_get_error(adev->pcm_fm_dl));
              }
              if(adev->master_mute){
                  ALOGV("open FM and set codec unmute");
                  set_codec_mute_forFM(adev,false);
              }
            }
            pthread_mutex_unlock(&adev->lock);
#else
            fm_open = 1;
            ALOGE("%s:Will open FM device",__func__);
#endif
        }
            set_route_by_array(adev->mixer, adev->dev_cfgs[i].on,
                    adev->dev_cfgs[i].on_len);
    }

	if (((adev->in_devices & ~AUDIO_DEVICE_BIT_IN) & adev->dev_cfgs[i].mask)
	    && (adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)) {
            set_route_by_array(adev->mixer, adev->dev_cfgs[i].on,
                    adev->dev_cfgs[i].on_len);
          /* force close main-mic ADCL when channel count is one for power issue */
          if ((AUDIO_DEVICE_IN_BUILTIN_MIC == ( adev->in_devices & adev->dev_cfgs[i].mask))
               && (adev->requested_channel_cnt == 1)) {
              /* force close main-mic ADCL when channel count is one for power issue */
              close_adc_channel(adev->mixer, true, false, false);
          }
        }
    }

    /* ...then disable old ones. */
    for (i = 0; i < adev->num_dev_cfgs; i++) {
        if (!(adev->out_devices & adev->dev_cfgs[i].mask)
	    && !(adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)) {
            set_route_by_array(adev->mixer, adev->dev_cfgs[i].off,
                    adev->dev_cfgs[i].off_len);
        if(AUDIO_DEVICE_OUT_ALL_FM == adev->dev_cfgs[i].mask && adev->pcm_fm_dl != NULL)
        {
            ALOGE("%s:close FM device",__func__);
            pthread_mutex_lock(&adev->lock);
            pcm_close(adev->pcm_fm_dl);
            adev->pcm_fm_dl= NULL;
            if(adev->master_mute){
                ALOGV("close FM so we set codec to mute by master_mute");
                set_codec_mute_forFM(adev,true);
            }
            pthread_mutex_unlock(&adev->lock);
        }
        }

        if (!((adev->in_devices & ~AUDIO_DEVICE_BIT_IN) & adev->dev_cfgs[i].mask)
	    && (adev->dev_cfgs[i].mask & AUDIO_DEVICE_BIT_IN)) {
            set_route_by_array(adev->mixer, adev->dev_cfgs[i].off,
                    adev->dev_cfgs[i].off_len);
        }
    }

    if (1 == fm_open) {
        fm_open = 0;
        ALOGE("%s:open FM device",__func__);
        pthread_mutex_lock(&adev->lock);
        //force_all_standby(adev);
        adev->pcm_fm_dl= pcm_open(s_tinycard, PORT_FM, PCM_OUT, &pcm_config_fm_dl);
        if (!pcm_is_ready(adev->pcm_fm_dl)) {
            ALOGE("%s:cannot open pcm_fm_dl : %s", __func__,pcm_get_error(adev->pcm_fm_dl));
            pcm_close(adev->pcm_fm_dl);
            adev->pcm_fm_dl= NULL;
        } else {
              if( 0 != pcm_start(adev->pcm_fm_dl)){
                  ALOGE("%s:pcm_fm_dl start unsucessfully: %s", __func__,pcm_get_error(adev->pcm_fm_dl));
              }
              if(adev->master_mute){
                  ALOGV("open FM and set codec unmute");
                  set_codec_mute_forFM(adev,false);
              }
        }
        pthread_mutex_unlock(&adev->lock);
    }

    /* update EQ profile*/
    if(adev->eq_available)
        vb_effect_profile_apply();
    SetAudio_gain_route(adev,1);
}

static void select_devices_signal(struct tiny_audio_device *adev)
{
    ALOGI("select_devices_signal starting...");
    sem_post(&adev->routing_mgr.device_switch_sem);
    ALOGI("select_devices_signal finished.");
}

static int start_call(struct tiny_audio_device *adev)
{
    ALOGE("Opening modem PCMs");
    return 0;
}

static void end_call(struct tiny_audio_device *adev)
{
    ALOGE("Closing modem PCMs");
}

static void set_eq_filter(struct tiny_audio_device *adev)
{

}

static void set_input_volumes(struct tiny_audio_device *adev, int main_mic_on,
        int headset_mic_on, int sub_mic_on)
{

}

static void set_output_volumes(struct tiny_audio_device *adev, bool tty_volume)
{
}

static void force_all_standby(struct tiny_audio_device *adev)
{
    struct tiny_stream_in *in;
    struct tiny_stream_out *out;

    if (adev->active_output) {
        out = adev->active_output;
        pthread_mutex_lock(&out->lock);
        do_output_standby(out);
        pthread_mutex_unlock(&out->lock);
    }

    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}



static void force_standby_for_voip(struct tiny_audio_device *adev)
{
    struct tiny_stream_in *in;
    struct tiny_stream_out *out;

    if (adev->active_output && (!(adev->voip_state & VOIP_PLAYBACK_STREAM))) {
       out = adev->active_output;
       pthread_mutex_lock(&out->lock);
       do_output_standby(out);
       pthread_mutex_unlock(&out->lock);
    }

    if (adev->active_input && ( !(adev->voip_state & VOIP_CAPTURE_STREAM))) {
       in = adev->active_input;
       pthread_mutex_lock(&in->lock);
       do_input_standby(in);
       pthread_mutex_unlock(&in->lock);
    }
}


static void select_mode(struct tiny_audio_device *adev)
{
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGE("Entering IN_CALL state, %s first call...out_devices:0x%x mode:%d ", adev->call_start ? "not":"is",adev->out_devices,adev->mode);
    } else {
        ALOGE("Leaving IN_CALL state, call_start=%d, mode=%d out_devices:0x%x ",
                adev->call_start, adev->mode,adev->out_devices);
    }
}

static int start_vaudio_output_stream(struct tiny_stream_out *out)
{
    unsigned int card = 0;
    unsigned int port = PORT_MM;
    struct pcm_config old_pcm_config;
    int ret=0;
    cp_type_t cp_type;
    card = s_vaudio;
    old_pcm_config=out->config;
    out->config = pcm_config_vplayback;
    out->buffer_vplayback = malloc(RESAMPLER_BUFFER_SIZE);
    if(!out->buffer_vplayback){
        ALOGE("start_vaudio_output_stream: alloc fail, size: %d", RESAMPLER_BUFFER_SIZE);
        goto error;
    }
    else {
        memset(out->buffer_vplayback, 0, RESAMPLER_BUFFER_SIZE);
    }

    cp_type = get_cur_cp_type(out->dev);
    if(cp_type == CP_TG) {
	    s_vaudio = get_snd_card_number(CARD_VAUDIO);
        card = s_vaudio;
    }
    else if (cp_type == CP_W) {
	    s_vaudio_w = get_snd_card_number(CARD_VAUDIO_W);
        card = s_vaudio_w;
    }
    BLUE_TRACE("start vaudio_output_stream cp_type is %d ,card is %d",cp_type, card);

    out->pcm_vplayback= pcm_open(card, port, PCM_OUT| PCM_MMAP |PCM_NOIRQ, &out->config);

    if (!pcm_is_ready(out->pcm_vplayback)) {
        goto error;
    }
    else {
        ret = create_resampler( DEFAULT_OUT_SAMPLING_RATE,
                out->config.rate,
                out->config.channels,
                RESAMPLER_QUALITY_DEFAULT,
                NULL,
                &out->resampler_vplayback);
        if (ret != 0) {
            goto error;
        }
    }
    return 0;

error:
    out->config = old_pcm_config ;
    if(out->buffer_vplayback){
        free(out->buffer_vplayback);
        out->buffer_vplayback=NULL;
    }
    if(out->pcm_vplayback){
        ALOGE("start_vaudio_output_stream error: %s", pcm_get_error(out->pcm_vplayback));
        pcm_close(out->pcm_vplayback);
        out->pcm_vplayback=NULL;
        ALOGE("start_vaudio_output_stream: out\n");
    }
    return -1;
}



static int open_voip_codec_pcm(struct tiny_audio_device *adev)
{
    ALOGE("voip:open voip_codec_pcm in");

    ALOGD("voip:open codec pcm in adev->voip_state is %x",adev->voip_state);
       if(!adev->pcm_modem_dl) {
           adev->pcm_modem_dl= pcm_open(s_tinycard, PORT_MODEM, PCM_OUT, &pcm_config_vx_voip);
           if (!pcm_is_ready(adev->pcm_modem_dl)) {
              ALOGE("cannot open pcm_modem_dl : %s", pcm_get_error(adev->pcm_modem_dl));
               pcm_close(adev->pcm_modem_dl);
               adev->pcm_modem_dl = NULL;
		ALOGE("voip:open voip_codec_pcm dl fail");
		return -1;
           }
       }
    ALOGD("voip:open codec pcm in 2");
       if(!adev->pcm_modem_ul) {
           adev->pcm_modem_ul= pcm_open(s_tinycard, PORT_MODEM, PCM_IN, &pcm_config_vrec_vx_voip);
           if (!pcm_is_ready(adev->pcm_modem_ul)) {
               ALOGE("cannot open pcm_modem_ul : %s", pcm_get_error(adev->pcm_modem_ul));
              pcm_close(adev->pcm_modem_ul);
               adev->pcm_modem_ul = NULL;
		ALOGE("voip:open voip_codec_pcm ul fail");
		return -1;
           }
       }

	if( 0 != pcm_start(adev->pcm_modem_dl)) {
		 ALOGE("voip pcm dl start unsucessfully");
	}
	if( 0 != pcm_start(adev->pcm_modem_ul)) {
	    ALOGE("voip pcm ul start unsucessfully");
	}
    ALOGD("voip:open codec pcm out 2");

    ALOGE("voip:open voip_codec_pcm out");
    return 0;
}

static int  close_voip_codec_pcm(struct tiny_audio_device *adev)
{
    ALOGD("voip:close voip codec pcm in adev->voip_state is %x",adev->voip_state);
    if(!adev->voip_state) {
       if(adev->pcm_modem_ul) {
           pcm_close(adev->pcm_modem_ul);
           adev->pcm_modem_ul = NULL;
       }
       if(adev->pcm_modem_dl) {
           pcm_close(adev->pcm_modem_dl);
           adev->pcm_modem_dl = NULL;
       }
    }
    ALOGD("voip:close voip codec pcm out");
    return 0;
}




#ifdef AUDIO_MUX_PCM
static int start_mux_output_stream(struct tiny_stream_out *out)
{
    unsigned int card = 0;
    unsigned int port = PORT_MM;
    struct pcm_config old_pcm_config;
    int ret=0;
    card = s_vaudio;
    old_pcm_config=out->config;
    out->config = pcm_config_vplayback;
    out->buffer_vplayback = malloc(RESAMPLER_BUFFER_SIZE);
    if(!out->buffer_vplayback){
        ALOGE("start_mux_output_stream: alloc fail, size: %d", RESAMPLER_BUFFER_SIZE);
        goto error;
    }
    else {
        memset(out->buffer_vplayback, 0, RESAMPLER_BUFFER_SIZE);
    }
    out->pcm_vplayback= mux_pcm_open(card, port, PCM_OUT, &out->config);
    if (!pcm_is_ready(out->pcm_vplayback)) {
        goto error;
    }
    else {
        ret = create_resampler( DEFAULT_OUT_SAMPLING_RATE,
                out->config.rate,
                out->config.channels,
                RESAMPLER_QUALITY_DEFAULT,
                NULL,
                &out->resampler_vplayback);
        if (ret != 0) {
            goto error;
        }
    }
    return 0;

error:
    out->config = old_pcm_config ;
    if(out->buffer_vplayback){
        free(out->buffer_vplayback);
        out->buffer_vplayback=NULL;
    }
    if(out->pcm_vplayback){
        ALOGE("start_vaudio_output_stream error: %s", pcm_get_error(out->pcm_vplayback));
        mux_pcm_close(out->pcm_vplayback);
        out->pcm_vplayback=NULL;
        ALOGE("start_vaudio_output_stream: out\n");
    }
    return -1;
}
#endif
static int start_sco_output_stream(struct tiny_stream_out *out)
{
    unsigned int card = 0;
    unsigned int port = PORT_MM;
	struct tiny_audio_device *adev = out->dev;

    int ret=0;
    BLUE_TRACE(" start_sco_output_stream in");
    s_voip = get_snd_card_number(CARD_SCO);
    card = s_voip;
    out->buffer_voip = malloc(RESAMPLER_BUFFER_SIZE);
    if(!out->buffer_voip){
        ALOGE("start_sco_output_stream: alloc fail, size: %d", RESAMPLER_BUFFER_SIZE);
        goto error;
    }
    else {
        memset(out->buffer_voip, 0, RESAMPLER_BUFFER_SIZE);
    }
    //out->pcm_voip = pcm_open(card, port, PCM_OUT, &pcm_config_scoplayback);

	ALOGD("start_sco_output_stream ok 1 ");
    open_voip_codec_pcm(adev);
    ALOGD("start_sco_output_stream error ok");
    out->pcm_voip = pcm_open(card, port, PCM_OUT| PCM_MMAP |PCM_NOIRQ, &pcm_config_scoplayback);
    ALOGD("start_sco_output_stream ok 4");


    if (!pcm_is_ready(out->pcm_voip)) {
        goto error;
    }
    else {
        ret = create_resampler( DEFAULT_OUT_SAMPLING_RATE,
                pcm_config_scoplayback.rate,
                out->config.channels,
                RESAMPLER_QUALITY_DEFAULT,
                NULL,
                &out->resampler_sco);
        if (ret != 0) {
            goto error;
        }
    }

    ALOGE("start_sco_output_stream error ok");
    return 0;

error:
    ALOGE("start_sco_output_stream error ");
    if(out->buffer_voip){
        free(out->buffer_voip);
        out->buffer_voip=NULL;
    }
    if(out->pcm_voip){
        ALOGE("start_sco_output_stream error: %s", pcm_get_error(out->pcm_voip));
        pcm_close(out->pcm_voip);
        out->pcm_voip=NULL;
        ALOGE("start_sco_output_stream: out\n");
    }
    return -1;
}

static int start_bt_sco_output_stream(struct tiny_stream_out *out)
{
    unsigned int card = 0;
    unsigned int port = PORT_MM;
    struct tiny_audio_device *adev = out->dev;

    int ret=0;
    BLUE_TRACE(" start_bt_sco_output_stream in");
    card = s_bt_sco;
    out->buffer_bt_sco = malloc(RESAMPLER_BUFFER_SIZE);
    if(!out->buffer_bt_sco){
        ALOGE("start_bt_sco_output_stream: alloc fail, size: %d", RESAMPLER_BUFFER_SIZE);
        goto error;
    }
    else {
        memset(out->buffer_bt_sco, 0, RESAMPLER_BUFFER_SIZE);
    }

    /* if bt sco capture stream has already been started, we just close bt sco capture
       stream. we will call start_input_stream in the next in_read func to start bt sco
       capture stream again.
       here we must get in->lock, because in_read maybe call pcm_read in the same time.
    */
    ALOGE("bt sco : %s before", __func__);
    if(adev->bt_sco_state & BT_SCO_UPLINK_IS_STARTED) {
        struct tiny_stream_in *bt_sco_in = adev->active_input;
        if(bt_sco_in) {
            ALOGE("bt sco : %s do_input_standby", __func__);
            pthread_mutex_lock(&bt_sco_in->lock);
            do_input_standby(bt_sco_in);
            pthread_mutex_unlock(&bt_sco_in->lock);
        }
    }
    adev->bt_sco_state |= BT_SCO_DOWNLINK_IS_EXIST;

    ALOGE("bt sco : %s after", __func__);

    //out->pcm_voip = pcm_open(card, port, PCM_OUT, &pcm_config_scoplayback);

    ALOGD("start_bt_sco_output_stream ok 1 ");
    out->pcm_bt_sco = pcm_open(card, port, PCM_OUT| PCM_MMAP |PCM_NOIRQ, &pcm_config_btscoplayback);
    ALOGD("start_bt_sco_output_stream ok 4");////


    if (!pcm_is_ready(out->pcm_bt_sco)) {
        goto error;
    }
    else {
        ret = create_resampler( DEFAULT_OUT_SAMPLING_RATE,
                pcm_config_btscoplayback.rate,
                out->config.channels,
                RESAMPLER_QUALITY_DEFAULT,
                NULL,
                &out->resampler_bt_sco);
        if (ret != 0) {
            goto error;
        }
    }

    ALOGE("start_bt_sco_output_stream error ok");
    return 0;

error:
    ALOGE("start_sco_output_stream error ");
    if(out->buffer_bt_sco){
        free(out->buffer_bt_sco);
        out->buffer_bt_sco=NULL;
    }
    if(out->pcm_bt_sco){
        ALOGE("start_sco_output_stream error: %s", pcm_get_error(out->pcm_bt_sco));
        pcm_close(out->pcm_bt_sco);
        out->pcm_bt_sco=NULL;
        ALOGE("start_sco_output_stream: out\n");
    }
    return -1;
}


/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct tiny_stream_out *out)
{
    struct tiny_audio_device *adev = out->dev;
    unsigned int card = 0;
    unsigned int port = PORT_MM;
    struct pcm_config old_pcm_config={0};
    int ret=0;

    adev->active_output = out;
    ALOGD("start output stream out->is_voip is %d, in",out->is_voip);

    if (!adev->call_start && adev->voip_state == 0) {
        /* FIXME: only works if only one output can be active at a time*/
        adev->out_devices &= (~AUDIO_DEVICE_OUT_ALL);
        adev->out_devices |= out->devices;
        if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
            i2s_pin_mux_sel(adev,2);
        }
	adev->prev_out_devices = ~adev->out_devices;
        select_devices_signal(adev);
    }
    else if (adev->voip_state) {
        at_cmd_cp_usecase_type(AUDIO_CP_USECASE_VOIP_1);  /* set the usecase type to cp side */
        if((adev->out_devices &AUDIO_DEVICE_OUT_ALL) != out->devices) {
            adev->out_devices &= (~AUDIO_DEVICE_OUT_ALL);
            adev->out_devices |= out->devices;
        }
	adev->prev_out_devices = ~adev->out_devices;
        select_devices_signal(adev);
    }
    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    if(out->is_voip){
        ret=start_sco_output_stream(out);
        if(ret){
            return ret;
        }
    }
    else if(out->is_bt_sco){
        ret=start_bt_sco_output_stream(out);
        if(ret){
            return ret;
        }
    }
    else if(adev->call_connected && ( !out->pcm_vplayback)) {
#ifdef AUDIO_MUX_PCM
        ret=start_mux_output_stream(out);
#else
        ret=start_vaudio_output_stream(out);
#endif
        if(ret){
            return ret;
        }
    }
    else {
        {
            static int16 start_index = 0, i=0;
            int16 temp_data[16]={0x0};
            start_index++;
            for(i=0; i<16; i++)
            {
                temp_data[i]  = 0x5555;
            }
            temp_data[0]=start_index;
#ifdef AUDIO_DUMP_EX
            dump_info.buf = temp_data;
            dump_info.buf_len = 32;
            dump_info.dump_switch_info = DUMP_MUSIC_HWL_BEFOORE_VBC;
            dump_data(dump_info);
#endif
        }
        BLUE_TRACE("open s_tinycard in");
        card = s_tinycard;
	if(out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
	    out->config = pcm_config_mm;
	}
	else {
	    out->config = pcm_config_mm_fast;
	}
        out->low_power = 1;

        out->pcm = pcm_open(card, port, PCM_OUT | PCM_MMAP | PCM_NOIRQ, &out->config);

        if (!pcm_is_ready(out->pcm)) {
            ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm));
            pcm_close(out->pcm);
            out->pcm = NULL;
            adev->active_output = NULL;
            return -ENOMEM;
        }
        BLUE_TRACE("open s_tinycard successfully");
    }

    out->resampler->reset(out->resampler);
#ifdef AUDIO_DUMP
    out_dump_create(&out->out_dump_fd, AUDIO_OUT_FILE_PATH);
#endif

    return 0;
}

static int check_input_parameters(uint32_t sample_rate, int format, int channel_count)
{
    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 2))
        return -EINVAL;

    switch(sample_rate) {
        case 8000:
        case 11025:
        case 16000:
        case 22050:
        case 24000:
        case 32000:
        case 44100:
        case 48000:
            break;
        default:
            return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;
    size_t device_rate;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
       multiple of 16 frames, as audioflinger expects audio buffers to
       be a multiple of 16 frames */
    size = (pcm_config_mm_ul.period_size * sample_rate) / pcm_config_mm_ul.rate;
    size = ((size + 15) / 16) * 16;

    return size * channel_count * sizeof(short);
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    return DEFAULT_OUT_SAMPLING_RATE;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct tiny_stream_out *out = (struct tiny_stream_out *)stream;

    /* take resampling into account and return the closest majoring
       multiple of 16 frames, as audioflinger expects audio buffers to
       be a multiple of 16 frames */
    size_t size = (out->config.period_size * DEFAULT_OUT_SAMPLING_RATE) / out->config.rate;
    size = ((size + 15) / 16) * 16;
    BLUE_TRACE("[TH] size=%d, frame_size=%d", size, audio_stream_frame_size(stream));
    return size * audio_stream_frame_size(stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    return (audio_channel_mask_t)AUDIO_CHANNEL_OUT_STEREO;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct tiny_stream_out *out)
{
    struct tiny_audio_device *adev = out->dev;
    ALOGD("do_output_standby in");
    if (!out->standby) {
        if (out->pcm) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        BLUE_TRACE("do_output_standby.mode:%d ",adev->mode);
        adev->active_output = 0;
        if(out->pcm_voip) {
            pcm_close(out->pcm_voip);
            out->pcm_voip = NULL;
        }
        if(out->buffer_voip) {
            free(out->buffer_voip);
            out->buffer_voip = 0;
        }
        if(out->resampler_sco) {
            release_resampler(out->resampler_sco);
            out->resampler_sco= 0;
        }
        if(out->is_voip) {
            adev->voip_state &= (~VOIP_PLAYBACK_STREAM);
            if(!adev->voip_state) {
                if(!adev->voip_state) {
                    close_voip_codec_pcm(adev);
                }
            }
            out->is_voip = false;
        }
        if(out->pcm_bt_sco) {
            /* if bt sco capture stream is started now, we must stop bt sco capture
               stream first when we close bt sco playback stream.
               we will call start_input_stream in the next in_read func to start bt
               sco capture stream again.
            */
            ALOGE("bt sco : %s before", __func__);
            if(adev->bt_sco_state & BT_SCO_UPLINK_IS_STARTED) {
                struct tiny_stream_in *bt_sco_in = adev->active_input;
                if(bt_sco_in) {
                    ALOGE("bt sco : %s do_input_standby", __func__);
                    pthread_mutex_lock(&bt_sco_in->lock);
                    do_input_standby(bt_sco_in);
                    pthread_mutex_unlock(&bt_sco_in->lock);
                }
            }
            ALOGE("bt sco : %s after", __func__);

            pcm_close(out->pcm_bt_sco);
            out->pcm_bt_sco = NULL;

            ALOGE("bt sco : %s downlink is not exist", __func__);
            adev->bt_sco_state &= (~BT_SCO_DOWNLINK_IS_EXIST);
        }
        if(out->buffer_bt_sco) {
            free(out->buffer_bt_sco);
            out->buffer_bt_sco = 0;
        }
        if(out->resampler_bt_sco) {
            release_resampler(out->resampler_bt_sco);
            out->resampler_bt_sco= 0;
        }
        if(out->is_bt_sco) {
            out->is_bt_sco=false;
        }
        if(out->pcm_vplayback) {
#ifdef AUDIO_MUX_PCM
            mux_pcm_close(out->pcm_vplayback);
#else
            pcm_close(out->pcm_vplayback);
#endif

            out->pcm_vplayback = NULL;
            if(out->buffer_vplayback) {
                free(out->buffer_vplayback);
                out->buffer_vplayback = 0;
            }
            if(out->resampler_vplayback) {
                release_resampler(out->resampler_vplayback);
                out->resampler_vplayback = 0;
            }
        }
#ifdef AUDIO_DUMP
        out_dump_release(&out->out_dump_fd);
#endif

        out->standby = 1;
    }
    adev->active_output = NULL;
    ALOGD("do_output_standby in out");
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct tiny_stream_out *out = (struct tiny_stream_out *)stream;
    int status;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct tiny_stream_out *out = (struct tiny_stream_out *)stream;
    struct tiny_audio_device *adev = out->dev;
    struct tiny_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    static int cur_mode = 0;

    BLUE_TRACE("[out_set_parameters], kvpairs=%s devices:0x%x mode:%d ", kvpairs,adev->out_devices,adev->mode);

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        ALOGW("[out_set_parameters],after str_parms_get_str,val(0x%x) ",val);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);
	if (((adev->out_devices & AUDIO_DEVICE_OUT_ALL) != val) && ((val != 0) || ((val == 0) && (adev->out_devices & AUDIO_DEVICE_OUT_ALL_FM))) //val=0 will cause XRUN. So ignore the "val=0"expect for closing FM path.
                  || (AUDIO_MODE_IN_CALL == adev->mode)
                  ||adev->voip_start) {
            adev->out_devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_devices |= val;
            out->devices = val;
            ALOGW("out_set_parameters want to set devices:0x%x old_mode:%d new_mode:%d call_start:%d ",adev->out_devices,cur_mode,adev->mode,adev->call_start);

            if ((val & AUDIO_DEVICE_OUT_ALL_FM)
                    && (adev->call_start || AUDIO_MODE_IN_CALL == adev->mode || adev->voip_start)) {
                ALOGW("[out_set_parameters], devices:0x%x mode:%d call_start:%d voip_start:%d",
                        val,adev->mode,adev->call_start,adev->voip_start);
                pthread_mutex_unlock(&out->lock);
                pthread_mutex_unlock(&adev->lock);
                return 0;
            }
            if(1 == adev->call_start) {
                if(adev->out_devices & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_ALL_SCO)) {
                    if(adev->cp_type == CP_TG)
                        i2s_pin_mux_sel(adev,1);
                    else if(adev->cp_type == CP_W)
                        i2s_pin_mux_sel(adev,0);
                }
            }
            else {
                if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
                    i2s_pin_mux_sel(adev,2);
                }
            }

            cur_mode = adev->mode;
            if((!adev->call_start)&&(!adev->voip_state))
                select_devices_signal(adev);
            pthread_mutex_unlock(&out->lock);
            pthread_mutex_unlock(&adev->lock);


              if ((AUDIO_MODE_IN_CALL == adev->mode) || adev->voip_start) {
                ret = at_cmd_route(adev);  //send at command to cp
                if (ret < 0) {
                    ALOGE("out_set_parameters at_cmd_route error(%d) ",ret);
                    return ret;
                }
            }
        }else{
            pthread_mutex_unlock(&out->lock);
            pthread_mutex_unlock(&adev->lock);
            ALOGW("the same devices(0x%x) with val(0x%x) val is zero...",adev->out_devices,val);
        }
    }

    ALOGW("out_set_parameters out...call_start:%d",adev->call_start);
    str_parms_destroy(parms);
    return ret;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    return strdup("");
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct tiny_stream_out *out = (struct tiny_stream_out *)stream;

    return (out->config.period_size * out->config.period_count * 1000) / out->config.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
        float right)
{
    return -ENOSYS;
}

static bool out_bypass_data(struct tiny_stream_out *out, uint32_t frame_size, uint32_t sample_rate, size_t bytes)
{
    /*
       1. There is some time between call_start and call_connected, we should throw away some data here.
       2. During in  AUDIO_MODE_IN_CALL and not in call_start, we should throw away some data in BT device.
       3. If mediaserver crash, we should throw away some pcm data after restarting mediaserver.
       4. After call thread gets stop_call cmd, but hasn't get lock.
       */
    struct tiny_audio_device *adev = out->dev;

    if (( (!adev->call_start) && (adev->mode == AUDIO_MODE_IN_CALL) && (adev->out_devices & AUDIO_DEVICE_OUT_ALL_SCO) )
            || (adev->call_start && (!adev->call_connected))
            || ((!adev->vbc_2arm) && (!adev->call_start) && (adev->mode == AUDIO_MODE_IN_CALL))
            || adev->call_prestop) {
        MY_TRACE("out_write throw away data call_start(%d) mode(%d) devices(0x%x) call_connected(%d) vbc_2arm(%d) call_prestop(%d)...",adev->call_start,adev->mode,adev->out_devices,adev->call_connected,adev->vbc_2arm,adev->call_prestop);
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&out->lock);
        usleep((int64_t)bytes * 1000000 / frame_size / sample_rate);
        return true;
    }
    else {
        return false;
    }
}


#ifdef AUDIO_MUX_PCM
static ssize_t out_write_mux(struct tiny_stream_out *out, const void* buffer,
        size_t bytes)
{
    void *buf;
    int ret=0;
    size_t frame_size = 0;
    size_t in_frames = 0;
    size_t out_frames =0;
    BLUE_TRACE("mux_playback out_write call_start(%d) call_connected(%d) ...in....",out->dev->call_start,out->dev->call_connected);
    frame_size = audio_stream_frame_size((const struct audio_stream *)(&out->stream.common));
    ALOGE(":out_write_mux in frame_size is %d",frame_size);
    in_frames = bytes / frame_size;
    out_frames = RESAMPLER_BUFFER_SIZE / frame_size;

    if(out->pcm_vplayback) {
        out->resampler_vplayback->resample_from_input(out->resampler_vplayback,
                (int16_t *)buffer,
                &in_frames,
                (int16_t *)out->buffer_vplayback,
                &out_frames);
        buf = out->buffer_vplayback;
        ret = mux_pcm_write(out->pcm_vplayback, (void *)buf, out_frames*frame_size);
        ALOGE(": mux_pcm_write out ret is %d",ret);
    }
    else
        usleep(out_frames*1000*1000/out->config.rate);
    BLUE_TRACE("muxplayback write over result is %d,frame_size is %d in frames %d, out frames %d",ret,frame_size,in_frames,out_frames);
    return 0;
}

#endif


static ssize_t out_write_vaudio(struct tiny_stream_out *out, const void* buffer,
        size_t bytes)
{
    void *buf;
    int ret = 0;
    size_t frame_size = 0;
    size_t in_frames = 0;
    size_t out_frames =0;
    frame_size = audio_stream_frame_size(&out->stream.common);
    in_frames = bytes / frame_size;
    out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    if(out->pcm_vplayback) {
        BLUE_TRACE("out_write_vaudio in bytes is %d",bytes);
        out->resampler_vplayback->resample_from_input(out->resampler_vplayback,
                (int16_t *)buffer,
                &in_frames,
                (int16_t *)out->buffer_vplayback,
                &out_frames);
        buf = out->buffer_vplayback;

		int i=0;
		int16_t * buf_p = buffer;
		/*for(i=0;i<5;i++)
		{
			VOIP_TRACE("voip:out_write_vaudio is %d,%d,%d,%d,%d,%d,%d,%d,%d,%d",*(buf_p+0),*(buf_p+1),*(buf_p+2),*(buf_p+3),*(buf_p+4),*(buf_p+5),*(buf_p+6),*(buf_p+7),*(buf_p+8),*(buf_p+9));
		}*/

        ret = pcm_mmap_write(out->pcm_vplayback, (void *)buf, out_frames*frame_size);
        BLUE_TRACE("out_write_vaudio out out frames  is %d",out_frames);
    }
    else
        usleep(out_frames*1000*1000/out->config.rate);

    return ret;
}

static ssize_t out_write_sco(struct tiny_stream_out *out, const void* buffer,
        size_t bytes)
{
    void *buf;
    //void *buffer1 = NULL;
    int ret;
    size_t frame_size = 0;
    size_t in_frames = 0;
    size_t out_frames =0;

    frame_size = audio_stream_frame_size((const struct audio_stream *)(&out->stream.common));
    in_frames = bytes / frame_size;
    out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    BLUE_TRACE("out_write_sco in bytes is %d,frame_size %d, in_frames %d, out_frames %d,out->pcm_voip %x", bytes, frame_size,in_frames, out_frames,out->pcm_voip);
    if(out->pcm_voip) {
        out->resampler_sco->resample_from_input(out->resampler_sco,
                (int16_t *)buffer,
                &in_frames,
                (int16_t *)out->buffer_voip,
                &out_frames);
        buf = out->buffer_voip;
        if(frame_size == 4){
            pcm_mixer(buf, out_frames*(frame_size/2));

        }

        ret = pcm_mmap_write(out->pcm_voip, (void *)buf, out_frames*frame_size/2);
        if(ret < 0) {
            ALOGE("out_write_sco: pcm_mmap_write error: ret %d", ret);
        }
    }
    else
        usleep(out_frames*1000*1000/out->config.rate);

    //BLUE_TRACE("voip:out_write_sco out bytes is %d,frame_size %d, in_frames %d, out_frames %d,out->pcm_voip %x", bytes, frame_size,in_frames, out_frames,out->pcm_voip);
    return ret;
}

static ssize_t out_write_bt_sco(struct tiny_stream_out *out, const void* buffer,
        size_t bytes)
{
    void *buf;
    int ret = -1;
    size_t frame_size = 0;
    size_t in_frames = 0;
    size_t out_frames =0;

    frame_size = audio_stream_frame_size(&out->stream.common);
    in_frames = bytes / frame_size;
    out_frames = RESAMPLER_BUFFER_SIZE / frame_size;
    BLUE_TRACE("out_write_bt_sco in bytes is %d,frame_size %d, in_frames %d, out_frames %d,out->pcm_voip %x", bytes, frame_size,in_frames, out_frames,out->pcm_voip);
    if(out->pcm_bt_sco) {
        out->resampler_bt_sco->resample_from_input(out->resampler_bt_sco,
                (int16_t *)buffer,
                &in_frames,
                (int16_t *)out->buffer_bt_sco,
                &out_frames);
        buf = out->buffer_bt_sco;
        if(frame_size == 4) {
            pcm_mixer(buf, out_frames*(frame_size/2));
        }

       //ret = pcm_write(out->pcm_voip, (void *)buf, out_frames*frame_size/2);
#ifdef AUDIO_DUMP
       out_dump_doing(out->out_dump_fd, (void *)buf, out_frames*frame_size/2);
#endif

        BLUE_TRACE("voip:out_write_bt_sco");
        ret = pcm_mmap_write(out->pcm_bt_sco, (void *)buf, out_frames*frame_size/2);
    }
    else{

        usleep(out_frames*1000*1000/out->config.rate);
        return ret;
    }

    BLUE_TRACE("voip:out_write_bt_sco out bytes is %d,frame_size %d, in_frames %d, out_frames %d,out->pcm_voip %x", bytes, frame_size,in_frames, out_frames,out->pcm_voip);
    return ret;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
        size_t bytes)
{
    int ret = 0;
    struct tiny_stream_out *out = (struct tiny_stream_out *)stream;
    struct tiny_audio_device *adev = out->dev;
    audio_modem_t *modem = adev->cp;
    size_t frame_size = 0;
    size_t in_frames = 0;
    size_t out_frames =0;
    struct tiny_stream_in *in;
    bool low_power;
    int kernel_frames;
    void *buf;
    static long time1=0, time2=0, deltatime=0, deltatime2=0;
    static long time3=0, time4=0, deltatime3=0,write_index=0, writebytes=0;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    //ALOGD("out_write1: out->devices %x,start: out->is_voip is %d, adev->voip_state is %d,adev->voip_start is %d",out->devices,out->is_voip,adev->voip_state,adev->voip_start);
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (0==out->standby) {//in playing
        if(modem->debug_info.enable) {
            time3 = getCurrentTimeUs();
            if(time4>time3) {
                deltatime3 = time4-time3;
                if(deltatime3>modem->debug_info.lastthis_outwritetime_gate) {
                    ALOGD("out_write lastthisoutwritedebuginfo time: %d(gate, us), %ld(us), %d(bytes).",
                        modem->debug_info.lastthis_outwritetime_gate,
                        deltatime3, bytes);
                }
            }
        }
    }

#ifdef AUDIO_DUMP_EX
    dump_info.buf = (void *)buffer;
    dump_info.buf_len = bytes;
    dump_info.dump_switch_info = DUMP_MUSIC_HWL_BEFORE_EXPRESS;
    dump_data(dump_info);
#endif

    if (out_bypass_data(out,audio_stream_frame_size(&stream->common),out_get_sample_rate(&stream->common),bytes)) {
        return bytes;
    }
#ifdef VOIP_DSP_PROCESS
    if (((adev->voip_start == 1) && (!(out->devices & AUDIO_DEVICE_OUT_ALL_SCO)))&&(!adev->call_start))
    {
        if(!out->is_voip ) {

            ALOGD("sco:out_write start and do standby");
            adev->voip_state |= VOIP_PLAYBACK_STREAM;
            force_standby_for_voip(adev);
            ALOGI("out->is_voip is %d",out->is_voip);
            do_output_standby(out);
            out->is_voip=true;
        }
    }
    else {
        if(out->is_voip) {
            ALOGD("sco:out_write stop and do standby");
            do_output_standby(out);
        }
    }
#endif
    if((adev->mode  != AUDIO_MODE_IN_CALL) && (out->devices & AUDIO_DEVICE_OUT_ALL_SCO)) {
        if(!out->is_bt_sco) {
            ALOGI("bt_sco:out_write_start and do standby");
            do_output_standby(out);
            out->is_bt_sco=true;
        }
    }
    else {
        if(out->is_bt_sco) {
           ALOGI("bt_sco:out_write_stop and do standby");
            do_output_standby(out);
        }
    }

    //ALOGD("into out_write 2: start:out->devices %x,out->is_voip is %d, voip_state is %d,adev->voip_start is %d",out->devices,out->is_voip,adev->voip_state,adev->voip_start);
    if((!out->is_voip) && adev->voip_state) {
        ALOGE("out_write: drop data and sleep,out->is_voip is %d, adev->voip_state is %d,adev->voip_start is %d",out->is_voip,adev->voip_state,adev->voip_start);
        usleep(100000);

        pthread_mutex_unlock(&out->lock);
        pthread_mutex_unlock(&adev->lock);
        return bytes;
    }

    if (out->standby) {
        out->standby = 0;
        if(modem->debug_info.enable) {
            write_index=0;
            writebytes=0;
        }
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
    }
    low_power = adev->low_power && !adev->active_input;
    pthread_mutex_unlock(&adev->lock);
    //ALOGD("into out_write 3: start: out->is_bt_sco is %d, out->devices is %x,out->is_voip is %d, voip_state is %d,adev->voip_start is %d",out->is_bt_sco,out->devices,out->is_voip,adev->voip_state,adev->voip_start);

    if(out->is_voip){
        //BLUE_TRACE("sco playback out_write call_start(%d) call_connected(%d) ...in....",adev->call_start,adev->call_connected);
        ret=out_write_sco(out,buffer,bytes);
    }
    else if(out->is_bt_sco){
        //BLUE_TRACE("out_write_bt_sco call_start(%d) call_connected(%d) ...in....",adev->call_start,adev->call_connected);
        ret=out_write_bt_sco(out,buffer,bytes);
    }
    else if (adev->call_connected) {
#ifdef AUDIO_MUX_PCM
        ret=out_write_mux(out,buffer,bytes);
#else
        ret=out_write_vaudio(out,buffer,bytes);
#endif

    }else {
        frame_size = audio_stream_frame_size((const struct audio_stream *)(&out->stream.common));
        in_frames = bytes / frame_size;
        out_frames = RESAMPLER_BUFFER_SIZE / frame_size;

        if ((low_power != out->low_power) && (out->flags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) {
            if (low_power) {
                out->config.avail_min = (out->config.period_size*out->config.period_count * 3) / 4;
                ALOGW("low_power out->write_threshold=%d, config.avail_min=%d, start_threshold=%d",
                        out->write_threshold,out->config.avail_min, out->config.start_threshold);
            } else {
                out->config.avail_min = SHORT_PERIOD_SIZE;
                ALOGW("out->write_threshold=%d, config.avail_min=%d, start_threshold=%d",
                        out->write_threshold,out->config.avail_min, out->config.start_threshold);
            }
            pcm_set_avail_min(out->pcm, out->config.avail_min);
            out->low_power = low_power;
        }

        /* only use resampler if required */
        if (out->config.rate != DEFAULT_OUT_SAMPLING_RATE) {
            out->resampler->resample_from_input(out->resampler,
                    (int16_t *)buffer,
                    &in_frames,
                    (int16_t *)out->buffer,
                    &out_frames);
            buf = out->buffer;
        } else {
            out_frames = in_frames;
            buf = (void *)buffer;
        }
        XRUN_TRACE("in_frames=%d, out_frames=%d", in_frames, out_frames);
        XRUN_TRACE("out->write_threshold=%d, config.avail_min=%d, start_threshold=%d",
                out->write_threshold,out->config.avail_min, out->config.start_threshold);
        if(modem->debug_info.enable) {
            time1 = getCurrentTimeUs();
        }
        ret = pcm_mmap_write(out->pcm, (void *)buf, out_frames * frame_size);//music playback
        if(modem->debug_info.enable) {
            time2 = getCurrentTimeUs();
            deltatime2 = ((time1>time2)?(time1-time2):(time2-time1));
        }

#ifdef AUDIO_DUMP
    out_dump_doing(out->out_dump_fd, (void *)buf, out_frames * frame_size);
#endif
#ifdef AUDIO_DUMP_EX
    dump_info.buf = buf;
    dump_info.buf_len = out_frames * frame_size;
    dump_info.dump_switch_info = DUMP_MUSIC_HWL_BEFOORE_VBC;
    dump_data(dump_info);
#endif
    }
    if(modem->debug_info.enable) {
        int unnormal = 0;
        time4 = getCurrentTimeUs();

        write_index++;
        writebytes += bytes;

        if(deltatime3>modem->debug_info.lastthis_outwritetime_gate) {
            unnormal = 1;
        }

        if(deltatime2>modem->debug_info.pcmwritetime_gate) {
            unnormal = 1;
        }

        if(deltatime > modem->debug_info.sleeptime_gate) {
            unnormal = 1;
        }
        if(unnormal) {
            ALOGD("out_write debug %ld(index), %d(bytes), %ld(total bytes), lastthis:%ld, %d(gate), pcmwrite:%ld, %d(gate), sleep:%ld, %d(gate).",
                        write_index, bytes, writebytes,
                        deltatime3, modem->debug_info.lastthis_outwritetime_gate,
                        deltatime2, modem->debug_info.pcmwritetime_gate,
                        deltatime, modem->debug_info.sleeptime_gate);
        }
    }

exit:
    if (ret != 0) {
        if(out->pcm_voip)
            ALOGW("warning:%d, (%s)", ret, pcm_get_error(out->pcm_voip));
        if (out->pcm)
            ALOGW("warning:%d, (%s)", ret, pcm_get_error(out->pcm));
        else if (out->pcm_vplayback)
            ALOGW("vwarning:%d, (%s)", ret, pcm_get_error(out->pcm_vplayback));
        do_output_standby(out);
        usleep(10000);
    }
    pthread_mutex_unlock(&out->lock);
    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
        uint32_t *dsp_frames)
{
    return -EINVAL;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

/** audio_stream_in implementation **/

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
        struct resampler_buffer* buffer);


static void release_buffer(struct resampler_buffer_provider *buffer_provider,
        struct resampler_buffer* buffer);


static int in_deinit_resampler(struct tiny_stream_in *in)
{
    if (in->resampler) {
        release_resampler(in->resampler);
        in->resampler = NULL;
    }

    if(in->buffer) {
        free(in->buffer);
        in->buffer = NULL;
    }
    return 0;
}


static int in_init_resampler(struct tiny_stream_in *in)
{
    int ret=0;
    int size=0;
    in->buf_provider.get_next_buffer = get_next_buffer;
    in->buf_provider.release_buffer = release_buffer;

    size = in->config.period_size * audio_stream_frame_size(&in->stream.common);
    in->buffer = malloc(size);
    if (!in->buffer) {
        ALOGE("in_init_resampler: alloc fail, size: %d", size);
        ret = -ENOMEM;
        goto err;
    }
    else {
        memset(in->buffer, 0, size);
    }

    ret = create_resampler(in->config.rate,
            in->requested_rate,
            in->config.channels,
            RESAMPLER_QUALITY_DEFAULT,
            &in->buf_provider,
            &in->resampler);
    if (ret != 0) {
        ret = -EINVAL;
        goto err;
    }

    return ret;

err:
    in_deinit_resampler(in);
    return ret;
}

/* audio_bt_sco_dup_thread_func is just to handle bt sco capture stream.
   The thread will write zero data to bt_sco_card if bt sco playback is
   not started. The thread will stop to write zero data to bt_sco_card
   if bt sco playback is started.
*/
static void *audio_bt_sco_dup_thread_func(void * param)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)param;
    struct pcm *bt_sco_playback = NULL;
    int ret = 0;
    void *buf = NULL;

    buf = malloc(SHORT_PERIOD_SIZE);
    if(!buf) {
        ALOGE("bt sco : alloc buffer for bt sco output fail");
        goto exit;
    } else {
        memset(buf, 0, SHORT_PERIOD_SIZE);
    }

    while(!adev->bt_sco_manager.thread_is_exit) {
        pthread_mutex_lock(&adev->bt_sco_manager.cond_mutex);
        if(adev->bt_sco_manager.dup_need_start) {
            pthread_mutex_unlock(&adev->bt_sco_manager.cond_mutex);
            if(bt_sco_playback == NULL) {
                ALOGE("bt sco : duplicate downlink card opening");
                bt_sco_playback = pcm_open(s_bt_sco, PORT_MM, PCM_OUT| PCM_MMAP |PCM_NOIRQ, &pcm_config_btscoplayback);
                if(!pcm_is_ready(bt_sco_playback)) {
                    ALOGE("bt sco : duplicate downlink card open fail");
                    pcm_close(bt_sco_playback);
                    bt_sco_playback = NULL;
                    adev->bt_sco_state |= BT_SCO_DOWNLINK_OPEN_FAIL;
                } else {
                    adev->bt_sco_state &= (~BT_SCO_DOWNLINK_OPEN_FAIL);
                }

                ALOGE("bt sco : duplicate open downlink card signal");
                sem_post(&adev->bt_sco_manager.dup_sem);
            }

            ALOGV("bt sco : duplicate thread write");
            if(bt_sco_playback) {
                ret = pcm_mmap_write(bt_sco_playback, buf, SHORT_PERIOD_SIZE / 2);
                ALOGV("bt sco : duplicate thread write ret is %d", ret);
            }
        } else {
            if(bt_sco_playback) {
                pcm_close(bt_sco_playback);
                bt_sco_playback = NULL;

                ALOGE("bt sco : duplicate close downlink card signal");
                sem_post(&adev->bt_sco_manager.dup_sem);
            }

            ALOGE("bt sco : duplicate thread wait for start");
            pthread_cond_wait(&adev->bt_sco_manager.cond, &adev->bt_sco_manager.cond_mutex);
            pthread_mutex_unlock(&adev->bt_sco_manager.cond_mutex);

            ALOGE("bt sco : duplicate thread is started now ...");
        }
    }

exit:
    ALOGE("bt sco : duplicate thread exit");
    if(bt_sco_playback) {
        pcm_close(bt_sco_playback);
    }
    if(buf) {
        free(buf);
    }
    return NULL;
}

/* if bt sco playback stream is not started and bt sco capture stream is started, we will
   start duplicate_thread to write zero data to bt_sco_card.
   we will stop duplicate_thread to write zero data to bt_sco_card if bt sco playback stream
   is started or bt sco capture stream is stoped.
*/
static int audio_bt_sco_duplicate_start(struct tiny_audio_device *adev, bool enable)
{
    int ret = 0;
    ALOGE("bt sco : %s duplicate thread %s", __func__, (enable ? "start": "stop"));
    pthread_mutex_lock(&adev->bt_sco_manager.dup_mutex);
    if(enable != adev->bt_sco_manager.dup_count) {
        adev->bt_sco_manager.dup_count = enable;

        pthread_mutex_lock(&adev->bt_sco_manager.cond_mutex);
        adev->bt_sco_manager.dup_need_start = enable;
        pthread_cond_signal(&adev->bt_sco_manager.cond);
        pthread_mutex_unlock(&adev->bt_sco_manager.cond_mutex);

        ALOGE("bt sco : %s %s before wait", __func__, (enable ? "start": "stop"));
        sem_wait(&adev->bt_sco_manager.dup_sem);
        ALOGE("bt sco : %s %s after wait", __func__, (enable ? "start": "stop"));
    }
    if(enable && (adev->bt_sco_state & BT_SCO_DOWNLINK_OPEN_FAIL)) {
        adev->bt_sco_manager.dup_count = !enable;
        adev->bt_sco_manager.dup_need_start = !enable;
        ret = -1;
    } else {
        ret = 0;
    }
    pthread_mutex_unlock(&adev->bt_sco_manager.dup_mutex);
    return ret;
}

static int audio_bt_sco_thread_create(struct tiny_audio_device *adev)
{
    int ret = 0;
    pthread_attr_t attr;

    /* create a thread to bt sco playback.*/
    pthread_attr_init(&attr);
    memset(&adev->bt_sco_manager, 0, sizeof(struct bt_sco_thread_manager));
    adev->bt_sco_manager.thread_is_exit = false;
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(&adev->bt_sco_manager.dup_thread, &attr,
            audio_bt_sco_dup_thread_func, (void *)adev);
    if (ret) {
        ALOGE("bt sco : duplicate thread create fail, code is %d", ret);
    }
    pthread_attr_destroy(&attr);

    /* initialize mutex and condition variable objects */
    pthread_mutex_init(&adev->bt_sco_manager.dup_mutex, NULL);
    pthread_mutex_init(&adev->bt_sco_manager.cond_mutex, NULL);
    pthread_cond_init(&adev->bt_sco_manager.cond, NULL);
    sem_init(&adev->bt_sco_manager.dup_sem, 0, 0);

    return ret;
}

static void audio_bt_sco_thread_destory(struct tiny_audio_device *adev)
{
    int ret = 0;

    adev->bt_sco_manager.thread_is_exit = true;
    ALOGE("bt sco : duplicate thread destory before");
    ret = pthread_join(adev->bt_sco_manager.dup_thread, NULL);
    ALOGE("bt sco : duplicate thread destory ret is %d", ret);
    adev->bt_sco_manager.dup_thread = NULL;

    pthread_mutex_destroy(&adev->bt_sco_manager.dup_mutex);
    pthread_mutex_destroy(&adev->bt_sco_manager.cond_mutex);
    pthread_cond_destroy(&adev->bt_sco_manager.cond);
    sem_destroy(&adev->bt_sco_manager.dup_sem);
}

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct tiny_stream_in *in)
{
    int ret = 0;
    struct tiny_audio_device *adev = in->dev;
    struct pcm_config  old_config = in->config;
    adev->active_input = in;
    ALOGW("start_input_stream in mode:0x%x devices:0x%x call_start:%d ",adev->mode,adev->in_devices,adev->call_start);
    if (!adev->call_start) {
        adev->in_devices &= ~AUDIO_DEVICE_IN_ALL;
        adev->in_devices |= in->device;
        if((in->device & ~ AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            i2s_pin_mux_sel(adev,2);
        }
	adev->prev_in_devices = ~adev->in_devices;
        select_devices_signal(adev);
    }

    /* this assumes routing is done previously */

    if(in->is_voip) {
        //BLUE_TRACE("start sco input stream in");
		BLUE_TRACE("voip:start sco input stream in in->requested_channels %d,in->config.channels %d",in->requested_channels,in->config.channels);
        open_voip_codec_pcm(adev);
        BLUE_TRACE("voip:start sco input stream in 4");

        in->config = pcm_config_scocapture;
        if(in->config.channels  != in->requested_channels) {
            in->config.channels = in->requested_channels;
        }
        in->active_rec_proc = 0;
        BLUE_TRACE("in voip:opencard");
        in->pcm = pcm_open(s_voip,PORT_MM,PCM_IN,&in->config );
        if (!pcm_is_ready(in->pcm)) {
            goto err;
        }
#ifndef VOIP_DSP_PROCESS
        in->active_rec_proc = init_rec_process(GetAudio_InMode_number_from_device(adev), in->requested_rate );
        ALOGI("record process sco module created is %s.", in->active_rec_proc ? "successful" : "failed");
#endif
    }
    else if(in->is_bt_sco) {
        //BLUE_TRACE("start sco input stream in");
		BLUE_TRACE("voip:start bt sco input stream in");
        in->config = pcm_config_btscocapture;
        if(in->config.channels  != in->requested_channels) {
            ALOGE("bt sco input : in->requested_channels is %d, in->config.channels is %d",in->requested_channels, in->config.channels);
            in->config.channels = in->requested_channels;
        }
        in->active_rec_proc = 0;
        BLUE_TRACE("in  bt_sco:opencard");
        in->pcm = pcm_open(s_bt_sco,PORT_MM,PCM_IN,&in->config );
        if (!pcm_is_ready(in->pcm)) {
            goto err;
        }
        in->active_rec_proc = init_rec_process(GetAudio_InMode_number_from_device(adev), in->requested_rate );
        ALOGI("record process sco module created is %s.", in->active_rec_proc ? "successful" : "failed");

        if(in->requested_rate != in->config.rate) {
            ALOGE("bt sco input : in->requested_rate is %d, in->config.rate is %d",in->requested_rate, in->config.rate);
            ret= in_init_resampler(in);
            ALOGE("bt sco input : in_init_resampler ret is %d",ret);
            if(ret){
                goto err;
            }
        }

        /* we will start duplicate_thread to write zero data to bt_sco_card if bt sco playback stream is not started.
           we will let start_input_stream return error if duplicate_thread open bt_sco_card fail.
        */
        adev->bt_sco_state |= BT_SCO_UPLINK_IS_STARTED;

        ALOGE("bt sco : %s before", __func__);
        if(!(adev->bt_sco_state & BT_SCO_DOWNLINK_IS_EXIST)) {
            if(audio_bt_sco_duplicate_start(adev, true)) {
                ALOGE("bt sco : %s start duplicate fail");
                goto err;
            }
        }
        ALOGE("bt sco : %s after", __func__);
    }
    else if(adev->call_start) {
        int card=0;
        cp_type_t cp_type = CP_MAX;
        in->active_rec_proc = 0;
        in->config = pcm_config_record_incall;
        if(in->config.channels  != in->requested_channels) {
            ALOGE("%s, voice-call input : in->requested_channels is %d, in->config.channels is %d",
                    __func__,in->requested_channels, in->config.channels);
            in->config.channels = in->requested_channels;
        }
#ifdef AUDIO_MUX_PCM
        in->mux_pcm = mux_pcm_open(s_vaudio,PORT_MM,PCM_IN,&in->config);
        if (!pcm_is_ready(in->mux_pcm)) {
            ALOGE("voice-call rec cannot open pcm_in driver: %s", pcm_get_error(in->mux_pcm));
            mux_pcm_close(in->mux_pcm);
            adev->active_input = NULL;
            return -ENOMEM;
        }
#else
        cp_type = get_cur_cp_type(in->dev);
        if(cp_type == CP_TG) {
	        s_vaudio = get_snd_card_number(CARD_VAUDIO);
            card = s_vaudio;
        }
        else if(cp_type == CP_W) {
	        s_vaudio_w = get_snd_card_number(CARD_VAUDIO_W);
            card = s_vaudio_w;
        }

        in->pcm = pcm_open(card,PORT_MM,PCM_IN,&in->config);
        if (!pcm_is_ready(in->pcm)) {
            ALOGE("voice-call rec cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
            pcm_close(in->pcm);
            in->pcm = NULL;
            adev->active_input = NULL;
            return -ENOMEM;
        }
#endif

    }
    else {
        if(adev->out_devices & AUDIO_DEVICE_OUT_ALL_FM){
            in->config = pcm_config_fm_ul;
            if(in->config.channels != in->requested_channels) {
              in->config.channels = in->requested_channels;
            }
           in->pcm = pcm_open(s_tinycard, PORT_MM, PCM_IN, &in->config);
           if(!pcm_is_ready(in->pcm)) {
             pcm_close(in->pcm);
             ALOGE("fm rec cannot open pcm_in driver : %s,samplerate:%d", pcm_get_error(in->pcm),in->config.rate);
             in->pcm = NULL;
           }
           if(NULL == in->pcm){
               in->config =  pcm_config_mm_ul;
               in->config.rate = MM_LOW_POWER_SAMPLING_RATE;
               if(in->config.channels != in->requested_channels) {
                 in->config.channels = in->requested_channels;
               }
               ALOGE("fm rec try to open pcm_in driver again using samplerate:%d",in->config.rate);
               in->pcm = pcm_open(s_tinycard, PORT_MM, PCM_IN, &in->config);
               if(!pcm_is_ready(in->pcm)) {
                 goto err;
               }
           }
        } else {
          in->config = pcm_config_mm_ul;
          if(in->config.channels != in->requested_channels) {
              in->config.channels = in->requested_channels;
            }

            if(in->config.rate != in->requested_rate)
            {
                in->config.rate = in->requested_rate;
            }
            ALOGE("start_input_stream pcm_open_0");
            in->pcm = pcm_open(s_tinycard, PORT_MM, PCM_IN, &in->config);
            if(!pcm_is_ready(in->pcm)) {
                if(in->pcm) {
                    pcm_close(in->pcm);
                    in->pcm = NULL;
                }
                in->config.rate = pcm_config_mm_ul.rate;
                ALOGE("start_input_stream pcm_open_1");
                in->pcm = pcm_open(s_tinycard, PORT_MM, PCM_IN, &in->config);
                if(!pcm_is_ready(in->pcm)) {
                    ALOGE("start_input_stream pcm open err");
                    goto err;
                }
            }
        }
        /* start to process pcm data captured, such as noise suppression.*/
        in->active_rec_proc = init_rec_process(GetAudio_InMode_number_from_device(adev),in->requested_rate );
        ALOGI("record process module created is %s.", in->active_rec_proc ? "successful" : "failed");
    }

    if(in->requested_rate != in->config.rate) {
        ALOGE(": in->requested_rate is %d, in->config.rate is %d",in->requested_rate, in->config.rate);
        ret= in_init_resampler(in);
        ALOGE(": in_init_resampler ret is %d",ret);
        if(ret) {
            goto err;
        }
    }

    ALOGE("start_input,channels=%d,peroid_size=%d, peroid_count=%d,rate=%d",
            in->config.channels, in->config.period_size,
            in->config.period_count, in->config.rate);

    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
        in->frames_in = 0;
    }

    {
        int size = 0;
        int buf_size = 0;
        size = in->config.period_size;
        size = ((size + 15) / 16) * 16;
        buf_size =  size * 2 * sizeof(short);
        if(in->proc_buf_size < buf_size){
	    if(in->proc_buf)
		free(in->proc_buf);
            in->proc_buf = malloc(buf_size);
            if(!in->proc_buf) {
                goto err;
            }
            in->proc_buf_size = buf_size;
        }
    }
    ALOGE("start input stream out");
    return 0;

err:
    in->config = old_config;
    if(in->pcm) {
        pcm_close(in->pcm);
        ALOGE("normal rec cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        in->pcm = NULL;
        adev->active_input = NULL;
    }

    in_deinit_resampler(in);

    if (in->active_rec_proc) {
        AUDPROC_DeInitDp();
        in->active_rec_proc = 0;
    }
    return -1;

}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    size_t size;
    size_t device_rate;

    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;

    if (check_input_parameters(in->requested_rate, AUDIO_FORMAT_PCM_16_BIT, in->config.channels) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
       multiple of 16 frames, as audioflinger expects audio buffers to
       be a multiple of 16 frames */
    size = in->config.period_size ;
    size = ((size + 15) / 16) * 16;

    return size * in->config.channels * sizeof(short);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
{
    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;

    if (in->config.channels == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
{
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct tiny_stream_in *in)
{
    struct tiny_audio_device *adev = in->dev;
    ALOGI("%s, standby=%d, in_devices=0x%08x", __func__, in->standby, adev->in_devices);
    if (!in->standby) {
#ifdef AUDIO_MUX_PCM
        if (in->mux_pcm) {
            mux_pcm_close(in->mux_pcm);
            in->mux_pcm = NULL;
        }
#endif

        if (in->pcm) {
            pcm_close(in->pcm);
            in->pcm = NULL;
        }

        if(in->is_voip) {
            if(adev->voip_state) {
                adev->voip_state &= (~VOIP_CAPTURE_STREAM);
                if(!adev->voip_state)
                close_voip_codec_pcm(adev);
            }
            in->is_voip = false;
        }
        if(in->is_bt_sco) {
            /* we will stop writing zero data to bt_sco_card. */
            in->is_bt_sco = false;
            adev->bt_sco_state &= (~BT_SCO_UPLINK_IS_STARTED);
            ALOGE("bt sco : %s before", __func__);
            audio_bt_sco_duplicate_start(adev, false);
            ALOGE("bt sco : %s after", __func__);
        }
        adev->active_input = 0;
        if ((adev->mode != AUDIO_MODE_IN_CALL)
#ifdef VOIP_DSP_PROCESS
            &&(adev->voip_start ==0)
#endif
        )
        {
            adev->in_devices &= ~AUDIO_DEVICE_IN_ALL;
            select_devices_signal(adev);
        }

        if(in->resampler){
            in_deinit_resampler( in);
        }

        if (in->active_rec_proc) {
            AUDPROC_DeInitDp();
            in->active_rec_proc = 0;
        }
        in->standby = 1;
    }
    ALOGD("do_input_standby out");
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;
    int status;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;
    struct tiny_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;

    BLUE_TRACE("[in_set_parameters], kvpairs=%s in_devices:0x%x mode:%d ", kvpairs,adev->in_devices,adev->mode);
    if (adev->call_start) {
        ALOGI("Voice call, no need care.");
        return 0;
    }
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
	     adev->input_source =val;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        if (in->device != val) {
            in->device = val;
            adev->in_devices &= ~AUDIO_DEVICE_IN_ALL;
            adev->in_devices |= in->device;
            select_devices_signal(adev);
        }
    }

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    return ret;
}

static char * in_get_parameters(const struct audio_stream *stream,
        const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
        struct resampler_buffer* buffer)
{
	void * buffer1 = NULL;
    struct tiny_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = container_of(buffer_provider, struct tiny_stream_in, buf_provider);

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->frames_in == 0) {
#ifdef AUDIO_MUX_PCM
        if(in->mux_pcm){
            in->read_status = mux_pcm_read(in->pcm,
                    (void*)in->buffer,
                    in->config.period_size *
                    audio_stream_frame_size((const struct audio_stream *)(&in->stream.common)));
        }
        else{
            in->read_status = pcm_read(in->pcm,
                    (void*)in->buffer,
                    in->config.period_size *
                    audio_stream_frame_size((const struct audio_stream *)(&in->stream.common)));
        }
#else
#if 1
        in->read_status = pcm_read(in->pcm,
                (void*)in->buffer,
                in->config.period_size *
                audio_stream_frame_size((const struct audio_stream *)(&in->stream.common)));
       buffer1 = (void*)in->buffer;

#ifdef AUDIO_DUMP_EX
    dump_info.buf = in->buffer;
    dump_info.buf_len = in->config.period_size * audio_stream_frame_size(&in->stream.common);
    dump_info.dump_switch_info =  DUMP_RECORD_HWL_AFTER_VBC;
    dump_data(dump_info);
#endif
#else
	    in->read_status = 0;
	    usleep(20000);
#endif

#endif

        if (in->read_status != 0) {
            if(in->pcm) {
                ALOGE("get_next_buffer() pcm_read sattus=%d, error: %s",
                        in->read_status, pcm_get_error(in->pcm));
            }
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->frames_in = in->config.period_size;
    }
    buffer->frame_count = (buffer->frame_count > in->frames_in) ?
        in->frames_in : buffer->frame_count;
    buffer->i16 = in->buffer + (in->config.period_size - in->frames_in) *
        in->config.channels;

    return in->read_status;
}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
        struct resampler_buffer* buffer)
{
    struct tiny_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = container_of(buffer_provider, struct tiny_stream_in, buf_provider);

    in->frames_in -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct tiny_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    //BLUE_TRACE("in voip3:read_frames, frames=%d", frames);
    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        //BLUE_TRACE("in voip4:frames_wr=%d, frames=%d, frames_rd=%d", frames_wr, frames, frames_rd);
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                        frames_wr * audio_stream_frame_size((const struct audio_stream *)(&in->stream.common))),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                { raw : NULL, },
frame_count : frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                        frames_wr * audio_stream_frame_size((const struct audio_stream *)(&in->stream.common)),
                        buf.raw,
                        buf.frame_count * audio_stream_frame_size((const struct audio_stream *)(&in->stream.common)));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

static bool in_bypass_data(struct tiny_stream_in *in,uint32_t frame_size, uint32_t sample_rate, void* buffer, size_t bytes)
{
    struct tiny_audio_device *adev = in->dev;
    /*
       1. If cp stopped calling and in-devices is AUDIO_DEVICE_IN_VOICE_CALL, it means that cp already stopped vt call, we should write
       0 data, otherwise, AudioRecord will obtainbuffer timeout.
       */
    if ((!adev->call_start) && ((in->device == AUDIO_DEVICE_IN_VOICE_CALL))
        || adev->call_prestop){
        ALOGW("in_bypass_data write 0 data call_start(%d) mode(%d)  in_device(0x%x) call_connected(%d) call_prestop(%d) ",adev->call_start,adev->mode,in->device,adev->call_connected,adev->call_prestop);
        memset(buffer,0,bytes);
        pthread_mutex_unlock(&adev->lock);
        pthread_mutex_unlock(&in->lock);
        usleep((int64_t)bytes * 1000000 / frame_size / sample_rate);
        return true;
    }else{
        return false;
    }
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
        size_t bytes)
{
    int ret = 0;
    static int pop_count = 0;
    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;
    struct tiny_audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_frame_size((const struct audio_stream *)(&stream->common));

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);

    ALOGD("into in_read1: start: in->is_voip is %d, voip_state is %d in_devices is %x",in->is_voip,adev->voip_state,in->device);
    if(in_bypass_data(in,audio_stream_frame_size((const struct audio_stream *)(&stream->common)),in_get_sample_rate(&stream->common),buffer,bytes)){
        return bytes;
    }

#ifdef VOIP_DSP_PROCESS
    if(((adev->voip_start == 1) && (!((in->device & ~ AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_ALL_SCO)))
            &&(!adev->call_start))
    {
        if(!in->is_voip ) {
            ALOGD(": in_read sco start  and do standby");
            do_input_standby(in);
            adev->voip_state |= VOIP_CAPTURE_STREAM;
            force_standby_for_voip(adev);
            in->is_voip=true;
        }
    }
    else{
        if(in->is_voip){
            ALOGD(": in_read sco stop  and do standby");
            do_input_standby( in);
        }
    }
#endif
    if((adev->mode  != AUDIO_MODE_IN_CALL) &&
            ((in->device & ~ AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET))
    {
        if(!in->is_bt_sco) {
            ALOGD("bt_sco:in_read start and do standby");
            do_input_standby(in);
            in->is_bt_sco=true;
        }
    }
    else{
        if(in->is_bt_sco){
            ALOGD("bt_sco:in_read stop and do standby");
            do_input_standby( in);
        }
    }

    if((!in->is_voip) && adev->voip_state){
        usleep(100000);
        memset(buffer,0,bytes);
        pthread_mutex_unlock(&in->lock);
        pthread_mutex_unlock(&adev->lock);
       return bytes;
    }

    if (in->standby) {
        in->standby = 0;
        ret = start_input_stream(in);
    }
    pthread_mutex_unlock(&adev->lock);


    if (ret < 0)
        goto exit;

#ifdef AUDIO_MUX_PCM
    if(((adev->call_connected) &&(!in->mux_pcm))
            ||((!adev->call_connected) &&(in->mux_pcm))) {
        usleep(20000);
        ALOGW("in_read no data read adev->call_connected is %d,in->mux_pcm is %x",adev->call_connected,(unsigned int)in->mux_pcm);
        pthread_mutex_unlock(&in->lock);
        return bytes;
    }
#endif

    /*BLUE_TRACE("in_read start.num_preprocessors=%d, resampler=%d",
      in->num_preprocessors, in->resampler);*/
    if (in->resampler != NULL) {
            ret = read_frames(in, buffer, frames_rq);
    } else {
#ifdef  AUDIO_MUX_PCM
        if(in->mux_pcm){
            ret = mux_pcm_read(in->mux_pcm, buffer, bytes);
        }
        else
            ret = pcm_read(in->pcm, buffer, bytes);
#else
        ret = pcm_read(in->pcm, buffer, bytes);
#endif
    }

    if (ret == 0 && in->active_rec_proc && in->proc_buf)
            aud_rec_do_process(buffer, bytes,in->proc_buf,in->proc_buf_size);

    if(in->pop_mute) {
        memset(buffer, 0, bytes);
        // mute 240ms for pop
        if(++pop_count >= in->pop_mute_count) {
            pop_count = 0;
            in->pop_mute = false;
        }
    }

#ifdef AUDIO_DUMP_EX
    dump_info.buf = buffer;
    dump_info.buf_len = bytes;
    dump_info.dump_switch_info =  DUMP_RECORD_HWL_AFTER_EXPRESS;
    dump_data(dump_info);
#endif

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);
    //BLUE_TRACE("in_read final OK, bytes=%d", bytes);

exit:
    if (ret < 0) {
        if(in->pcm) {
            ALOGW("in_read,warning: ret=%d, (%s)", ret, pcm_get_error(in->pcm));
        }
        do_input_standby(in);
    }
    pthread_mutex_unlock(&in->lock);
    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
        audio_io_handle_t handle,
        audio_devices_t devices,
        audio_output_flags_t flags,
        struct audio_config *config,
        struct audio_stream_out **stream_out)
{
    struct tiny_audio_device *ladev = (struct tiny_audio_device *)dev;
    struct tiny_stream_out *out;
    int ret;

    BLUE_TRACE("adev_open_output_stream, devices=%d", devices);
    out = (struct tiny_stream_out *)calloc(1, sizeof(struct tiny_stream_out));
    if (!out){
        ALOGE("adev_open_output_stream calloc fail, size:%d", sizeof(struct tiny_stream_out));
        return -ENOMEM;
    }
    memset(out, 0, sizeof(struct tiny_stream_out));
    ret = create_resampler(DEFAULT_OUT_SAMPLING_RATE,
            MM_FULL_POWER_SAMPLING_RATE,
            2,
            RESAMPLER_QUALITY_DEFAULT,
            NULL,
            &out->resampler);
    if (ret != 0)
    {
        ALOGE("adev_open_output_stream create_resampler fail, ret:%d", ret);
        goto err_open;
    }
    out->buffer = malloc(RESAMPLER_BUFFER_SIZE); /* todo: allow for reallocing */
    if (NULL==out->buffer)
    {
        ALOGE("adev_open_output_stream out->buffer alloc fail, size:%d", RESAMPLER_BUFFER_SIZE);
        goto err_open;
    }
    else
    {
        memset(out->buffer, 0, RESAMPLER_BUFFER_SIZE);
    }

    out->stream.common.get_sample_rate = out_get_sample_rate;
    out->stream.common.set_sample_rate = out_set_sample_rate;
    out->stream.common.get_buffer_size = out_get_buffer_size;
    out->stream.common.get_channels = out_get_channels;
    out->stream.common.get_format = out_get_format;
    out->stream.common.set_format = out_set_format;
    out->stream.common.standby = out_standby;
    out->stream.common.dump = out_dump;
    out->stream.common.set_parameters = out_set_parameters;
    out->stream.common.get_parameters = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect = out_remove_audio_effect;
    out->stream.get_latency = out_get_latency;
    out->stream.set_volume = out_set_volume;
    out->stream.write = out_write;
    out->stream.get_render_position = out_get_render_position;

    if(flags&AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
	out->config = pcm_config_mm;
    }
    else {
	out->config = pcm_config_mm_fast;
    }

    out->dev = ladev;
    out->standby = 1;
    out->devices = devices;
    out->flags = flags;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    BLUE_TRACE("Successful, adev_open_output_stream");
    *stream_out = &out->stream;
    return 0;

err_open:
    BLUE_TRACE("Error adev_open_output_stream");
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
        struct audio_stream_out *stream)
{
    struct tiny_stream_out *out = (struct tiny_stream_out *)stream;
    BLUE_TRACE("adev_close_output_stream");
    out_standby(&stream->common);
    if (out->buffer)
        free(out->buffer);
    if (out->resampler){
        release_resampler(out->resampler);
        out->resampler = NULL;
    }

    if(out->buffer_vplayback)
        free(out->buffer_vplayback);
    if(out->resampler_vplayback)
        release_resampler(out->resampler_vplayback);
    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;
    int val=0;
    uint32_t gain = 0;

    BLUE_TRACE("adev_set_parameters, kvpairs : %s", kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bluetooth_nrec = true;
        else
            adev->bluetooth_nrec = false;
    }

    /* specify the sampling rate supported by bt headset
     * 8KHz for BT headset NB, as default.
     * 16KHz for BT headset WB.
     * */
    ret = str_parms_get_str(parms, "bt_samplerate", value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        adev->bluetooth_type = val;
    }

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->low_power = false;
        else
            adev->low_power = true;
    }

 #if VOIP_DSP_PROCESS
    ret = str_parms_get_str(parms, "sprd_voip_start", value, sizeof(value));
    if (ret > 0) {
        if(strcmp(value, "true") == 0) {
            ALOGI("%s, voip turn on by output", __FUNCTION__);
            adev->voip_start = 1;
        } else if (strcmp(value, "false") == 0) {
            ALOGI("%s, voip turn off by output", __FUNCTION__);
            adev->voip_start = 0;
        }
    }
#endif
    ret = str_parms_get_str(parms, "FM_Volume", value, sizeof(value));
    if (ret >= 0) {
        bool checkvalid = false;
        val = atoi(value);
        if(0 < val && val <= FM_VOLUME_MAX)
        {
            checkvalid = true;
        }
        if(checkvalid && val != adev->fm_volume)
        {
            adev->fm_volume = val;
            gain |=fm_volume_tbl[val-1];
            gain |=fm_volume_tbl[val-1]<<16;
            SetAudio_gain_fmradio(adev,gain);
        }
        ALOGE("adev_set_parameters fm volume :%d",val);
    }
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        ALOGI("get adev lock adev->devices is %x,%x",adev->out_devices,adev->out_devices&AUDIO_DEVICE_OUT_ALL);
        if(((adev->mode == AUDIO_MODE_IN_CALL) && (adev->call_connected) && ((adev->out_devices & AUDIO_DEVICE_OUT_ALL) != val))||adev->voip_state){
            if(val&AUDIO_DEVICE_OUT_ALL) {
                ALOGE("adev set device in val is %x",val);
                adev->out_devices &= ~AUDIO_DEVICE_OUT_ALL;
                adev->out_devices |= val;
                if(adev->active_output)
                    adev->active_output->devices = val;

                if(adev->out_devices & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_ALL_SCO)) {
                    if(adev->cp_type == CP_TG)
                        i2s_pin_mux_sel(adev,1);
                    else if(adev->cp_type == CP_W)
                        i2s_pin_mux_sel(adev,0);
                }

                pthread_mutex_unlock(&adev->lock);
                ret = at_cmd_route(adev);  //send at command to cp
                if (ret < 0) {
                    ALOGE("out_set_parameters at_cmd_route error(%d) ",ret);
                }
            }
            else {
                pthread_mutex_unlock(&adev->lock);
            }

        } else if(((val & (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) != 0) && ((adev->out_devices & AUDIO_DEVICE_OUT_ALL) != val)) {
            adev->out_devices &= ~AUDIO_DEVICE_OUT_ALL;
            adev->out_devices |= val;
            ALOGW("adev_set_parameters want to set devices:0x%x mode:%d call_start:%d ",adev->out_devices,adev->mode,adev->call_start);
            select_devices_signal(adev);
            pthread_mutex_unlock(&adev->lock);
        }
        else {
            pthread_mutex_unlock(&adev->lock);
        }
    }

    str_parms_destroy(parms);
    return ret;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
        const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    ALOGW("adev_init_check");
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;
    ALOGW("[adev_set_voice_volume], devices:0x%x ",adev->out_devices);
    if (adev->out_devices & AUDIO_DEVICE_OUT_ALL_FM) {
        return 0;
    }
    BLUE_TRACE("adev_set_voice_volume in...volume:%f mode:%d call_start:%d ",volume,adev->mode,adev->call_start);
    adev->voice_volume = volume;
    /*Send at command to cp side*/
    at_cmd_volume(volume,adev->mode);
    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;
    ALOGW("adev_set_master_volume in...devices:0x%x ,volume:%f ",adev->out_devices,volume);
    SetAudio_gain_route(adev,1);
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;
    bool need_unmute = false;
    BLUE_TRACE("adev_set_mode, mode=%d", mode);
    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        need_unmute = true;
        select_mode(adev);
    }else{
        BLUE_TRACE("adev_set_mode,the same mode(%d)",mode);
    }
    pthread_mutex_unlock(&adev->lock);
    //there is lock in function adev_set_master_mute,so we can`t call this funciont under lock
    if(need_unmute)
        adev_set_master_mute(dev, false);

    return 0;
}

static int set_codec_mute(struct tiny_audio_device *adev)
{
    ALOGD("set_codec_mute(%d)", adev->master_mute);
    if (adev->private_ctl.speaker_mute)
        mixer_ctl_set_value(adev->private_ctl.speaker_mute, 0, adev->master_mute);

    if (adev->private_ctl.speaker2_mute)
        mixer_ctl_set_value(adev->private_ctl.speaker2_mute, 0, adev->master_mute);

    if (adev->private_ctl.earpiece_mute)
        mixer_ctl_set_value(adev->private_ctl.earpiece_mute, 0, adev->master_mute);

    if (adev->private_ctl.headphone_mute)
        mixer_ctl_set_value(adev->private_ctl.headphone_mute, 0, adev->master_mute);

    return 0;
}

static void set_codec_mute_forFM(struct tiny_audio_device *adev ,bool mute)
{
    ALOGV("%s in ",__func__);
    if (adev->private_ctl.speaker_mute)
        mixer_ctl_set_value(adev->private_ctl.speaker_mute, 0, mute);

    if (adev->private_ctl.speaker2_mute)
        mixer_ctl_set_value(adev->private_ctl.speaker2_mute, 0, mute);

    if (adev->private_ctl.earpiece_mute)
        mixer_ctl_set_value(adev->private_ctl.earpiece_mute, 0, mute);

    if (adev->private_ctl.headphone_mute)
        mixer_ctl_set_value(adev->private_ctl.headphone_mute, 0, mute);

    return;

}

static int adev_set_master_mute(struct audio_hw_device *dev, bool mute)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;

    //ALOGD("%s, mute=%d, master_mute=%d", __func__, mute, adev->master_mute);
    if (adev->master_mute == mute)
        return 0;
    if (!adev->master_mute && adev->mode == AUDIO_MODE_IN_CALL)
	return 0;
    pthread_mutex_lock(&adev->lock);
    adev->master_mute = mute;
    if(mute && adev->pcm_fm_dl != NULL){
        pthread_mutex_unlock(&adev->lock);
        ALOGV("FM is open so we can not set master mute");
        return 0;
    }
    pthread_mutex_unlock(&adev->lock);
    ALOGD("adev_set_master_mute(%d)", adev->master_mute);
    select_devices_signal(adev);

    return 0;
}

static int adev_get_master_mute(const struct audio_hw_device *dev, bool *mute)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;

    *mute = adev->master_mute;

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
        const struct audio_config *config)
{
    size_t size;
    int channel_count = popcount(config->channel_mask);

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate, config->format, channel_count);
}

static int in_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int in_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int adev_open_input_stream(struct audio_hw_device *dev,
        audio_io_handle_t handle,
        audio_devices_t devices,
        struct audio_config *config,
        struct audio_stream_in **stream_in)

{
    struct tiny_audio_device *ladev = (struct tiny_audio_device *)dev;
    struct tiny_stream_in *in;
    int ret = 0;
    int channel_count = popcount(config->channel_mask);

    BLUE_TRACE("[TH], adev_open_input_stream,devices=0x%x,sample_rate=%d, channel_count=%d",
            devices, config->sample_rate, channel_count);

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

    in = (struct tiny_stream_in *)calloc(1, sizeof(struct tiny_stream_in));
    if (!in)
    {
        ALOGE("adev_open_input_stream alloc fail, size:%d", sizeof(struct tiny_stream_in));
        return -ENOMEM;
    }
    memset(in, 0, sizeof(struct tiny_stream_in));
    in->stream.common.get_sample_rate = in_get_sample_rate;
    in->stream.common.set_sample_rate = in_set_sample_rate;
    in->stream.common.get_buffer_size = in_get_buffer_size;
    in->stream.common.get_channels = in_get_channels;
    in->stream.common.get_format = in_get_format;
    in->stream.common.set_format = in_set_format;
    in->stream.common.standby = in_standby;
    in->stream.common.dump = in_dump;
    in->stream.common.set_parameters = in_set_parameters;
    in->stream.common.get_parameters = in_get_parameters;
    in->stream.common.add_audio_effect = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;

    in->requested_rate = config->sample_rate;



    if (ladev->call_start)
        memcpy(&in->config, &pcm_config_record_incall, sizeof(pcm_config_record_incall));
    else
        memcpy(&in->config, &pcm_config_mm_ul, sizeof(pcm_config_mm_ul));
    in->config.channels = channel_count;
    in->requested_channels = channel_count;

    ladev->requested_channel_cnt = channel_count;

    {
	int size = 0;
	size = in->config.period_size ;
	size = ((size + 15) / 16) * 16;
	in->proc_buf_size = size * 2 * sizeof(short);
	in->proc_buf = malloc(in->proc_buf_size);
	if(!in->proc_buf) {
	    goto err;
	}
    }
    if(in->requested_rate) {
	in->pop_mute_count = RECORD_POP_MIN_TIME/((in->config.period_size*1000)/in->requested_rate);
    }
    in->dev = ladev;
    in->standby = 1;
    in->device = devices;
    in->pop_mute = true;

    *stream_in = &in->stream;
    BLUE_TRACE("Successfully, adev_open_input_stream.");
    return 0;

err:
    BLUE_TRACE("Failed(%d), adev_open_input_stream.", ret);
    if (in->buffer) {
        free(in->buffer);
        in->buffer = NULL;
    }
    if (in->resampler) {
        release_resampler(in->resampler);
           in->resampler = NULL;
    }
    if(in->proc_buf) {
        free(in->proc_buf);
        in->proc_buf = NULL;
    }

    free(in);
    *stream_in = NULL;
    return ret;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
        struct audio_stream_in *stream)
{
    struct tiny_stream_in *in = (struct tiny_stream_in *)stream;

    in_standby(&stream->common);

    if (in->resampler) {
        free(in->buffer);
        release_resampler(in->resampler);
    }
    if (in->proc_buf) {
        free(in->proc_buf);
        in->proc_buf = NULL;
        in->proc_buf_size = 0;
    }
    if (in->ref_buf)
        free(in->ref_buf);

    free(stream);
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    unsigned int i, j;
    struct tiny_audio_device *adev = (struct tiny_audio_device *)device;

    audio_bt_sco_thread_destory(adev);
    /* free audio PGA */
    audio_pga_free(adev->pga);
    ALOGD("voip:enter into vbc_ctrl_close");
    vbc_ctrl_close();
	ALOGD("voip:get out vbc_ctrl_close");
    //Need to free mixer configs here.
    for (i=0; i < adev->num_dev_cfgs; i++) {
        for (j=0; j < adev->dev_cfgs->on_len; j++) {
            free(adev->dev_cfgs[i].on[j].ctl_name);
            //Is there a string of strval?
        };
        free(adev->dev_cfgs[i].on);
        for (j=0; j < adev->dev_cfgs->off_len; j++) {
            free(adev->dev_cfgs[i].off[j].ctl_name);
        };
        free(adev->dev_cfgs[i].off);
    };
    free(adev->dev_cfgs);

    free(adev->cp->vbc_ctrl_pipe_info);
    free(adev->cp);

    adev_free_audmode();
    mixer_close(adev->mixer);
    stream_routing_manager_close(adev);
    voice_command_manager_close(adev);
    free(device);
    return 0;
}

static uint32_t adev_get_supported_devices(const struct audio_hw_device *dev)
{
    return (/* OUT */
            AUDIO_DEVICE_OUT_EARPIECE |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_AUX_DIGITAL |
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_ALL_SCO |
            AUDIO_DEVICE_OUT_ALL_FM |
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_COMMUNICATION |
            AUDIO_DEVICE_IN_AMBIENT |
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_AUX_DIGITAL |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET |
            AUDIO_DEVICE_IN_ALL_SCO|
            AUDIO_DEVICE_IN_VOICE_CALL |
            AUDIO_DEVICE_IN_DEFAULT);
}

/* parse the private field of xml config file. */
static void adev_config_parse_private(struct config_parse_state *s, const XML_Char *name)
{
    if (s && name) {
        if (strcmp(s->private_name, PRIVATE_VBC_CONTROL) == 0) {
            s->adev->private_ctl.vbc_switch =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_switch);
        } else if (strcmp(s->private_name, PRIVATE_VBC_EQ_SWITCH) == 0) {
            s->adev->private_ctl.vbc_eq_switch =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_eq_switch);
        } else if (strcmp(s->private_name, PRIVATE_VBC_EQ_UPDATE) == 0) {
            s->adev->private_ctl.vbc_eq_update =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_eq_update);
        } else if (strcmp(s->private_name, PRIVATE_VBC_EQ_PROFILE) == 0) {
            s->adev->private_ctl.vbc_eq_profile_select =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_eq_profile_select);
        } else if (strcmp(s->private_name, PRIVATE_MIC_BIAS) == 0) {
            s->adev->private_ctl.mic_bias_switch =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.mic_bias_switch);
        } else if (strcmp(s->private_name, PRIVATE_INTERNAL_PA) == 0) {
            s->adev->private_ctl.internal_pa =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.internal_pa);
        }
        else if (strcmp(s->private_name, PRIVATE_VBC_DA_EQ_SWITCH) == 0) {
            s->adev->private_ctl.vbc_da_eq_switch =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_da_eq_switch);
        }
        else if (strcmp(s->private_name, PRIVATE_VBC_AD01_EQ_SWITCH) == 0) {
            s->adev->private_ctl.vbc_ad01_eq_switch =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_ad01_eq_switch);
        }
        else if (strcmp(s->private_name, PRIVATE_VBC_AD23_EQ_SWITCH) == 0) {
            s->adev->private_ctl.vbc_ad23_eq_switch =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_ad23_eq_switch);
        }
        else if (strcmp(s->private_name, PRIVATE_VBC_DA_EQ_PROFILE) == 0) {
            s->adev->private_ctl.vbc_da_eq_profile_select =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_da_eq_switch);
        }
        else if (strcmp(s->private_name, PRIVATE_VBC_AD01_EQ_PROFILE) == 0) {
            s->adev->private_ctl.vbc_ad01_eq_profile_select =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_ad01_eq_switch);
        }
        else if (strcmp(s->private_name, PRIVATE_VBC_AD23_EQ_PROFILE) == 0) {
            s->adev->private_ctl.vbc_ad23_eq_profile_select =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.vbc_ad23_eq_switch);
        }
        else if (strcmp(s->private_name, PRIVATE_SPEAKER_MUTE) == 0) {
            s->adev->private_ctl.speaker_mute =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.speaker_mute);
        }
        else if (strcmp(s->private_name, PRIVATE_SPEAKER2_MUTE) == 0) {
            s->adev->private_ctl.speaker2_mute =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.speaker2_mute);
        }
        else if (strcmp(s->private_name, PRIVATE_EARPIECE_MUTE) == 0) {
            s->adev->private_ctl.earpiece_mute =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.earpiece_mute);
        }
        else if (strcmp(s->private_name, PRIVATE_HEADPHONE_MUTE) == 0) {
            s->adev->private_ctl.headphone_mute =
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.headphone_mute);
        }
        else if (strcmp(s->private_name, PRIVATE_AUD_LOOP_VBC) == 0) {
            s->adev->private_ctl.fm_loop_vbc=
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.fm_loop_vbc);
        }
        else if (strcmp(s->private_name, PRIVATE_AUD1_LOOP_VBC) == 0) {
            s->adev->private_ctl.ad1_fm_loop_vbc=
                mixer_get_ctl_by_name(s->adev->mixer, name);
            CTL_TRACE(s->adev->private_ctl.ad1_fm_loop_vbc);
        }
    }
}

static void adev_config_start(void *data, const XML_Char *elem,
        const XML_Char **attr)
{
    struct config_parse_state *s = data;
    struct tiny_dev_cfg *dev_cfg;
    const XML_Char *name = NULL;
    const XML_Char *val = NULL;
    unsigned int i, j;
    char value[PROPERTY_VALUE_MAX];
    unsigned int dev_num = 0;

    if (property_get(FM_DIGITAL_SUPPORT_PROPERTY, value, "0") && strcmp(value, "1") == 0)
    {
        dev_names = dev_names_digitalfm;
        dev_num = sizeof(dev_names_digitalfm) / sizeof(dev_names_digitalfm[0]);
    }
    else
    {
        dev_names = dev_names_linein;
        dev_num = sizeof(dev_names_linein) / sizeof(dev_names_linein[0]);
    }

    /* default if not set it 0 */
    for (i = 0; attr[i]; i += 2) {
        if (strcmp(attr[i], "name") == 0)
            name = attr[i + 1];

        if (strcmp(attr[i], "val") == 0)
            val = attr[i + 1];
    }

    if (!name) {
        ALOGE("unnamed entry %s, %d", elem, i);
        return;
    }

    if (strcmp(elem, "device") == 0) {
        for (i = 0; i < dev_num; i++) {
            if (strcmp((dev_names+i)->name, name) == 0) {
                ALOGI("Allocating device %s\n", name);
                dev_cfg = realloc(s->adev->dev_cfgs,
                        (s->adev->num_dev_cfgs + 1)
                        * sizeof(*dev_cfg));
                if (!dev_cfg) {
                    ALOGE("Unable to allocate dev_cfg\n");
                    return;
                }

                s->dev = &dev_cfg[s->adev->num_dev_cfgs];
                memset(s->dev, 0, sizeof(*s->dev));
                s->dev->mask = (dev_names+i)->mask;

                s->adev->dev_cfgs = dev_cfg;
                s->adev->num_dev_cfgs++;
            }
        }

    } else if (strcmp(elem, "path") == 0) {
        if (s->path_len)
            ALOGW("Nested paths\n");

        /* If this a path for a device it must have a role */
        if (s->dev) {
            /* Need to refactor a bit... */
            if (strcmp(name, "on") == 0) {
                s->on = true;
            } else if (strcmp(name, "off") == 0) {
                s->on = false;
            } else {
                ALOGW("Unknown path name %s\n", name);
            }
        }

    } else if (strcmp(elem, "ctl") == 0) {
        struct route_setting *r;

        if (!name) {
            ALOGE("Unnamed control\n");
            return;
        }

        if (!val) {
            ALOGE("No value specified for %s\n", name);
            return;
        }

        ALOGI("Parsing control %s => %s\n", name, val);

        r = realloc(s->path, sizeof(*r) * (s->path_len + 1));
        if (!r) {
            ALOGE("Out of memory handling %s => %s\n", name, val);
            return;
        }

        r[s->path_len].ctl_name = strdup(name);
        r[s->path_len].strval = NULL;

        /* This can be fooled but it'll do */
        r[s->path_len].intval = atoi(val);
        if (!r[s->path_len].intval && strcmp(val, "0") != 0)
            r[s->path_len].strval = strdup(val);

        s->path = r;
        s->path_len++;
        ALOGI("s->path_len=%d", s->path_len);
    }
    else if (strcmp(elem, "private") == 0) {
        memset(s->private_name, 0, PRIVATE_NAME_LEN);
        memcpy(s->private_name, name, strlen(name));
    }
    else if (strcmp(elem, "func") == 0) {
        adev_config_parse_private(s, name);
    }
}

static void adev_config_end(void *data, const XML_Char *name)
{
    struct config_parse_state *s = data;
    unsigned int i;

    if (strcmp(name, "path") == 0) {
        if (!s->path_len)
            ALOGW("Empty path\n");

        if (!s->dev) {
            ALOGI("Applying %d element default route\n", s->path_len);

            set_route_by_array(s->adev->mixer, s->path, s->path_len);

            for (i = 0; i < s->path_len; i++) {
                free(s->path[i].ctl_name);
                free(s->path[i].strval);
            }

            free(s->path);

            /* Refactor! */
        } else if (s->on) {
            ALOGI("%d element on sequence\n", s->path_len);
            s->dev->on = s->path;
            s->dev->on_len = s->path_len;

        } else {
            ALOGI("%d element off sequence\n", s->path_len);

            /* Apply it, we'll reenable anything that's wanted later */
            set_route_by_array(s->adev->mixer, s->path, s->path_len);

            s->dev->off = s->path;
            s->dev->off_len = s->path_len;
        }

        s->path_len = 0;
        s->path = NULL;

    } else if (strcmp(name, "device") == 0) {
        s->dev = NULL;
    }
}

static int adev_config_parse(struct tiny_audio_device *adev)
{
    struct config_parse_state s;
    FILE *f;
    XML_Parser p;
    char property[PROPERTY_VALUE_MAX];
    char file[80];
    int ret = 0;
    bool eof = false;
    int len;

    //property_get("ro.product.device", property, "tiny_hw");
    snprintf(file, sizeof(file), "/system/etc/%s", "tiny_hw.xml");

    ALOGV("Reading configuration from %s\n", file);
    f = fopen(file, "r");
    if (!f) {
        ALOGE("Failed to open %s\n", file);
        return -ENODEV;
    }

    p = XML_ParserCreate(NULL);
    if (!p) {
        ALOGE("Failed to create XML parser\n");
        ret = -ENOMEM;
        goto out;
    }

    memset(&s, 0, sizeof(s));
    s.adev = adev;
    XML_SetUserData(p, &s);

    XML_SetElementHandler(p, adev_config_start, adev_config_end);

    while (!eof) {
        len = fread(file, 1, sizeof(file), f);
        if (ferror(f)) {
            ALOGE("I/O error reading config\n");
            ret = -EIO;
            goto out_parser;
        }
        eof = feof(f);

        if (XML_Parse(p, file, len, eof) == XML_STATUS_ERROR) {
            ALOGE("Parse error at line %u:\n%s\n",
                    (unsigned int)XML_GetCurrentLineNumber(p),
                    XML_ErrorString(XML_GetErrorCode(p)));
            ret = -EINVAL;
            goto out_parser;
        }
    }

out_parser:
    XML_ParserFree(p);
out:
    fclose(f);

    return ret;
}

static void aud_vb_effect_start(struct tiny_audio_device *adev)
{
    if (adev)
    {
        if(adev->private_ctl.vbc_eq_switch)
        {
            mixer_ctl_set_value(adev->private_ctl.vbc_eq_switch, 0, 1);
        }
        if(adev->private_ctl.vbc_da_eq_switch)
        {
            mixer_ctl_set_value(adev->private_ctl.vbc_da_eq_switch, 0, 1);
        }
        if(adev->private_ctl.vbc_ad01_eq_switch)
        {
            //mixer_ctl_set_value(adev->private_ctl.vbc_ad01_eq_switch, 0, 1);
        }
        if(adev->private_ctl.vbc_ad23_eq_switch)
        {
            //mixer_ctl_set_value(adev->private_ctl.vbc_ad23_eq_switch, 0, 1);
        }
    }
}

static void aud_vb_effect_stop(struct tiny_audio_device *adev)
{
    if (adev)
    {
        if(adev->private_ctl.vbc_eq_switch)
        {
            mixer_ctl_set_value(adev->private_ctl.vbc_eq_switch, 0, 0);
        }
        if(adev->private_ctl.vbc_da_eq_switch)
        {
            mixer_ctl_set_value(adev->private_ctl.vbc_da_eq_switch, 0, 0);
        }
        if(adev->private_ctl.vbc_ad01_eq_switch)
        {
            mixer_ctl_set_value(adev->private_ctl.vbc_ad01_eq_switch, 0, 0);
        }
        if(adev->private_ctl.vbc_ad23_eq_switch)
        {
            mixer_ctl_set_value(adev->private_ctl.vbc_ad23_eq_switch, 0, 0);
        }
    }
}

/* Headset is 0, Handsfree is 3 */
static int get_mode_from_devices(int devices)
{
    int ret = 3;

    if (((devices & ~AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_BUILTIN_MIC)
            ||((devices & ~ AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_BACK_MIC))
        ret = 3;
    else if (((devices & ~AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_WIRED_HEADSET)
            ||((devices & ~AUDIO_DEVICE_BIT_IN) & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET))
        ret = 0;

    return ret;
}
/*
 * Read audproc params from nv and config.
 * return value: TRUE:success, FALSE:failed
 */
static int init_rec_process(int rec_mode, int sample_rate)
{
    int ret0 = 0; //failed
    int ret1 = 0;
    off_t offset = 0;
    AUDIO_TOTAL_T *aud_params_ptr = NULL;
    DP_CONTROL_PARAM_T *ctrl_param_ptr = 0;
    RECORDEQ_CONTROL_PARAM_T *eq_param_ptr = 0;
    unsigned int extendArraySize = 0;

    ALOGW("rec_mode(%d), sample_rate(%d)", rec_mode, sample_rate);

    aud_params_ptr = audio_para_ptr;//(AUDIO_TOTAL_T *)mmap(0, 4*sizeof(AUDIO_TOTAL_T),PROT_READ,MAP_SHARED,audio_fd,0);
    if ( NULL == aud_params_ptr ) {
        ALOGE("mmap failed %s",strerror(errno));
        return 0;
    }
    ctrl_param_ptr = (DP_CONTROL_PARAM_T *)((aud_params_ptr+rec_mode)->audio_enha_eq.externdArray);

    ret0 = AUDPROC_initDp(ctrl_param_ptr, sample_rate);

    //get total items of extend array.
    extendArraySize = sizeof((aud_params_ptr+rec_mode)->audio_enha_eq.externdArray);
    ALOGW("extendArraySize=%d, eq_size=%d, dp_size=%d",
            extendArraySize, sizeof(RECORDEQ_CONTROL_PARAM_T), sizeof(DP_CONTROL_PARAM_T));
    if ((sizeof(RECORDEQ_CONTROL_PARAM_T) + sizeof(DP_CONTROL_PARAM_T)) <= extendArraySize)
    {
        eq_param_ptr =(RECORDEQ_CONTROL_PARAM_T *)&((aud_params_ptr+rec_mode)->audio_enha_eq.externdArray[19]);
        ret1 = AUDPROC_initRecordEq(eq_param_ptr, sample_rate);
    }else{
        ALOGE("Parameters error: No EQ params to init.");
    }

    return (ret0 || ret1);
}

static int aud_rec_do_process(void * buffer,size_t bytes,void * tmp_buffer, size_t tmp_buffer_bytes)
{
    int16_t *temp_buf = NULL;
    size_t read_bytes = bytes;
    unsigned int dest_count = 0;
    temp_buf = (int16_t *)tmp_buffer;
    if (temp_buf && (tmp_buffer_bytes >= 2)) {
        do {
            if(tmp_buffer_bytes <=  bytes) {
                read_bytes = tmp_buffer_bytes;
            }
            else {
                read_bytes = bytes;
            }
            bytes -= read_bytes;
	AUDPROC_ProcessDp((int16 *) buffer, (int16 *) buffer, read_bytes >> 1, temp_buf, temp_buf, &dest_count);
        memcpy(buffer, temp_buf, read_bytes);
        buffer = (uint8_t *) buffer + read_bytes;
        }while(bytes);
    } else {
        ALOGE("temp_buf malloc failed.(len=%d)", (int) read_bytes);
        return -1;
    }
    return 0;
}

static void *stream_routing_thread_entry(void * param)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)param;
    pthread_attr_t attr;
    struct sched_param m_param;
    int newprio=39;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_getschedparam(&attr, &m_param);
    m_param.sched_priority=newprio;
    pthread_attr_setschedparam(&attr, &m_param);

    while(!adev->routing_mgr.is_exit) {
        ALOGI("stream_routing_thread looping now...");
        sem_wait(&adev->routing_mgr.device_switch_sem);
        do_select_devices(adev);
        ALOGI("stream_routing_thread looping done.");
    }
    ALOGW("stream_routing_thread_entry exit!!!");
    return 0;
}

static int stream_routing_manager_create(struct tiny_audio_device *adev)
{
    int ret;

    adev->routing_mgr.is_exit = false;
    /* init semaphore to signal thread */
    ret = sem_init(&adev->routing_mgr.device_switch_sem, 0, 0);
    if (ret) {
        ALOGE("sem_init falied, code is %s", strerror(errno));
        return ret;
    }
    /* create a thread to manager the device routing switch.*/
    ret = pthread_create(&adev->routing_mgr.routing_switch_thread, NULL,
            stream_routing_thread_entry, (void *)adev);
    if (ret) {
        ALOGE("pthread_create falied, code is %s", strerror(errno));
        return ret;
    }

    return ret;
}

static void stream_routing_manager_close(struct tiny_audio_device *adev)
{
    adev->routing_mgr.is_exit = true;
    /* release associated thread resource.*/
    sem_destroy(&adev->routing_mgr.device_switch_sem);
}


static void *voice_command_thread_entry(void * param)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)param;
    pthread_attr_t attr;
    struct sched_param m_param;
    int newprio=39;

    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    pthread_attr_getschedparam(&attr, &m_param);
    m_param.sched_priority=newprio;
    pthread_attr_setschedparam(&attr, &m_param);

    while(!adev->voice_command_mgr.is_exit) {
        ALOGI(" %s looping now...",__func__);
        sem_wait(&adev->voice_command_mgr.device_switch_sem);
        do_voice_command(adev);
        ALOGI(" %s looping done.",__func__);
    }
    ALOGW("%s exit!!!",__func__);
    return 0;
}

static int voice_command_manager_create(struct tiny_audio_device *adev)
{
    int ret;

    adev->voice_command_mgr.is_exit = false;
    /* init semaphore to signal thread */
    ret = sem_init(&adev->voice_command_mgr.device_switch_sem, 0, 0);
    if (ret) {
        ALOGE("sem_init falied, code is %s", strerror(errno));
        return ret;
    }
    adev->at_cmd_vectors = malloc(sizeof(T_AT_CMD));
    if (adev->at_cmd_vectors == NULL) {
        ALOGE("Unable to allocate at_cmd_vectors ");
        return -1;
    }
    memset(adev->at_cmd_vectors,0x00,sizeof(T_AT_CMD));
    /* create a thread to manager the device routing switch.*/
    ret = pthread_create(&adev->voice_command_mgr.routing_switch_thread, NULL,
            voice_command_thread_entry, (void *)adev);
    if (ret) {
        ALOGE("pthread_create falied, code is %s", strerror(errno));
        return ret;
    }

    return ret;
}

static void voice_command_manager_close(struct tiny_audio_device *adev)
{
    adev->voice_command_mgr.is_exit = true;
    if(adev->at_cmd_vectors != NULL)
    {
        free(adev->at_cmd_vectors);
        adev->at_cmd_vectors = NULL;
    }
    /* release associated thread resource.*/
    sem_destroy(&adev->voice_command_mgr.device_switch_sem);
}

static  vbc_ctrl_pipe_para_t *adev_modem_create(audio_modem_t  *modem, const char *num)
{
    vbc_ctrl_pipe_para_t *a;
    if (!atoi((char *)num)) {
        ALOGE("Unnormal modem num!");
        return NULL;
    }

    modem->num = atoi((char *)num);
    /* check if we need to allocate  space for modem profile */
    if(!modem->vbc_ctrl_pipe_info)
    {
        modem->vbc_ctrl_pipe_info = malloc(modem->num *
                sizeof(vbc_ctrl_pipe_para_t));

        if (modem->vbc_ctrl_pipe_info == NULL) {
            ALOGE("Unable to allocate modem profiles");
            return NULL;
        }
        else
        {
            /* initialise the new profile */
            memset((void*)modem->vbc_ctrl_pipe_info,0x00,modem->num *
                    sizeof(vbc_ctrl_pipe_para_t));
        }
    }

	ALOGD("peter: modem num is %d",modem->num);
    /* return the profile just added */
    return modem->vbc_ctrl_pipe_info;
}


static void adev_modem_start_tag(void *data, const XML_Char *tag_name,
        const XML_Char **attr)
{
    struct modem_config_parse_state *state = data;
    audio_modem_t *modem = state->modem_info;
    unsigned int i;
    int value;
    struct mixer_ctl *ctl;
    vbc_ctrl_pipe_para_t item;
    vbc_ctrl_pipe_para_t *vbc_ctrl_pipe_info = NULL;

    /* Look at tags */
    if (strcmp(tag_name, "audio") == 0) {
        if (strcmp(attr[0], "device") == 0) {
            ALOGI("The device name is %s", attr[1]);
        } else {
            ALOGE("Unnamed audio!");
        }
    }
    else if (strcmp(tag_name, "modem") == 0) {
        /* Obtain the modem num */
        if (strcmp(attr[0], "num") == 0) {
            ALOGD("The modem num is '%s'", attr[1]);
            state->vbc_ctrl_pipe_info = adev_modem_create(modem, attr[1]);
        } else {
            ALOGE("no modem num!");
        }
    }
    else if (strcmp(tag_name, "cp") == 0) {
        if (state->vbc_ctrl_pipe_info) {
            /* Obtain the modem name  \pipe\vbc   filed */
            if (strcmp(attr[0], "name") != 0) {
                ALOGE("Unnamed modem!");
                goto attr_err;
            }
            if (strcmp(attr[2], "pipe") != 0) {
                ALOGE("'%s' No pipe filed!", attr[0]);
                goto attr_err;
            }
            if (strcmp(attr[4], "vbchannel") != 0) {
                ALOGE("'%s' No vbc filed!", attr[0]);
                goto attr_err;
            }
            ALOGD("cp name is '%s', pipe is '%s',vbc is '%s'", attr[1], attr[3],attr[5]);
            if(strcmp(attr[1], "w") == 0)
            {
                state->vbc_ctrl_pipe_info->cp_type = CP_W;
            }
            else if(strcmp(attr[1], "t") == 0)
            {
                state->vbc_ctrl_pipe_info->cp_type = CP_TG;
            }
            memcpy((void*)state->vbc_ctrl_pipe_info->s_vbc_ctrl_pipe_name,(void*)attr[3],strlen((char *)attr[3]));
            state->vbc_ctrl_pipe_info->channel_id = atoi((char *)attr[5]);
            state->vbc_ctrl_pipe_info++;

        } else {
            ALOGE("error profile!");
        }
    }
    else if (strcmp(tag_name, "i2s_for_btcall") == 0)
    {
        if (strcmp(attr[0], "index") == 0) {
            ALOGD("The iis_for_btcall index is '%s'", attr[1]);
            modem->i2s_bt.index = atoi((char *)attr[1]);
        } else {
            ALOGE("no iis_ctl index for bt call!");
        }

        if (strcmp(attr[2], "switch") == 0) {
            ALOGD("The iis_for_btcall switch is '%s'", attr[3]);
            if(strcmp(attr[3],"1") == 0)
                modem->i2s_bt.is_switch = true;
            else if(strcmp(attr[3],"0") == 0)
                modem->i2s_bt.is_switch = false;
        } else {
            ALOGE("no iis_ctl switch for bt call!");
        }
        if (strcmp(attr[4], "dst") == 0) {
            ALOGD("The iis_for_btcall dst  is '%s'", attr[5]);
            if (strcmp(attr[5], "internal") == 0)
                modem->i2s_bt.is_ext = 0;
            else if (strcmp(attr[5], "external") == 0)
                modem->i2s_bt.is_ext = 1;
        } else {
            ALOGE("no dst path for bt call!");
        }

        if (strcmp(attr[6], "cp0_ctl_file") == 0) {
            if((strlen(attr[7]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_bt.fd_sys_cp0_path , attr[7], strlen(attr[7])+1);
                ALOGE(" cp0_ctl_file is %s",modem->i2s_bt.fd_sys_cp0_path);
                modem->i2s_bt.fd_sys_cp0 = open(modem->i2s_bt.fd_sys_cp0_path,O_RDWR | O_SYNC);
                if(modem->i2s_bt.fd_sys_cp0 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_cp0_ctl fd,errno is %d",errno);
                }
            }
        }
        if (strcmp(attr[8], "cp1_ctl_file") == 0) {
            if((strlen(attr[9]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_bt.fd_sys_cp1_path , attr[9], strlen(attr[9])+1);
                ALOGE(" cp1_ctl_file is %s",modem->i2s_bt.fd_sys_cp1_path);
                modem->i2s_bt.fd_sys_cp1 = open(modem->i2s_bt.fd_sys_cp1_path,O_RDWR | O_SYNC);
                if(modem->i2s_bt.fd_sys_cp1 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_cp1 ctl fd,errno is %d",errno);
                }
            }
        }

        if (strcmp(attr[10], "cp2_ctl_file") == 0) {
            if((strlen(attr[11]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_bt.fd_sys_cp2_path , attr[11], strlen(attr[11])+1);
                ALOGE(" cp1_ct2_file is %s",modem->i2s_bt.fd_sys_cp2_path);
                modem->i2s_bt.fd_sys_cp2 = open(modem->i2s_bt.fd_sys_cp2_path,O_RDWR | O_SYNC);
                if(modem->i2s_bt.fd_sys_cp2 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_cp2 ctl fd,errno is %d",errno);
                }
            }
        }

        if (strcmp(attr[12], "ap_ctl_file") == 0) {
            if((strlen(attr[13]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_bt.fd_sys_ap_path , attr[13], strlen(attr[13])+1);
                ALOGE(" ap_ctl_file is %s",modem->i2s_bt.fd_sys_ap_path);
                modem->i2s_bt.fd_sys_ap = open(modem->i2s_bt.fd_sys_ap_path,O_RDWR | O_SYNC);
                if(modem->i2s_bt.fd_sys_ap == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_ap ctl fd,errno is %d",errno);
                }
            }
        }

        if(!modem->i2s_bt.is_ext) {
            if (strcmp(attr[14], "cp0_bt_ctl_file") == 0) {
                ALOGD("cp0_bt_ctl_file");
                if((strlen(attr[15]) +1) <= I2S_CTL_PATH_MAX){
                    memcpy(modem->i2s_bt.fd_bt_cp0_path , attr[15], strlen(attr[15])+1);
                   ALOGE(" cp0_bt_ctl_file is %s",modem->i2s_bt.fd_bt_cp0_path);
                    modem->i2s_bt.fd_bt_cp0 = open(modem->i2s_bt.fd_bt_cp0_path,O_RDWR | O_SYNC);
                    if(modem->i2s_bt.fd_bt_cp0 == -1) {
                        ALOGE(" audio_hw_primary: could not open i2s bt_cp0_ctl fd,errno is %d",errno);
                    }
                }
            }
            if (strcmp(attr[16], "cp1_bt_ctl_file") == 0) {
                if((strlen(attr[17]) +1) <= I2S_CTL_PATH_MAX){
                    memcpy(modem->i2s_bt.fd_bt_cp1_path , attr[17], strlen(attr[17])+1);
                    ALOGE(" cp1_bt_ctl_file is %s",modem->i2s_bt.fd_bt_cp1_path);
                    modem->i2s_bt.fd_bt_cp1 = open(modem->i2s_bt.fd_bt_cp1_path,O_RDWR | O_SYNC);
                    if(modem->i2s_bt.fd_bt_cp1 == -1) {
                        ALOGE(" audio_hw_primary: could not open i2s bt_cp1 ctl fd,errno is %d",errno);
                    }
                }
            }

            if (strcmp(attr[18], "cp2_bt_ctl_file") == 0) {
                if((strlen(attr[19]) +1) <= I2S_CTL_PATH_MAX){
                    memcpy(modem->i2s_bt.fd_bt_cp2_path , attr[19], strlen(attr[19])+1);
                    ALOGE(" cp2_bt_ctl_file is %s",modem->i2s_bt.fd_bt_cp2_path);
                    modem->i2s_bt.fd_bt_cp2 = open(modem->i2s_bt.fd_bt_cp2_path,O_RDWR | O_SYNC);
                    if(modem->i2s_bt.fd_bt_cp2 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s bt_cp2 ctl fd,errno is %d",errno);
                    }
                }
            }
            if (strcmp(attr[20], "ap_bt_ctl_file") == 0) {
                if((strlen(attr[21]) +1) <= I2S_CTL_PATH_MAX){
                    memcpy(modem->i2s_bt.fd_bt_ap_path , attr[21], strlen(attr[21])+1);
                    ALOGE(" ap_bt_ctl_file is %s",modem->i2s_bt.fd_bt_ap_path);
                    modem->i2s_bt.fd_bt_ap = open(modem->i2s_bt.fd_bt_ap_path,O_RDWR | O_SYNC);
                    if(modem->i2s_bt.fd_bt_ap == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s bt_ap ctl fd,errno is %d",errno);
                    }
                }
            }
        }
    }
   else if (strcmp(tag_name, "i2s_for_extspeaker") == 0)
    {
        if (strcmp(attr[0], "index") == 0) {
            ALOGD("The i2s_for_extspeaker index is '%s'", attr[1]);
            modem->i2s_extspk.index = atoi((char *)attr[1]);
        } else {
            ALOGE("no iis_ctl index for extspk call!");
        }
        if (strcmp(attr[2], "switch") == 0) {
            ALOGD("The iis_for_btcall switch is '%s'", attr[3]);
            if(strcmp(attr[3],"1") == 0)
                modem->i2s_extspk.is_switch = true;
            else if(strcmp(attr[3],"0") == 0)
                modem->i2s_extspk.is_switch = false;
        } else {
            ALOGE("no iis_ctl switch for extspk call!");
        }
        if (strcmp(attr[4], "dst") == 0) {
            if (strcmp(attr[5], "external") == 0)
                modem->i2s_extspk.is_ext = 1;
            else if(strcmp(attr[5], "internal") == 0)
                modem->i2s_extspk.is_ext = 0;

            ALOGD("The i2s_for_extspeaker dst  is '%d'", modem->i2s_extspk.is_ext);

        }
        else {
                ALOGE("no dst path for bt call!");
        }
       if (strcmp(attr[6], "cp0_ctl_file") == 0) {
            ALOGD("i2s_for_extspeaker cp0_bt_ctl_file");
            if((strlen(attr[7]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_extspk.fd_sys_cp0_path , attr[7], strlen(attr[7])+1);
                ALOGE(" cp0_ctl_file is %s",modem->i2s_extspk.fd_sys_cp0_path);
                modem->i2s_extspk.fd_sys_cp0 = open(modem->i2s_extspk.fd_sys_cp0_path,O_RDWR | O_SYNC);
                if(modem->i2s_extspk.fd_sys_cp0 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_cp0_ctl fd,errno is %d",errno);
                }
            }
        }
        if (strcmp(attr[8], "cp1_ctl_file") == 0) {
            if((strlen(attr[9]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_extspk.fd_sys_cp1_path , attr[9], strlen(attr[9])+1);
                ALOGE(" cp1_ctl_file is %s",modem->i2s_extspk.fd_sys_cp1_path);
                modem->i2s_extspk.fd_sys_cp1 = open(modem->i2s_extspk.fd_sys_cp1_path,O_RDWR | O_SYNC);
                if(modem->i2s_extspk.fd_sys_cp1 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_cp1 ctl fd,errno is %d",errno);
                }
            }
        }
        if (strcmp(attr[10], "cp2_ctl_file") == 0) {
            if((strlen(attr[11]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_extspk.fd_sys_cp2_path , attr[11], strlen(attr[11])+1);
                ALOGE(" cp2_ctl_file is %s",modem->i2s_extspk.fd_sys_cp2_path);
                modem->i2s_extspk.fd_sys_cp2 = open(modem->i2s_extspk.fd_sys_cp2_path,O_RDWR | O_SYNC);
                if(modem->i2s_extspk.fd_sys_cp2 == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_cp2 ctl fd,errno is %d",errno);
                }
            }
        }
        if (strcmp(attr[12], "ap_ctl_file") == 0) {
            if((strlen(attr[13]) +1) <= I2S_CTL_PATH_MAX){
                memcpy(modem->i2s_extspk.fd_sys_ap_path , attr[13], strlen(attr[13])+1);
                ALOGE(" ap_ctl_file is %s",modem->i2s_extspk.fd_sys_ap_path);
                modem->i2s_extspk.fd_sys_ap = open(modem->i2s_extspk.fd_sys_ap_path,O_RDWR | O_SYNC);
                if(modem->i2s_extspk.fd_sys_ap == -1) {
                    ALOGE(" audio_hw_primary: could not open i2s sys_ap ctl fd,errno is %d",errno);
                }
            }
        }
    }
    else if ((strcmp(tag_name, "voip")&& !modem->voip_res.is_done) == 0) {

            char prop_t[PROPERTY_VALUE_MAX] = {0};
            char prop_w[PROPERTY_VALUE_MAX] = {0};
            bool t_enable = false;
            bool w_enalbe = false;

            if(property_get(MODEM_T_ENABLE_PROPERTY, prop_t, "") && 0 == strcmp(prop_t, "1") )
            {
                MY_TRACE("%s:%s",__func__,MODEM_T_ENABLE_PROPERTY);
                t_enable = true;
            }
            if(property_get(MODEM_W_ENABLE_PROPERTY, prop_w, "") && 0 == strcmp(prop_w, "1"))
            {
                MY_TRACE("%s:%s",__func__,MODEM_W_ENABLE_PROPERTY);
                w_enalbe = true;
            }
           /* Obtain the modem num */

           if (strcmp(attr[0], "modem") == 0) {

                    if(strcmp(attr[1], "w") == 0)
                    {
                        if(w_enalbe){
                            ALOGD("The voip run on modem  is '%s'", attr[1]);
                            modem->voip_res.cp_type = CP_W;
                            modem->voip_res.is_done = true;
                        }
                        else
                            return ;
                    }
                    else if(strcmp(attr[1], "t") == 0)
                    {

                        if(t_enable){
                            ALOGD("The voip run on modem  is '%s'", attr[1]);
                            modem->voip_res.cp_type = CP_TG;
                            modem->voip_res.is_done = true;
                        }
                        else
                            return;
                    }
           } else {
                    ALOGE("no modem type for voip!");
                    goto attr_err;
           }

           if (strcmp(attr[2], "pipe") == 0) {
                    ALOGD("voip pipe name is %s", attr[3]);
                    if((strlen(attr[3]) +1) <= VOIP_PIPE_NAME_MAX)
                       memcpy(modem->voip_res.pipe_name, attr[3], strlen(attr[3])+1);
            }
            else {
                    ALOGE("'%s' No pipe filed!", attr[2]);
                    goto attr_err;
            }
            if (strcmp(attr[4], "vbchannel") == 0) {
                   modem->voip_res.channel_id = atoi((char *)attr[5]);
            }
            else {
                    ALOGE("'%s' No vbc filed!", attr[4]);
                    goto attr_err;
            }

           if (strcmp(attr[6], "enable") == 0) {
                   ALOGD("The enable_for_voip is '%s'", attr[7]);
                   if(strcmp(attr[7],"1") == 0)
                       modem->voip_res.enable = true;
           } else {
                   ALOGE("no iis_ctl index for bt call!");
           }
   }
   else if (strcmp(tag_name, "debug") == 0) //parse debug info
   {
        if (strcmp(attr[0], "enable") == 0)
        {
            if (strcmp(attr[1], "0") == 0)
            {
                modem->debug_info.enable = 0;
            }
            else
            {
                modem->debug_info.enable = 1;
            }
        }
        else
        {
            ALOGE("no adaptable type for debug!");
            goto attr_err;
        }
    }
    else if (strcmp(tag_name, "debuginfo") == 0) //parse debug info
    {
        if (strcmp(attr[0], "sleepdeltatimegate") == 0)
        {
            ALOGD("The sleepdeltatimegate is  '%s'", attr[1]);
            modem->debug_info.sleeptime_gate=atoi((char *)attr[1]);
        }
        else if (strcmp(attr[0], "pcmwritetimegate") == 0)
        {
            ALOGD("The pcmwritetimegate is  '%s'", attr[1]);
            modem->debug_info.pcmwritetime_gate=atoi((char *)attr[1]);
        }
        else if (strcmp(attr[0], "lastthiswritetimegate") == 0)
        {
            ALOGD("The lastthiswritetimegate is  '%s'", attr[1]);
            modem->debug_info.lastthis_outwritetime_gate=atoi((char *)attr[1]);
        }
        else
        {
            ALOGE("no adaptable info for debuginfo!");
            goto attr_err;
        }
   }

attr_err:
    return;
}
static void adev_modem_end_tag(void *data, const XML_Char *tag_name)
{
    struct modem_config_parse_state *state = data;
}

/* Initialises  the audio params,the modem profile and variables , */
static int adev_modem_parse(struct tiny_audio_device *adev)
{
    struct modem_config_parse_state state;
    XML_Parser parser;
    FILE *file;
    int bytes_read;
    void *buf;
    int i;
    int ret = 0;

    vbc_ctrl_pipe_para_t *vbc_ctrl_pipe_info = NULL;
    audio_modem_t *modem = NULL;

    modem = calloc(1, sizeof(audio_modem_t));
    if (!modem)
    {
        ALOGE("adev_modem_parse alloc fail, size:%d", sizeof(audio_modem_t));
        ret = -ENOMEM;
        goto err_calloc;
    }
    else
    {
        memset(modem, 0, sizeof(audio_modem_t));
    }

    modem->num = 0;
    modem->vbc_ctrl_pipe_info = NULL;

    file = fopen(AUDIO_XML_PATH, "r");
    if (!file) {
        ALOGE("Failed to open %s", AUDIO_XML_PATH);
        ret = -ENODEV;
        goto err_fopen;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        ALOGE("Failed to create XML parser");
        ret = -ENOMEM;
        goto err_parser_create;
    }

    memset(&state, 0, sizeof(state));
    state.modem_info = modem;
    XML_SetUserData(parser, &state);
    XML_SetElementHandler(parser, adev_modem_start_tag, adev_modem_end_tag);

    for (;;) {
        buf = XML_GetBuffer(parser, BUF_SIZE);
        if (buf == NULL)
        {
            ret = -EIO;
            goto err_parse;
        }
        bytes_read = fread(buf, 1, BUF_SIZE, file);
        if (bytes_read < 0)
        {
            ret = -EIO;
            goto err_parse;
        }
        if (XML_ParseBuffer(parser, bytes_read,
                    bytes_read == 0) == XML_STATUS_ERROR) {
            ALOGE("Error in codec PGA xml (%s)", AUDIO_XML_PATH);
            ret = -EINVAL;
            goto err_parse;
        }

        if (bytes_read == 0)
            break;
    }

    adev->cp = modem;
    XML_ParserFree(parser);
    fclose(file);
    return ret;

err_parse:
    XML_ParserFree(parser);
err_parser_create:
    fclose(file);
err_fopen:
err_calloc:
    if(modem){
        free(modem);
        modem = NULL;
    }
    return ret;
}

static void vb_effect_getpara(struct tiny_audio_device *adev)
{
    static bool read_already=0;
    off_t offset = 0;
    AUDIO_TOTAL_T * aud_params_ptr;
    int len = sizeof(AUDIO_TOTAL_T)*adev_get_audiomodenum4eng();
    int srcfd;
    char *filename = NULL;

    adev->audio_para = calloc(1, len);
    if (!adev->audio_para)
    {
        ALOGE("vb_effect_getpara alloc fail, size:%d", len);
        return;
    }
    memset(adev->audio_para, 0, len);
    srcfd = open((char *)(ENG_AUDIO_PARA_DEBUG), O_RDONLY);
    filename = (srcfd < 0 )? ( ENG_AUDIO_PARA):(ENG_AUDIO_PARA_DEBUG);
    if(srcfd >= 0)
    {
        close(srcfd);
    }
    ALOGI("vb_effect_getpara read name:%s.", filename);
    stringfile2nvstruct(filename, adev->audio_para, len); //get data from audio_hw.txt.
    audio_para_ptr = adev->audio_para;
}

static void *audiopara_tuning_thread_entry(void * param)
{
    struct tiny_audio_device *adev = (struct tiny_audio_device *)param;
    int fd_aud = -1;
    int fd_dum = -1;
    int send_fd_aud = -1;
    int ops_bit = 0;
    int result = -1;
    AUDIO_TOTAL_T ram_from_eng;
    int mode_index = 0;
    int buffersize = 0;
    void* pmem = NULL;
    int length = 0;
    //mode_t mode_f = 0;
    ALOGE("%s E\n",__FUNCTION__);
    memset(&ram_from_eng,0x00,sizeof(AUDIO_TOTAL_T));
    if (mkfifo(AUDFIFO,S_IFIFO|0666) <0) {
        if (errno != EEXIST) {
            ALOGE("%s create audio fifo error %s\n",__FUNCTION__,strerror(errno));
            return NULL;
        }
    }
    if(chmod(AUDFIFO, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0) {
        ALOGE("%s Cannot set RW to \"%s\": %s", __FUNCTION__,AUDFIFO, strerror(errno));
    }
    if (mkfifo(AUDFIFO_2,S_IFIFO|0666) <0) {
        if (errno != EEXIST) {
            ALOGE("%s create audio fifo_2 error %s\n",__FUNCTION__,strerror(errno));
            return NULL;
        }
    }
    if(chmod(AUDFIFO_2, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH) != 0) {
        ALOGE("%s Cannot set RW to \"%s\": %s", __FUNCTION__,AUDFIFO_2, strerror(errno));
    }
    fd_aud = open(AUDFIFO,O_RDONLY);
    if (fd_aud == -1) {
        ALOGE("%s open audio FIFO error %s\n",__FUNCTION__,strerror(errno));
        return NULL;
    }
    fd_dum = open(AUDFIFO,O_WRONLY);
    if (fd_dum == -1) {
        ALOGE("%s open dummy audio FIFO error %s\n",__FUNCTION__,strerror(errno));
        close(fd_aud);
        return NULL;
    }
    while (ops_bit != -1) {
        result = read(fd_aud,&ops_bit,sizeof(int));
        ALOGE("%s read audio FIFO result %d,ram_req:%d\n",__FUNCTION__,result,ops_bit);
        if (result >0) {
            pthread_mutex_lock(&adev->lock);
            //1、write parameter to Flash or RAM
            if(ops_bit & ENG_FLASH_OPS){
                ALOGE("%s audio para --> update from flash\n",__FUNCTION__);
                if (adev->audio_para){
                    free(adev->audio_para);
                }
                vb_effect_getpara(adev);
                vb_effect_setpara(adev->audio_para);
            }else if(ops_bit & ENG_RAM_OPS){
                ALOGE("%s audio para --> update from RAM\n",__FUNCTION__);
                result = read(fd_aud,&mode_index,sizeof(int));
                result = read(fd_aud,&ram_from_eng,sizeof(AUDIO_TOTAL_T));
                ALOGE("%s read audio FIFO result %d,mode_index:%d,size:%d\n",__FUNCTION__,result,mode_index,sizeof(AUDIO_TOTAL_T));
                adev->audio_para[mode_index] = ram_from_eng;
            }
            //2、mandatory to set PGA GAIN
            if(ENG_PGA_OPS & ops_bit) {
                SetAudio_gain_route(adev,1);
            }
            //3、mandatory to get Phone information,include hardware version,FM type,DSP-Process-Voip,VBC LoopBack
            if(ENG_PHONEINFO_OPS & ops_bit) {
                buffersize = AUDIO_AT_HARDWARE_NAME_LENGTH + (AUDIO_AT_ITEM_NAME_LENGTH + AUDIO_AT_ITEM_VALUE_LENGTH )*AUDIO_AT_ITEM_NUM ;
                pmem = malloc(buffersize);
                if(pmem!=NULL){
                    memset(pmem,0,buffersize);
                    length = audiopara_get_compensate_phoneinfo(pmem);
                    send_fd_aud = open(AUDFIFO_2,O_WRONLY);
                    if (send_fd_aud == -1) {
                        ALOGE("%s open audio FIFO_2 error %s\n",__FUNCTION__,strerror(errno));
                    } else {
                        ALOGE("%s ENG_PHONEINFO_OPS enter :%d!\n",__FUNCTION__,length);
                        result = write(send_fd_aud ,&length,sizeof(int));
                        result = write(send_fd_aud ,pmem,length);
                        close(send_fd_aud );
                        free(pmem);
                        ALOGE("%s ENG_PHONEINFO_OPS complete!\n",__FUNCTION__);
                    }
                } else {
                    ALOGE("%s allocate ENG_PHONEINFO_OPS error!\n",__FUNCTION__);
                }
            }
            pthread_mutex_unlock(&adev->lock);
            ALOGE("%s read audio FIFO X.\n",__FUNCTION__);
        }
    }
    ALOGE("exit from audio tuning thread");
    close(fd_dum);
    close(fd_aud);
    unlink(AUDFIFO);
    pthread_exit(NULL);
    return NULL;
}

static int audiopara_tuning_manager_create(struct tiny_audio_device *adev)
{
    int ret;
    /* create a thread to manager audiopara tuning.*/
    ret = pthread_create(&adev->audiopara_tuning_thread, NULL,
            audiopara_tuning_thread_entry, (void *)adev);
    if (ret) {
        ALOGE("pthread_create falied, code is %d", ret);
        return ret;
    }
    return ret;
}

static int audiopara_get_compensate_phoneinfo(void* pmsg)
{
    char value[PROPERTY_VALUE_MAX]={0};
    int result = true;
    char* currentPosition = (char*)pmsg;
    char* startPosition = (char*)pmsg;
    //1,get and fill product hareware info.
    if (!property_get("ro.product.hardware", value, "0")){
        result = false;
    }
    ALOGE("%s produc.hardware:%s",__func__,value);
    memcpy(currentPosition,value,sizeof(value));

    //2,get and fill build.version info.
    currentPosition = currentPosition + AUDIO_AT_HARDWARE_NAME_LENGTH;
    if (!property_get("ro.build.version.release", value, "0")){
        result = false;
    }
    ALOGE("%s ro.build.version.release:%s",__func__,value);
    memcpy(currentPosition,value,sizeof(value));


    //3,get and fill digital/linein fm flag.
    currentPosition = currentPosition + AUDIO_AT_HARDWARE_NAME_LENGTH;
    strcpy(currentPosition,AUDIO_AT_DIGITAL_FM_NAME);
    currentPosition = currentPosition + AUDIO_AT_ITEM_NAME_LENGTH;
    if (property_get(FM_DIGITAL_SUPPORT_PROPERTY, value, "0") && strcmp(value, "1") == 0){
        sprintf(currentPosition,"%d",1);
    } else {
        sprintf(currentPosition,"%d",0);
    }
    ALOGE("%s :%s:%s",__func__,(currentPosition - AUDIO_AT_ITEM_NAME_LENGTH),currentPosition);

    //4,get and fill wether fm loop vbc or not.
    currentPosition = currentPosition + AUDIO_AT_ITEM_VALUE_LENGTH;
    strcpy(currentPosition,AUDIO_AT_FM_LOOP_VBC_NAME );
    currentPosition = currentPosition + AUDIO_AT_ITEM_NAME_LENGTH;

    if(s_adev->private_ctl.fm_loop_vbc !=NULL && 1 == mixer_ctl_get_value(s_adev->private_ctl.fm_loop_vbc, 0)){
        sprintf(currentPosition,"%d",1);
    ALOGE("%s :%s:%s,ctrl:%0x,value:%d,ctrl1:%0x,value:%d \n",__func__,(currentPosition - AUDIO_AT_ITEM_NAME_LENGTH),currentPosition,s_adev->private_ctl.fm_loop_vbc,mixer_ctl_get_value(s_adev->private_ctl.fm_loop_vbc, 0),s_adev->private_ctl.ad1_fm_loop_vbc,mixer_ctl_get_value(s_adev->private_ctl.ad1_fm_loop_vbc, 0));
    } else {
        sprintf(currentPosition,"%d",0);
    }

    //5,get and fill whether voip process by DSP.
    currentPosition = currentPosition + AUDIO_AT_ITEM_VALUE_LENGTH;
    strcpy(currentPosition,AUDIO_AT_VOIP_DSP_PROCESS_NAME);
    currentPosition = currentPosition + AUDIO_AT_ITEM_NAME_LENGTH;
#ifdef VOIP_DSP_PROCESS
        sprintf(currentPosition,"%d",1);
#else
        sprintf(currentPosition,"%d",0);
#endif
    ALOGE("%s :%s:%s",__func__,(currentPosition - AUDIO_AT_ITEM_NAME_LENGTH),currentPosition);

    //6,get and fill anthoer item.
    currentPosition = currentPosition + AUDIO_AT_ITEM_VALUE_LENGTH;
    result = currentPosition - startPosition;
    ALOGE("%s :result length:%d",__func__,result);
    return result;
}

static int adev_open(const hw_module_t* module, const char* name,
        hw_device_t** device)
{
    struct tiny_audio_device *adev;
    int ret;
    BLUE_TRACE("adev_open");
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;
    adev = calloc(1, sizeof(struct tiny_audio_device));
    if (!adev)
    {
        ALOGE("vb_effect_getpara alloc fail, size:%d", sizeof(struct tiny_audio_device));
        return -ENOMEM;
    }
    memset(adev, 0, sizeof(struct tiny_audio_device));
    s_adev = adev;

    adev->hw_device.common.tag = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version = AUDIO_DEVICE_API_VERSION_2_0;
    adev->hw_device.common.module = (struct hw_module_t *) module;
    adev->hw_device.common.close = adev_close;

    adev->hw_device.get_supported_devices = adev_get_supported_devices;
    adev->hw_device.init_check = adev_init_check;
    adev->hw_device.set_voice_volume = adev_set_voice_volume;
    adev->hw_device.set_master_volume = adev_set_master_volume;
    adev->hw_device.set_mode = adev_set_mode;
    adev->hw_device.set_master_mute = adev_set_master_mute;
    adev->hw_device.get_master_mute = adev_get_master_mute;
    adev->hw_device.set_mic_mute = adev_set_mic_mute;
    adev->hw_device.get_mic_mute = adev_get_mic_mute;
    adev->hw_device.set_parameters = adev_set_parameters;
    adev->hw_device.get_parameters = adev_get_parameters;
    adev->hw_device.get_input_buffer_size = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream = adev_open_output_stream;
    adev->hw_device.close_output_stream = adev_close_output_stream;
    adev->hw_device.open_input_stream = adev_open_input_stream;
    adev->hw_device.close_input_stream = adev_close_input_stream;
    adev->hw_device.dump = adev_dump;

    pthread_mutex_lock(&adev->lock);
    ret = adev_modem_parse(adev);
    pthread_mutex_unlock(&adev->lock);
    if (ret < 0) {
        ALOGE("Warning:Unable to locate all audio modem parameters from XML.");
    }
    /* get audio para from audio_para.txt*/
    vb_effect_getpara(adev);
    vb_effect_setpara(adev->audio_para);
    /* query sound cards*/
    s_tinycard = get_snd_card_number(CARD_SPRDPHONE);
    s_vaudio = get_snd_card_number(CARD_VAUDIO);
    s_voip = get_snd_card_number(CARD_SCO);
    s_bt_sco = get_snd_card_number(CARD_BT_SCO);
    s_vaudio_w = get_snd_card_number(CARD_VAUDIO_W);

    ALOGI("s_tinycard = %d, s_vaudio = %d,s_voip = %d, s_bt_sco = %d,s_vaudio_w is %d",
            s_tinycard,s_vaudio,s_voip,s_bt_sco,s_vaudio_w);
    if (s_tinycard < 0 && s_vaudio < 0&&(s_voip < 0 ) && (s_vaudio_w < 0)&&(s_bt_sco < 0)) {
        ALOGE("Unable to load sound card, aborting.");
        goto ERROR;
    }
    adev->mixer = mixer_open(s_tinycard);
    if (!adev->mixer) {
        ALOGE("Unable to open the mixer, aborting.");
        goto ERROR;
    }
    /* parse mixer ctl */
    ret = adev_config_parse(adev);
    if (ret < 0) {
        ALOGE("Unable to locate all mixer controls from XML, aborting.");
        goto ERROR;
    }
    BLUE_TRACE("ret=%d, num_dev_cfgs=%d", ret, adev->num_dev_cfgs);
    BLUE_TRACE("dev_cfgs_on depth=%d, dev_cfgs_off depth=%d", adev->dev_cfgs->on_len,  adev->dev_cfgs->off_len);

	ret = dump_parse_xml();
	if (ret < 0) {
        ALOGE("Unable to locate dump information  from XML, aborting.");
        goto ERROR;
    }

    /* generate eq params file of vbc effect*/
    adev->eq_available = false;
    ret = create_vb_effect_params();
    if (ret != 0) {
        ALOGW("Warning: Failed to create the parameters file of vbc_eq");
    } else {
        ret = mixer_ctl_set_enum_by_string(adev->private_ctl.vbc_eq_update, "loading");
        if (ret == 0) adev->eq_available = true;
        ALOGI("eq_loading, ret(%d), eq_available(%d)", ret, adev->eq_available);
    }
    if (adev->eq_available) {
        vb_effect_config_mixer_ctl(adev->private_ctl.vbc_eq_update, adev->private_ctl.vbc_eq_profile_select);
        vb_da_effect_config_mixer_ctl(adev->private_ctl.vbc_da_eq_profile_select);
        vb_ad_effect_config_mixer_ctl(adev->private_ctl.vbc_ad01_eq_profile_select, adev->private_ctl.vbc_ad23_eq_profile_select);
        aud_vb_effect_start(adev);
    }
    /*Parse PGA*/
    adev->pga = audio_pga_init(adev->mixer);
    if (!adev->pga) {
        ALOGE("Warning: Unable to locate PGA from XML.");
    }
    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    adev->mode = AUDIO_MODE_NORMAL;
    adev->out_devices = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_devices = 0;

    adev->pcm_modem_dl = NULL;
    adev->pcm_modem_ul = NULL;
    adev->call_start = 0;
    adev->call_connected = 0;
    adev->call_prestop = 0;
    adev->voice_volume = 1.0f;
    adev->bluetooth_nrec = false;

    adev->input_source = 0;
    mixer_ctl_set_value(adev->private_ctl.vbc_switch, 0, VBC_ARM_CHANNELID);  //switch to arm
    adev->vbc_2arm = mixer_ctl_get_value(adev->private_ctl.vbc_switch,0);
    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;
    /* Create a task to get vbpipe message from cp when voice-call */
    ret = vbc_ctrl_open(adev);
    //ret = 0;
    if (ret < 0)  goto ERROR;

	adev->cp->voip_res.adev = adev;
#ifdef VOIP_DSP_PROCESS
	ret = vbc_ctrl_voip_open(&(adev->cp->voip_res));
	//ret = 0;//vbc_ctrl_voip_open(&(adev->cp->voip_res));
	if (ret < 0) {
	ALOGE("voip: vbc_ctrl_voip_open error ");
	goto ERROR;
	}
#endif

vb_ctl_modem_monitor_open (adev);

/*
this is used to loopback test.
*/
    ret = mmi_audio_loop_open();
    if (ret)  ALOGW("Warning: audio loop can NOT work.");

    ret =audiopara_tuning_manager_create(adev);
    if (ret)  ALOGW("Warning: audio tuning can NOT work.");

    ret = stream_routing_manager_create(adev);
    if (ret) {
        ALOGE("Unable to create stream_routing_manager, aborting.");
        goto ERROR;
    }

    ret = audio_bt_sco_thread_create(adev);
    if (ret) {
        ALOGE("bt sco : Unable to create audio_bt_sco_thread_create, aborting.");
        goto ERROR;
    }

    ret = voice_command_manager_create(adev);
    if (ret) {
        ALOGE("Unable to create voice_command_manager_create, aborting.");
        goto ERROR;
    }


    return 0;

ERROR:
    if (adev->pga)    audio_pga_free(adev->pga);
    if (adev->mixer)  mixer_close(adev->mixer);
    if (adev->audio_para)  free(adev->audio_para);
    if (adev)         free(adev);
    return -EINVAL;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "Spreadtrum Audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};

