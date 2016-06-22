
#define LOG_TAG "audio_mux_pcm"

#include <stdlib.h>
#include <sys/select.h>
#include <fcntl.h>
#include <tinyalsa/asoundlib.h>
#include <cutils/log.h>
#include <pthread.h>






#define SAUDIO_CMD_NONE          0x00000000
#define SAUDIO_CMD_OPEN           0x00000001
#define SAUDIO_CMD_CLOSE         0x00000002
#define SAUDIO_CMD_START         0x00000004
#define SAUDIO_CMD_STOP             0x00000008
#define SAUDIO_CMD_PREPARE  0x00000010
#define SAUDIO_CMD_TRIGGER              0x00000020
#define SAUDIO_CMD_RECEIVE         0x00000040

#define SAUDIO_CMD_OPEN_RET            0x00010000
#define SAUDIO_CMD_CLOSE_RET         0x00020000
#define SAUDIO_CMD_START_RET          0x00040000
#define SAUDIO_CMD_STOP_RET             0x00080000
#define SAUDIO_CMD_PREPARE_RET  0x00100000
#define SAUDIO_CMD_TRIGGER_RET  0x00200000
#define SAUDIO_CMD_RECEIVE_RET         0x00400000

#define AUDIO_PLAYBACK  0
#define AUDIO_CAPTURE     1

#define PCM_ERROR_MAX 128
#define AUDIO_PLAYBACK_BUFFER 1200

#define AUDIO_MUX_CTRL_FILE     "/dev/ts0710mux24"
#define AUDIO_MUX_PLAYBACK_FILE "/dev/ts0710mux25"
#define AUDIO_MUX_CAPTURE_FILE "/dev/ts0710mux26"


struct pcm {
    int fd;
    unsigned int flags;
    int running:1;
    int underruns;
    unsigned int buffer_size;
    unsigned int boundary;
    char error[PCM_ERROR_MAX];
    struct pcm_config config;
    struct snd_pcm_mmap_status *mmap_status;
    struct snd_pcm_mmap_control *mmap_control;
    struct snd_pcm_sync_ptr *sync_ptr;
    void *mmap_buffer;
    unsigned int noirq_frames_per_msec;
    int wait_for_avail_min;
};



struct mux_pcm
{
    struct pcm dummy_pcm;
    pthread_mutex_t lock; 
    int  mux_fd;
    int stream_type;
    int state;
};


struct cmd_common {
	unsigned int command;
	unsigned int sub_cmd;
	unsigned int reserved1;
	unsigned int reserved2;
};

struct cmd_prepare {
	struct cmd_common common;
	unsigned int rate;	/* rate in Hz */
	unsigned char channels;	/* channels */
	unsigned char format;
	unsigned char reserved1;
	unsigned char reserved2;
	unsigned int period;	/* period size */
	unsigned int periods;	/* periods */
};

struct cmd_open {
	struct cmd_common common;
	uint32_t stream_type;
};



static int audio_ctrl_fd=0;
static pthread_mutex_t  audio_mux_ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;

static int  audio_mux_ctrl_lock()
{
     pthread_mutex_lock(&audio_mux_ctrl_mutex);
     return 0;
}

static int audio_mux_ctrl_unlock()
{
     pthread_mutex_unlock(&audio_mux_ctrl_mutex);
     return 0;
}

int32_t audio_mux_ctrl_send(uint8_t * data, uint32_t  bytes)
{
    int result=-1;
    audio_mux_ctrl_lock();
    if(audio_ctrl_fd>0) {
	result=write(audio_ctrl_fd, data ,bytes);		
     }
     audio_mux_ctrl_unlock();
    return result;
}








 int32_t saudio_wait_common_cmd( uint32_t cmd, uint32_t subcmd)
{
	int32_t result = 0;
	struct cmd_common cmd_common_buffer={0};
	struct cmd_common *common = &cmd_common_buffer;
	ALOGE(": function is saudio_wait_common_cmd in");

	if(audio_ctrl_fd <0){
            return -1;
	}
	audio_mux_ctrl_lock();
	result=read(audio_ctrl_fd,common,sizeof(struct cmd_common));
        
	ALOGE("common->command is %x ,sub cmd %x,\n", common->command, common->sub_cmd);
	if (subcmd) {
		if ((common->command == cmd) && (common->sub_cmd == subcmd)) {
			result = 0;
		} else {
			result = -1;
		}
	} else {
		if (common->command == cmd) {
			result = 0;
		} else {
			result = -1;
		}
	}
	audio_mux_ctrl_unlock();
	ALOGE(": function is saudio_wait_common_cmdout,result is %d",result);
	return result;
}


 int32_t saudio_send_common_cmd(uint32_t cmd, uint32_t subcmd)
{
	int32_t result = -1;
	struct cmd_common common={0};
	struct cmd_common cmd_common_buffer={0};
	struct cmd_common *common_ret = &cmd_common_buffer;
	uint32_t cmd_ret=cmd<<16;
	
	ALOGE(":  saudio_send_common_cmd  E");
	ALOGE("cmd %x, subcmd %x\n",  cmd, subcmd);
	common.command=cmd;
	common.sub_cmd=subcmd;	
	if(audio_ctrl_fd <0){
		return -1;
	}
	audio_mux_ctrl_lock();
	
	result=write(audio_ctrl_fd,&common,sizeof(struct cmd_common));
	if(result <0) 
	{
		audio_mux_ctrl_unlock();
		return result;
	}    


	result=read(audio_ctrl_fd,common_ret,sizeof(struct cmd_common));

	ALOGE(":common->command is %x ,sub cmd %x,\n", common_ret->command, common_ret->sub_cmd);

	if (common_ret->command == cmd_ret) 
	{
		result = 0;
	} else
	{
		result = -1;
	}

	
	audio_mux_ctrl_unlock();
	ALOGE(":  saudio_send_common_cmd  X");
	return result;
}



struct pcm * mux_pcm_open(unsigned int card, unsigned int device,
                     unsigned int flags, struct pcm_config *config)
{
    struct mux_pcm  *pcm=NULL;
    int sub_cmd=0;
    int ret=0;
    ALOGE(": function is mux_pcm_open in flags is %x",flags);
    pcm = calloc(1, sizeof(struct mux_pcm));
    if (!pcm)
        return NULL;
    memset(pcm, 0, sizeof(struct mux_pcm));    
    
    audio_mux_ctrl_lock();
    if(audio_ctrl_fd <=  0) {
        audio_ctrl_fd = open(AUDIO_MUX_CTRL_FILE, O_RDWR);
        if(audio_ctrl_fd <= 0 ){
            ALOGE(": mux_pcm_open ctrl open failed");
            audio_mux_ctrl_unlock();        
            goto error;
        }
    }
    audio_mux_ctrl_unlock();     
    
    sub_cmd = (flags&PCM_IN)?AUDIO_CAPTURE:AUDIO_PLAYBACK;
    pcm->stream_type = sub_cmd;
    ALOGE(":pcm->stream_type  is %d",pcm->stream_type );
    if(pcm->stream_type == AUDIO_PLAYBACK){
        pcm->mux_fd=open(AUDIO_MUX_PLAYBACK_FILE, O_RDWR);
     }
     else{
        pcm->mux_fd=open(AUDIO_MUX_CAPTURE_FILE, O_RDWR);
     }
     
     if(pcm->mux_fd<= 0){
       goto error;
    }
    
    ret =saudio_send_common_cmd(SAUDIO_CMD_OPEN,sub_cmd);
    if(ret){
        goto error;
    }

    pcm->dummy_pcm.fd=pcm->mux_fd;
    pcm->dummy_pcm.config=*config;
     pthread_mutex_init(&pcm->lock, NULL);
    memcpy(pcm->dummy_pcm.error,"unknow error",sizeof("unknow error"));
    
    ALOGE(": function is mux_pcm_open out");
    return &(pcm->dummy_pcm);

error:
    pcm->dummy_pcm.fd=-1;
    return pcm; 
    
}


int mux_pcm_write(struct pcm *pcm_in, void *data, unsigned int count)
{
    struct mux_pcm  *pcm=(struct mux_pcm  *)pcm_in;
    int ret=0;
    int bytes=0;
	int left = count;
	int offset=0;
	int sendlen=0;
	
    ALOGE(": function is mux_pcm_write,count:%d",count);
    if( !pcm){
        return 0;
    }
    if(pcm->mux_fd >0){
        struct cmd_common common={0};
        pthread_mutex_lock(&pcm->lock);
        if(!pcm->state) {
            pcm->state = 1;
            ret = saudio_send_common_cmd(SAUDIO_CMD_START,pcm->stream_type);
            if(ret){
                pthread_mutex_unlock(&pcm->lock);
                return 0;
            }
        }
        pthread_mutex_unlock(&pcm->lock);
		while(left)		
		{
			    sendlen = left < AUDIO_PLAYBACK_BUFFER ? left : AUDIO_PLAYBACK_BUFFER;

				ALOGE("mux_pcm_write in %d",sendlen);
				bytes= write(pcm->mux_fd,data+offset,sendlen);
				offset += sendlen;
				left -= sendlen;
				ALOGE("mux_pcm_write out %d,left :%d",bytes,left);
				
				if(bytes)
				{	
					ret = saudio_wait_common_cmd(SAUDIO_CMD_RECEIVE<<16 ,pcm->stream_type);
					if(ret)
					{
						return 0;
					}
				}	

				
	   }
    }
    ALOGE(": function is mux_pcm_write out");
    return bytes;
}



int mux_pcm_read(struct pcm *pcm_in, void *data, unsigned int count)
{
     struct mux_pcm  *pcm=(struct mux_pcm  *)pcm_in;
     int ret=0;
    int bytes=count;
    int bytes_read=0;
    ALOGE(": function is mux_pcm_read in");
    if( !pcm){
        return 0;
    }
    if(pcm->mux_fd > 0){
        pthread_mutex_lock(&pcm->lock);
        if(!pcm->state) {
            pcm->state = 1;
            ret =saudio_send_common_cmd(SAUDIO_CMD_START,pcm->stream_type);
            if(ret){
                pthread_mutex_unlock(&pcm->lock);
                return -1;
            }
        }
         pthread_mutex_unlock(&pcm->lock);
        ALOGE("mux_pcm_read in %d",count);
        while(bytes){           
           bytes_read= read(pcm->mux_fd,data,bytes);
           bytes -= bytes_read;
       }
       ALOGE("mux_pcm_read out %d",bytes);
    }
    ALOGE(": function is mux_pcm_read out");
    return bytes;
}


int mux_pcm_close(struct pcm *pcm_in)
{
     struct mux_pcm  *pcm=(struct mux_pcm  *)pcm_in;
    int ret=0;
    ALOGE(": function is mux_pcm_close");
    if( !pcm){
        return -1;
    }
    if(pcm->mux_fd>0){
        pthread_mutex_lock(&pcm->lock);
        if(pcm->state == 1){
            ret = saudio_send_common_cmd(SAUDIO_CMD_STOP,pcm->stream_type);
            if(ret){
                pthread_mutex_unlock(&pcm->lock);
                return ret;
            }
            pcm->state = 0;
        }
        pthread_mutex_unlock(&pcm->lock);
        ret = saudio_send_common_cmd(SAUDIO_CMD_CLOSE,pcm->stream_type);
        if(ret){
            return ret;
        }
       ret= close(pcm->mux_fd);
       pcm->mux_fd=-1;
     
    }
      free(pcm);
    ALOGE(": function is mux_pcm_close out");
    return ret;


}


