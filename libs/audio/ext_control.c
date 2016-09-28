#include <sys/select.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#define LOG_TAG "ext_contrl"

#define DefaultBufferLength 30000*100
#define FILE_PATH_MUSIC  "data/local/media/audio_dumpmusic.pcm"
#define FILE_PATH_SCO  "data/local/media/audio_dumpsco.pcm"
#define FILE_PATH_BTSCO  "data/local/media/audio_dumpbtsco.pcm"
#define FILE_PATH_VAUDIO  "data/local/media/audio_dumpvaudio.pcm"
#define FILE_PATH_INREAD  "data/local/media/audio_dumpinread.pcm"
#define FILE_PATH_INREAD_NO_PROCESS  "data/local/media/audio_dumpinread_noprocess.pcm"
#define FILE_PATH_INREAD_NO_RESAMPLER  "data/local/media/audio_dumpinread_noresampler.pcm"

#define FILE_PATH_MUSIC_WAV  "data/local/media/audio_dumpmusic.wav"
#define FILE_PATH_SCO_WAV  "data/local/media/audio_dumpsco.wav"
#define FILE_PATH_BTSCO_WAV  "data/local/media/audio_dumpbtsco.wav"
#define FILE_PATH_VAUDIO_WAV  "data/local/media/audio_dumpvaudio.wav"
#define FILE_PATH_INREAD_WAV  "data/local/media/audio_dumpinread.wav"
#define FILE_PATH_INREAD_NO_PROCESS_WAV  "data/local/media/audio_dumpinread_noprocess.wav"
#define FILE_PATH_INREAD_NO_RESAMPLER_WAV  "data/local/media/audio_dumpinread_noresampler.wav"

#define FILE_PATH_HAL_INFO  "data/local/media/audio_hw_info.txt"
#define FILE_PATH_HELP  "data/local/media/help.txt"
#define FILE_PATH_CP_RAM_PRE  "data/local/media/voicepoint"
#define MAX_FILE_LENGTH 255
#define FILE_PATH_CP_ENABLE_POINT_INFO  "data/local/media/cppoint.txt"

#define CP_DUMP_DATA "dumpvoice"
#define CP_DUMP_POINTSIZE "getfilledsize"
#define CP_DUMP_DURATION "setduration"
#define CP_DUMP_POINT_GETDUARTION "getduration"//"whichpoint"
#define CP_DUMP_POINT_ENABLE "setpointon"
#define CP_DUMP_POINT_DISABLE "setpointoff"
#define CP_DUMP_POINT_DISPLAY "getpoint"//"whichpoint"
#define CP_MAX_POINT 30
#define CP_MAX_DURATION 60
#define NB_SAMPLE_RATE 8000
#define VOICE_FRAME_SIZE 2

pthread_t control_audio_loop;

static void *control_audio_loop_process(void *arg);

int ext_control_open(struct tiny_audio_device *adev){
    ALOGI("%s---",__func__);
    if(pthread_create(&control_audio_loop, NULL, control_audio_loop_process, (void *)adev)) {
        ALOGE("control_audio_loop thread creating failed !!!!");
        return -1;
    }
    return 0;
}

static int read_noblock_l(int fd,int8_t *buf,int bytes){
    int ret = 0;
    ret = read(fd,buf,bytes);
    return ret;
}

static void empty_command_pipe(int fd){
    char buff[16];
    int ret;
    do {
        ret = read(fd, &buff, sizeof(buff));
    } while (ret > 0 || (ret < 0 && errno == EINTR));
}

/***********************************************************
 *function: init dump buffer info;
 *
 * *********************************************************/
int init_dump_info(out_dump_t* out_dump,const char* filepath,size_t buffer_length,bool need_cache,bool save_as_wav){
    ALOGE("%s ",__func__);
    if(out_dump == NULL){
        ALOGE("%s, can not init dump info ,out_dump is null",__func__);
        return -1;
    }
    //1,create dump fife
    out_dump->dump_fd = fopen(filepath,"wb");
    if(out_dump->dump_fd == NULL){
        ALOGE("%s, creat dump file err",__func__);
    }
    if(save_as_wav){
        out_dump->wav_fd = open(filepath,O_RDWR);
        if(out_dump->wav_fd <= 0){
           LOG_E("%s creat wav file error",__func__);
        }
    }else{
        out_dump->wav_fd = NULL;
    }

    //2,malloc cache buffer
    if(need_cache){
        out_dump->cache_buffer = malloc(buffer_length);
        if(out_dump->cache_buffer == NULL){
            ALOGE("malloc cache buffer err!");
            if(out_dump->dump_fd > 0){
                fclose(out_dump->dump_fd);
                out_dump->dump_fd = NULL;
            }
            if(out_dump->wav_fd > 0){
                close(out_dump->wav_fd);
                out_dump->wav_fd = 0;
            }
            return -1;
        }
        memset(out_dump->cache_buffer,0,buffer_length);
    }else{
        out_dump->cache_buffer = NULL;
    }
    out_dump->buffer_length = buffer_length;
    out_dump->write_flag = 0;
    out_dump->more_one = false;
    out_dump->total_length = 0;
    if(0 == strcmp(filepath,FILE_PATH_INREAD_WAV) || 0 == strcmp(filepath,FILE_PATH_INREAD) ||
            0 == strcmp(filepath,FILE_PATH_INREAD_NO_PROCESS_WAV) || 0 == strcmp(filepath,FILE_PATH_INREAD_NO_PROCESS)){
        out_dump->sampleRate = 8000;
        out_dump->channels = 1;
    }if(0 == strcmp(filepath,FILE_PATH_INREAD_NO_RESAMPLER_WAV) || 0 == strcmp(filepath,FILE_PATH_INREAD_NO_RESAMPLER)){
        out_dump->sampleRate = 16000;
        out_dump->channels = 1;
    }else{
        out_dump->sampleRate = 44100;
        out_dump->channels = 2;
    }
    return 0;
}

/***********************************************************
 *function: release dump buffer info;
 *
 * *********************************************************/
int release_dump_info(out_dump_t* out_dump){
    LOG_I("%s ",__func__);
    if(out_dump == NULL){
        ALOGE("out_dump is null");
        return -1;
    }
    //1 relese buffer
    LOG_I("release buffer");
    if(out_dump->cache_buffer){
        free(out_dump->cache_buffer);
        out_dump->cache_buffer = NULL;
    }

    //2 close file fd
    LOG_I("release fd");
    if(out_dump->dump_fd){
        fclose(out_dump->dump_fd);
        out_dump->dump_fd = NULL;
    }
    if(out_dump->wav_fd){
        close(out_dump->wav_fd);
        out_dump->wav_fd = 0;
    }

    out_dump->write_flag = 0;
    out_dump->more_one = false;
    out_dump->total_length = 0;
    return 0;
}

/********************************************
 *function: save cache buffer to file
 *
 * ******************************************/
int save_cache_buffer(out_dump_t* out_dump)
{
    LOG_I("%s ",__func__);
    if (out_dump == NULL || out_dump->cache_buffer == NULL) {
        LOG_E("adev or DumpBuffer is NULL");
        return -1;
    }
    size_t written = 0;
    if(out_dump->dump_fd == NULL){
        LOG_E("dump fd is null ");
        return -1;
    }

   if (out_dump->more_one) {
        size_t size1 = out_dump->buffer_length - out_dump->write_flag;
        size_t size2 = out_dump->write_flag;
        LOG_I("size1:%d,size2:%d,buffer_length:%d",size1,size2,out_dump->buffer_length);
        written = fwrite(((uint8_t *)out_dump->cache_buffer + out_dump->write_flag), size1, 1, out_dump->dump_fd);
        written += fwrite((uint8_t *)out_dump->cache_buffer, size2, 1, out_dump->dump_fd);
        out_dump->total_length = out_dump->buffer_length;
    } else {
        written += fwrite((uint8_t *)out_dump->cache_buffer, out_dump->write_flag, 1, out_dump->dump_fd);
        out_dump->total_length = out_dump->write_flag;
    }
    LOG_E("writen:%ld",out_dump->total_length);
    return written;
}

/******************************************
 *function: write dump data to cache buffer
 *
 ******************************************/
size_t dump_to_buffer(out_dump_t *out_dump, void* buf, size_t size)
{

    LOG_I("%s  ",__func__);
    if (out_dump == NULL || out_dump->cache_buffer == NULL || buf == NULL ) {
         LOG_E("adev or DumpBuffer is NULL or buf is NULL");
        return -1;
    }
    size_t copy = 0;
    size_t bytes = size;
    uint8_t *src = (uint8_t *)buf;
    //size>BufferLength,  size larger then the left space,size smaller then the left space
    if (size > out_dump->buffer_length) {
        int Multi = size/out_dump->buffer_length;
        src= buf + (size - (Multi-1) * out_dump->buffer_length);
        bytes = out_dump->buffer_length;
        out_dump->write_flag = 0;
    }
    if (bytes > (out_dump->buffer_length - out_dump->write_flag)) {
        out_dump ->more_one = true;
        size_t size1 = out_dump->buffer_length - out_dump->write_flag;
        size_t size2 = bytes - size1;
        memcpy(out_dump->cache_buffer + out_dump->write_flag,src,size1);
        memcpy(out_dump->cache_buffer,src+size1,size2);
        out_dump->write_flag = size2;
    } else {
        memcpy(out_dump->cache_buffer + out_dump->write_flag,src,bytes);
        out_dump->write_flag += bytes;
        if (out_dump->write_flag >= out_dump->buffer_length) {
            out_dump->write_flag -= out_dump->buffer_length;
        }
    }
    copy = bytes;
    return copy;
}

/***********************************************
 *function: write dump to file directly
 *
 ***********************************************/
int dump_to_file(FILE *out_fd ,void* buffer, size_t size)
{
    LOG_D("%s ,%p,%p,%d",__func__,out_fd,buffer,size);
    int ret = 0;
    if(out_fd){
        ret = fwrite((uint8_t *)buffer,size, 1, out_fd);
        if(ret < 0){
            LOG_W("%s fwrite filed:%d",__func__,size);
        }
    }else{
        LOG_E("out_fd is NULL, can not write");
    }
    return ret;
}

/********************************************
 * function:add wav header
 *
 * *****************************************/
int add_wav_header(out_dump_t* out_dump){
    char header[44];
    long totalAudioLen = out_dump->total_length;
    long totalDataLen = totalAudioLen + 36;
    long longSampleRate = out_dump->sampleRate;
    int channels = out_dump->channels;
    long byteRate = out_dump->sampleRate * out_dump->channels * 2;
    LOG_E("%s ",__func__);
    header[0] = 'R'; // RIFF/WAVE header
    header[1] = 'I';
    header[2] = 'F';
    header[3] = 'F';
    header[4] = (char) (totalDataLen & 0xff);
    header[5] = (char) ((totalDataLen >> 8) & 0xff);
    header[6] = (char) ((totalDataLen >> 16) & 0xff);
    header[7] = (char) ((totalDataLen >> 24) & 0xff);
    header[8] = 'W';
    header[9] = 'A';
    header[10] = 'V';
    header[11] = 'E';
    header[12] = 'f'; // 'fmt ' chunk
    header[13] = 'm';
    header[14] = 't';
    header[15] = ' ';
    header[16] = 16; // 4 bytes: size of 'fmt ' chunk
    header[17] = 0;
    header[18] = 0;
    header[19] = 0;
    header[20] = 1; // format = 1
    header[21] = 0;
    header[22] = (char) channels;
    header[23] = 0;
    header[24] = (char) (longSampleRate & 0xff);
    header[25] = (char) ((longSampleRate >> 8) & 0xff);
    header[26] = (char) ((longSampleRate >> 16) & 0xff);
    header[27] = (char) ((longSampleRate >> 24) & 0xff);
    header[28] = (char) (byteRate & 0xff);
    header[29] = (char) ((byteRate >> 8) & 0xff);
    header[30] = (char) ((byteRate >> 16) & 0xff);
    header[31] = (char) ((byteRate >> 24) & 0xff);
    header[32] = (char) (channels * 16 / 8); // block align
    header[33] = 0;
    header[34] = 16; // bits per sample
    header[35] = 0;
    header[36] = 'd';
    header[37] = 'a';
    header[38] = 't';
    header[39] = 'a';
    header[40] = (char) (totalAudioLen & 0xff);
    header[41] = (char) ((totalAudioLen >> 8) & 0xff);
    header[42] = (char) ((totalAudioLen >> 16) & 0xff);
    header[43] = (char) ((totalAudioLen >> 24) & 0xff);
    if(out_dump == NULL){
        log_e("%s,err:out_dump is null",__func__);
        return -1;
    }
    lseek(out_dump->wav_fd,0,SEEK_SET);
    write(out_dump->wav_fd,header,sizeof(header));

    return 0;
}

/*************************************************
 *function:the interface of dump
 *
 * ***********************************************/
void do_dump(dump_info_t* dump_info, void* buffer, size_t size){
    if(dump_info == NULL){
        LOG_E("err:out_dump or ext_contrl is null");
        return;
    }
    if(dump_info->dump_to_cache){
        if(dump_info->dump_music){
            dump_to_buffer(dump_info->out_music,buffer,size);
        }else if(dump_info->dump_vaudio){
            dump_to_buffer(dump_info->out_vaudio,buffer,size);
        }else if(dump_info->dump_sco){
            dump_to_buffer(dump_info->out_sco,buffer,size);
        }else if(dump_info->dump_bt_sco){
            dump_to_buffer(dump_info->out_bt_sco,buffer,size);
        }else if(dump_info->dump_in_read){
            dump_to_buffer(dump_info->in_read,buffer,size);
        }else if(dump_info->dump_in_read_noprocess){
            dump_to_buffer(dump_info->in_read_noprocess,buffer,size);
        }else if(dump_info->dump_in_read_noresampler){
            dump_to_buffer(dump_info->in_read_noresampler,buffer,size);
        }
    }else{
        if(dump_info->dump_music){
            dump_to_file(dump_info->out_music->dump_fd,buffer,size);
        }else if(dump_info->dump_vaudio){
            dump_to_file(dump_info->out_vaudio->dump_fd,buffer,size);
        }else if(dump_info->dump_sco){
            dump_to_file(dump_info->out_sco->dump_fd,buffer,size);
        }else if(dump_info->dump_bt_sco){
            dump_to_file(dump_info->out_bt_sco->dump_fd,buffer,size);
        }else if(dump_info->dump_in_read){
            dump_to_file(dump_info->in_read->dump_fd,buffer,size);
        }else if(dump_info->dump_in_read_noprocess){
            dump_to_file(dump_info->in_read_noprocess->dump_fd,buffer,size);
        }else if(dump_info->dump_in_read_noresampler){
            dump_to_file(dump_info->in_read_noresampler->dump_fd,buffer,size);
        }
    }
    return;
}

/*************************************************
 *function:dump hal info to file
 *
 * ***********************************************/
void dump_hal_info(struct tiny_audio_device * adev){
    FILE* fd = fopen(FILE_PATH_HAL_INFO,"w+");
    if(fd == NULL){
        LOG_E("%s, open file err",__func__);
        return;
    }
    LOG_D("%s",__func__);
    fprintf(fd,"audio_mode_t:%d \n",adev->mode);
    fprintf(fd,"out_devices:%d \n",adev->out_devices);
    fprintf(fd,"in_devices:%d \n",adev->in_devices);
    fprintf(fd,"prev_out_devices:%d \n",adev->prev_out_devices);
    fprintf(fd,"prev_in_devices:%d \n",adev->prev_in_devices);
    fprintf(fd,"routeDev:%d \n",adev->routeDev);
    fprintf(fd,"cur_vbpipe_fd:%d \n",adev->cur_vbpipe_fd);
    fprintf(fd,"cp_type:%d \n",adev->cp_type);
    fprintf(fd,"call_start:%d \n",adev->call_start);
    fprintf(fd,"call_connected:%d \n",adev->call_connected);
    fprintf(fd,"call_prestop:%d \n",adev->call_prestop);
    fprintf(fd,"vbc_2arm:%d \n",adev->vbc_2arm);
    fprintf(fd,"voice_volume:%f \n",adev->voice_volume);
    fprintf(fd,"mic_mute:%d \n",adev->mic_mute);
    fprintf(fd,"bluetooth_nrec:%d \n",adev->bluetooth_nrec);
    fprintf(fd,"bluetooth_type:%d \n",adev->bluetooth_type);
    fprintf(fd,"low_power:%d \n",adev->low_power);
    fprintf(fd,"realCall:%d \n",adev->realCall);
    fprintf(fd,"num_dev_cfgs:%d \n",adev->num_dev_cfgs);
    fprintf(fd,"num_dev_linein_cfgs:%d \n",adev->num_dev_linein_cfgs);
    fprintf(fd,"eq_available:%d \n",adev->eq_available);
    fprintf(fd,"bt_sco_state:%d \n",adev->bt_sco_state);
    fprintf(fd,"voip_state:%d \n",adev->voip_state);
    fprintf(fd,"voip_start:%d \n",adev->voip_start);
    fprintf(fd,"master_mute:%d \n",adev->master_mute);
    fprintf(fd,"cache_mute:%d \n",adev->cache_mute);
    fprintf(fd,"fm_volume:%d \n",adev->fm_volume);
    fprintf(fd,"fm_open:%d \n",adev->fm_open);
    fprintf(fd,"requested_channel_cnt:%d \n",adev->requested_channel_cnt);
    fprintf(fd,"input_source:%d \n",adev->input_source);

    fprintf(fd,"adev dump info: \n");
    fprintf(fd,"loglevel:%d \n",log_level);
    fprintf(fd,"dump_to_cache:%d \n",adev->ext_contrl->dump_info->dump_to_cache);
    fprintf(fd,"dump_as_wav:%d \n",adev->ext_contrl->dump_info->dump_as_wav);
    fprintf(fd,"dump_music:%d \n",adev->ext_contrl->dump_info->dump_music);
    fprintf(fd,"dump_vaudio:%d \n",adev->ext_contrl->dump_info->dump_vaudio);
    fprintf(fd,"dump_sco:%d \n",adev->ext_contrl->dump_info->dump_sco);
    fprintf(fd,"dump_bt_sco:%d \n",adev->ext_contrl->dump_info->dump_bt_sco);

    fclose(fd);
    return;
}

static int sendandrecv(char* pipe,char*cmdstring,struct timeval* timeout,void* buffer,int size){
    int ret = 0;
    int max_fd_dump;
    int pipe_dump;
    int left = size;
    LOG_D("%s : %s",__func__,cmdstring);
    pipe_dump = open(pipe, O_RDWR);
    if(pipe_dump < 0){
        LOG_E("%s, open %s error!! ",__func__,pipe);
        return -1;
    } else {
        if((fcntl(pipe_dump,F_SETFL,O_NONBLOCK))<0)
        {
            ALOGD("cat set pipe_dump nonblock error!");
        }
        ret = write_nonblock(pipe_dump,cmdstring,strlen(cmdstring));
        if(ret < 0){
            LOG_E("wrrite noblock error ");
            goto exit;
        }
        fd_set fds_read_dump;
        max_fd_dump = pipe_dump + 1;
        while(left > 0) {
            FD_ZERO(&fds_read_dump);
            FD_SET(pipe_dump,&fds_read_dump);
            ret = select(max_fd_dump,&fds_read_dump,NULL,NULL,timeout);
            if(ret < 0){
                LOG_E("cat select error ");
                goto exit;
            }
            if(FD_ISSET(pipe_dump,&fds_read_dump) <= 0 ){
                ret = -1;
                LOG_E("cat SELECT OK BUT NO fd is set");
                goto exit;
            }
            ret = read_noblock_l(pipe_dump,buffer+(size-left),left);
            if(ret < 0){
                LOG_E("cat read data err");
                goto exit;
            }
            left -= ret;
        }
    }
exit:
    close(pipe_dump);
    return ret;
}


static int set_duration(char* pipe,char* value){
    char *curindx = NULL;
    char *preindx = NULL;
    char data[32]= {0};
    int duration = 0;//s
    int rsp = 0;
    int ret,result;
    struct timeval timeout = {5,0};
    if(strlen(value) != 0){
        duration = atoi(value);
        if( 0 < duration && duration <= CP_MAX_DURATION){
            sprintf(data,"%s=%d",CP_DUMP_DURATION,duration);
            ret = sendandrecv(pipe,data,&timeout,&rsp,sizeof(int));
            LOG_D("set duration %d sucess. ",rsp);
        } else {
            LOG_D("Because of ARM precious resource.the required duration %d bigger than the MAX duration %d must be fail.",duration,CP_MAX_DURATION);
        }
    }
exit:
    return ret;
}
static int string2intarray(char* string,int* pointarray,int arraysize){
    char *firstnum= NULL;
    char *preindx = NULL;
    char *curindx = NULL;
    bool isnum = 0;
    char substr[32] = {0};
    int ret = 0;
    int point = 0;
    int pointnum = 0;
    if(string == NULL || pointarray == NULL){
        LOG_D(" NULL pointer.",__func__);
        return -1;
    }
    LOG_D("%s command:%s.",__func__,string);
    if(strlen(string) != 0){
        firstnum = string;
        while((firstnum - string) < sizeof(string)){
            char c = *firstnum;
            if(c <='9'&& c >= '0'){
                isnum = 1;
                break;
            }
            firstnum++;
        }
        preindx = firstnum;
        LOG_V("parse from:%d character",(firstnum-string)+1);

        while((preindx != NULL) && isnum) {
            bool illegal_zero = 0;
            curindx = strstr(preindx,",");
            if(curindx == NULL){
                memcpy(substr,preindx,strlen(preindx));
            } else {
                memcpy(substr,preindx,curindx-preindx);
            }
            point = atoi(substr);
            if( (point==0) && (*substr !='0')){
                illegal_zero = 1 ;// atio("abcd") return 0
            }

            if(point >=0 && point < arraysize && !illegal_zero){
                LOG_D("set point:%s,int:%d",substr,point);
                pointarray[pointnum] = point;
                pointnum++;
            } else {
                if(illegal_zero){
                    LOG_D("fail: illegal string %s ",substr);
                } else {
                    LOG_D("fail: the point %s: reach the MAX NUM supported in cp",substr);
                }
            }
            preindx = (curindx == NULL) ? NULL:(curindx+1);
            memset(substr,0x00,sizeof(substr));
        }
    }
    return pointnum;
}

static int set_point(char *pipe,char* value,int enable)
{
    char *curindx = NULL;
    char *preindx = NULL;
    char *firstnum= NULL;
    bool isnum = 0;
    char cmd[32] = {0};
    int ret = 0;
    int point = 0;
    int pointarray[32] = {0};
    int pointnum = 0;
    struct timeval timeout = {5,0};
    int cur = 0;
    int rsp = 0;
    pointnum  = string2intarray(value,pointarray,sizeof(pointarray));
    if(pointnum <= 0){
        return -1;
    }
    while(cur < pointnum){
        sprintf(cmd,"%s=%d",(enable ? CP_DUMP_POINT_ENABLE : CP_DUMP_POINT_DISABLE),pointarray[cur]);
        ret = sendandrecv(pipe,cmd,&timeout,&rsp,sizeof(int));
        if(ret < 0){
            LOG_E("read data err");
            return -1;
        }
        LOG_D("%s point:%d suceess.",(enable ? "enable":"disable"),rsp);
        cur++;
    }
    return ret;
}
static int get_duration(char *pipe){
    int ret = 0;
    struct timeval timeout = {5,0};
    int duration = 0;
    ret = sendandrecv(pipe,CP_DUMP_POINT_GETDUARTION,&timeout,&duration,sizeof(int));
    if(ret  < -1){
        return -1;
    }
    LOG_D("%s,ret:%d,duration:0x%x",__func__,ret,duration);
    return duration;
}

static int get_enablepoint(char *pipe){
    int ret = 0;
    struct timeval timeout = {5,0};
    int enablebits = 0;
    ret = sendandrecv(pipe,CP_DUMP_POINT_DISPLAY,&timeout,&enablebits,sizeof(int));
    if(ret  < -1){
        return -1;
    }
    LOG_D("%s,ret:%d,enablebits:0x%x",__func__,ret,enablebits);
    return enablebits;
}

static  int get_pointinfo(char *pipe,bool savefile) {
    LOG_V("cat cppoint start f");
    int ret,result;
    struct timeval timeout = {5,0};
    char buffer[32] = {0};
    FILE* dump_fd = NULL;
    int enablebits = 0;
    int temp = 0;
    int point[CP_MAX_POINT]= {0};
    int tzero = 0;
    int duration = 0;
    int indx = 0;
    enablebits = get_enablepoint(pipe);
    if(enablebits < -1){
        return -1;
    }

    LOG_D("cat enablebits,ret:%d,enablebits:0x%x,enablenumber:%d",ret,enablebits,__builtin_popcount(enablebits));
    if(__builtin_popcount(enablebits) == 0){
        LOG_E(" No enable point");
    }
    //parse the enable point from int
    temp = enablebits;
    while(tzero < CP_MAX_POINT){
        tzero = __builtin_ctz(temp);
        if(tzero < CP_MAX_POINT){
            point[tzero] = 1;
            LOG_E("The enable point %d .",tzero);
        }
        temp &= ~(1 << tzero);
    }

    duration = get_duration(pipe);
    if(duration < -1){
        goto exit;
    }
    LOG_D("cat duration,ret:%d,duration:0x%x",ret,duration);

    if(savefile){
        dump_fd = fopen(FILE_PATH_CP_ENABLE_POINT_INFO,"wb");
        if(dump_fd == NULL){
            LOG_E("cat fopen %s err",FILE_PATH_CP_ENABLE_POINT_INFO);
            goto exit;
        }
        ret = dump_to_file(dump_fd,"enable point: ",strlen("enable point:"));
        for(indx = 0;indx < CP_MAX_POINT;indx++){
            if(enablebits & (1<<indx)){
                memset(buffer,0x00,sizeof(buffer));
                sprintf(buffer,"%d  ",indx);
                ret = dump_to_file(dump_fd,buffer,strlen(buffer));
                if(ret < 0){
                    fclose(dump_fd);
                    LOG_E("cat dump read data err");
                    goto exit;
                }
            }
        }
        ret = dump_to_file(dump_fd,"\n",strlen("\n"));

        memset(buffer,0x00,sizeof(buffer));
        sprintf(buffer,"duration: %ds",duration);
        ret = dump_to_file(dump_fd,buffer,strlen(buffer));
        if(ret < 0){
            LOG_E("cat dump read data err");
            fclose(dump_fd);
            goto exit;
        }
        ret = dump_to_file(dump_fd,"\n",strlen("\n"));
        fclose(dump_fd);
    }
exit:
    return ret;
}

char* get_pointinfo_hal(char *pipe) {
    LOG_V("cat cppoint start f");
    int ret,result;
    struct timeval timeout = {5,0};
    char buffer[32] = {0};
    char buffer_all[1024] = {0};
    int enablebits = 0;
    int temp = 0;
    int point[CP_MAX_POINT]= {0};
    int tzero = 0;
    int duration = 0;
    int indx = 0;
    enablebits = get_enablepoint(pipe);
    if(enablebits < -1){
        return -1;
    }

    LOG_D("cat enablebits,ret:%d,enablebits:0x%x,enablenumber:%d",ret,enablebits,__builtin_popcount(enablebits));
    if(__builtin_popcount(enablebits) == 0){
        LOG_E(" No enable point");
    }
    //parse the enable point from int
    temp = enablebits;
    while(tzero < CP_MAX_POINT){
        tzero = __builtin_ctz(temp);
        if(tzero < CP_MAX_POINT){
            point[tzero] = 1;
            LOG_E("The enable point %d .",tzero);
        }
        temp &= ~(1 << tzero);
    }

    duration = get_duration(pipe);
    if(duration < -1){
        goto exit;
    }
    LOG_D("cat duration,ret:%d,duration:0x%x",ret,duration);
    memset(buffer,0x00,sizeof(buffer));
    sprintf(buffer,"enable point:  ");
    strcat(buffer_all,buffer);
    for(indx = 0;indx < CP_MAX_POINT;indx++){
        if(enablebits & (1<<indx)){
            memset(buffer,0x00,sizeof(buffer));
            sprintf(buffer,"%d  ",indx);
            strcat(buffer_all,buffer);
        }
    }
    memset(buffer,0x00,sizeof(buffer));
    sprintf(buffer,"\n\nduration: %ds\n",duration);
    strcat(buffer_all,buffer);
    return buffer_all;
exit:
    return ret;
}

void  savememory2file(void*buffer,int size,int point){
    char filepath[MAX_FILE_LENGTH] = {0};
    FILE* dump_fd = NULL;
    int ret = 0;
    sprintf(filepath,"%s%d%s",FILE_PATH_CP_RAM_PRE,point,".pcm");
    LOG_E("%s %s",__func__,filepath);
    dump_fd = fopen(filepath,"wb");
    if(dump_fd == NULL){
        LOG_E("cat fopen data err:%s",filepath);
        return -1;
    }
    ret = dump_to_file(dump_fd,buffer,size);
    if(ret < 0){
        LOG_E("cat dump read data err");
        fclose(dump_fd);
        return -1;
    }
    fclose(dump_fd);
    return 0;
}

static int dump_voice(char* value,char* pipe)
{
    LOG_V("ramdump start.");
    int ret;
    struct timeval timeout = {5,0};
    void *buffer = NULL;
    int length = 0;
    int enablebits = 0;
    int cur = 0;
    int duration = 0;
    char cmd[32] = {0};
    int pointarray[32] = {0};
    int pointnum = 0;
    enablebits = get_enablepoint(pipe);
    if(enablebits <= 0){
        return -1;
    }
    pointnum  = string2intarray(value,pointarray,sizeof(pointarray));
    while(cur < pointnum){
        LOG_D("%s %d poind%d,%d",__func__,pointnum, cur,pointarray[cur]);
        cur++;
    }

    if(0 == strncmp(value,"all",strlen("all"))){
        pointnum = CP_MAX_POINT; //dump all point
        for(cur=0;cur < sizeof(pointarray)/sizeof(pointarray[0]);cur++){
            pointarray[cur] = cur;
        }
    }

    if(pointnum > 0){
        for(cur=0;cur<pointnum;cur++){
            if(enablebits & (1 << pointarray[cur])){
                sprintf(cmd,"%s=%d",CP_DUMP_POINTSIZE,pointarray[cur]);
                ret = sendandrecv(pipe,cmd,&timeout,&length,sizeof(int));
                LOG_D("%s,point:%d,length:%d",__func__,pointarray[cur],length);
                if(ret < 0 || (length == 0)){
                    LOG_D("%s,ret:%d,length:%d",__func__,ret,length);
                    continue;
                }
                buffer = malloc(length);
                if(buffer == NULL){
                    return -1;
                }
                sprintf(cmd,"%s=%d",CP_DUMP_DATA,pointarray[cur]);
                ret = sendandrecv(pipe,cmd,&timeout,buffer,length);
                if(ret < 0){
                    break;
                }
                savememory2file(buffer,length,pointarray[cur]);
                free(buffer);
                buffer = NULL;
            }
        }
    }
    if(buffer != NULL){
        free(buffer);
        buffer = NULL;
    }
    return ret;
}

static void *control_audio_loop_process(void *arg){
    int pipe_fd,max_fd;
    fd_set fds_read;
    int result;
    int count;
    void* data;
    int val_int;
    struct str_parms *parms;
    char value[30];
    int ret = 0;
    int retdump;
    struct tiny_audio_device *adev = (struct tiny_audio_device *)arg;
    FILE* help_fd = NULL;
    void* help_buffer = NULL;
    FILE* pipe_d = NULL;

    pipe_fd = open("/dev/pipe/mmi.audio.ctrl", O_RDWR);
    if(pipe_fd < 0){
        LOG_E("%s, open pipe error!! ",__func__);
        return NULL;
    }
    max_fd = pipe_fd + 1;
    if((fcntl(pipe_fd,F_SETFL,O_NONBLOCK)) <0){
        LOG_E("set flag RROR --------");
    }
    data = (char*)malloc(1024);
    if(data == NULL){
        LOG_E("malloc data err");
        return NULL;
    }
    LOG_I("begin to receive audio control message");
    while(1){
        FD_ZERO(&fds_read);
        FD_SET(pipe_fd,&fds_read);
        result = select(max_fd,&fds_read,NULL,NULL,NULL);
        if(result < 0){
            LOG_E("select error ");
            continue;
        }
        if(FD_ISSET(pipe_fd,&fds_read) <= 0 ){
            LOG_E("SELECT OK BUT NO fd is set");
            continue;
        }
        memset(data,0,1024);
        count = read_noblock_l(pipe_fd,data,1024);
        if(count < 0){
            LOG_E("read data err");
            empty_command_pipe(pipe_fd);
            continue;
        }
        LOG_E("data:%s ",data);
        parms = str_parms_create_str(data);

        ret = str_parms_get_str(parms,"dumpmusic", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_E("dumpmusic is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_music,FILE_PATH_MUSIC_WAV,
                                adev->ext_contrl->dump_info->out_music->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_music,FILE_PATH_MUSIC,
                                adev->ext_contrl->dump_info->out_music->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_music = true;
            }else{
                adev->ext_contrl->dump_info->dump_music = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_music);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_music);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_music);
            }
        }

        ret = str_parms_get_str(parms,"dumpsco", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_sco,FILE_PATH_SCO_WAV,
                                adev->ext_contrl->dump_info->out_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_sco,FILE_PATH_SCO,
                                adev->ext_contrl->dump_info->out_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_sco = true;
            }else{
                adev->ext_contrl->dump_info->dump_sco = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_sco);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_sco);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_sco);
            }
        }

        ret = str_parms_get_str(parms,"dumpbtsco", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpbtsco is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_bt_sco,FILE_PATH_BTSCO_WAV,
                                adev->ext_contrl->dump_info->out_bt_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_bt_sco,FILE_PATH_BTSCO,
                                adev->ext_contrl->dump_info->out_bt_sco->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_bt_sco = true;
            }else{
                adev->ext_contrl->dump_info->dump_bt_sco = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_bt_sco);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_bt_sco);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_bt_sco);
            }
         }

        ret = str_parms_get_str(parms,"dumpvaudio", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpvaudio is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                     init_dump_info(adev->ext_contrl->dump_info->out_vaudio,FILE_PATH_VAUDIO_WAV,
                                adev->ext_contrl->dump_info->out_vaudio->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->out_vaudio,FILE_PATH_VAUDIO,
                                adev->ext_contrl->dump_info->out_vaudio->buffer_length,
                                adev->ext_contrl->dump_info->dump_to_cache,
                                adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_vaudio = true;
            }else{
                adev->ext_contrl->dump_info->dump_vaudio = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->out_vaudio);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->out_vaudio);
                }
                release_dump_info(adev->ext_contrl->dump_info->out_vaudio);
            }
        }

        ret = str_parms_get_str(parms,"dumpinread", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpinread is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    init_dump_info(adev->ext_contrl->dump_info->in_read,FILE_PATH_INREAD_WAV,
                               adev->ext_contrl->dump_info->in_read->buffer_length,
                               adev->ext_contrl->dump_info->dump_to_cache,
                               adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                    init_dump_info(adev->ext_contrl->dump_info->in_read,FILE_PATH_INREAD,
                               adev->ext_contrl->dump_info->in_read->buffer_length,
                               adev->ext_contrl->dump_info->dump_to_cache,
                               adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_in_read = true;
            }else{
                adev->ext_contrl->dump_info->dump_in_read = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->in_read);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->in_read);
                }
                release_dump_info(adev->ext_contrl->dump_info->in_read);
            }
        }

        ret = str_parms_get_str(parms,"dumpinnoprocess", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpinread is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    init_dump_info(adev->ext_contrl->dump_info->in_read_noprocess,FILE_PATH_INREAD_NO_PROCESS_WAV,
                               adev->ext_contrl->dump_info->in_read_noprocess->buffer_length,
                               adev->ext_contrl->dump_info->dump_to_cache,
                               adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                     init_dump_info(adev->ext_contrl->dump_info->in_read_noprocess,FILE_PATH_INREAD_NO_PROCESS,
                               adev->ext_contrl->dump_info->in_read_noprocess->buffer_length,
                               adev->ext_contrl->dump_info->dump_to_cache,
                               adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_in_read_noprocess = true;
            }else{
                adev->ext_contrl->dump_info->dump_in_read_noprocess = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->in_read_noprocess);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->in_read_noprocess);
                }
                release_dump_info(adev->ext_contrl->dump_info->in_read_noprocess);
            }
        }

        ret = str_parms_get_str(parms,"dumpinnoresampler", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dumpinread is :%d",val_int);
            if(val_int){
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    init_dump_info(adev->ext_contrl->dump_info->in_read_noresampler,FILE_PATH_INREAD_NO_RESAMPLER_WAV,
                               adev->ext_contrl->dump_info->in_read_noresampler->buffer_length,
                               adev->ext_contrl->dump_info->dump_to_cache,
                               adev->ext_contrl->dump_info->dump_as_wav);
                }else{
                     init_dump_info(adev->ext_contrl->dump_info->in_read_noresampler,FILE_PATH_INREAD_NO_RESAMPLER,
                               adev->ext_contrl->dump_info->in_read_noresampler->buffer_length,
                               adev->ext_contrl->dump_info->dump_to_cache,
                               adev->ext_contrl->dump_info->dump_as_wav);
                }
                adev->ext_contrl->dump_info->dump_in_read_noresampler = true;
            }else{
                adev->ext_contrl->dump_info->dump_in_read_noresampler = false;
                if(adev->ext_contrl->dump_info->dump_to_cache){
                    save_cache_buffer(adev->ext_contrl->dump_info->in_read_noresampler);
                }
                if(adev->ext_contrl->dump_info->dump_as_wav){
                    add_wav_header(adev->ext_contrl->dump_info->in_read_noresampler);
                }
                release_dump_info(adev->ext_contrl->dump_info->in_read_noresampler);
            }
        }
        ret = str_parms_get_str(parms,"dumpcache", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dump to cache :%d",val_int);
            if(val_int){
                adev->ext_contrl->dump_info->dump_to_cache = true;
             }else{
                adev->ext_contrl->dump_info->dump_to_cache = false;
             }
        }
        ret = str_parms_get_str(parms,"dumpwav", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("dump as wav :%d",val_int);
            if(val_int){
                adev->ext_contrl->dump_info->dump_as_wav = true;
             }else{
                adev->ext_contrl->dump_info->dump_as_wav = false;
             }
        }

        ret = str_parms_get_str(parms,"bufferlength", value, sizeof(value));
        {
            if(ret >= 0){
                val_int = atoi(value);
                LOG_D("set buffer length:%d",val_int);
                adev->ext_contrl->dump_info->out_music->buffer_length = val_int;
                adev->ext_contrl->dump_info->out_bt_sco->buffer_length = val_int;
                adev->ext_contrl->dump_info->out_sco->buffer_length = val_int;
                adev->ext_contrl->dump_info->out_vaudio->buffer_length = val_int;
            }
        }

        ret = str_parms_get_str(parms,"loglevel", value, sizeof(value));
        if(ret >= 0){
            val_int = atoi(value);
            LOG_D("log is :%d",val_int);
            if(val_int >= 0){
                log_level = val_int;
            }
        }

        ret = str_parms_get_str(parms,"help",value,sizeof(value));
        if(ret >= 0){
            help_fd = fopen(FILE_PATH_HELP,"rb");
            pipe_d = fopen("/dev/pipe/mmi.audio.ctrl","wb");
            help_buffer = (void*)malloc(1024);
            if(help_fd == NULL || pipe_d == NULL){
                LOG_E("ERROR ------------");
            }else{
                while(fgets(help_buffer,1024,help_fd)){
                    fputs(help_buffer,pipe_d);
                }
            }
            fclose(pipe_d);
            fclose(help_fd);
            free(help_buffer);
            sleep(5);
        }

        ret = str_parms_get_str(parms,"dumphalinfo",value,sizeof(value));
        if(ret >= 0){
            LOG_D("dump audio hal info");
            dump_hal_info(adev);
        }

        //echo setpointon=17,5 > dev/pipe/mmi.audio.ctrl
        ret = str_parms_get_str(parms,CP_DUMP_POINT_ENABLE,value,sizeof(value));
        if(ret >= 0){
            LOG_V("enable dump point:%s,pipe:%s",value,adev->cp_nbio_pipe);
            ret = set_point(adev->cp_nbio_pipe,value,1);
        }

        //echo setpointoff=17 > dev/pipe/mmi.audio.ctrl
        ret = str_parms_get_str(parms,CP_DUMP_POINT_DISABLE,value,sizeof(value));
        if(ret >= 0){
            LOG_V("disable dump point:%s,pipe:%s",value,adev->cp_nbio_pipe);
            ret = set_point(adev->cp_nbio_pipe,value,0);
        }

        //echo getpointinfo=1 > dev/pipe/mmi.audio.ctrl
        ret = str_parms_get_str(parms,CP_DUMP_POINT_DISPLAY,value,sizeof(value));
        if(ret >= 0){
            LOG_V("get enabled point:%s.pipe:%s",value,adev->cp_nbio_pipe);
            ret = get_pointinfo(adev->cp_nbio_pipe,1);
        }

        //echo dumpvoice=1,2,17 > dev/pipe/mmi.audio.ctrl
        ret = str_parms_get_str(parms,CP_DUMP_DATA,value,sizeof(value));
        if(ret >= 0){
            ret = dump_voice(value,adev->cp_nbio_pipe);
        }

        //echo setduration=15> dev/pipe/mmi.audio.ctrl
        ret = str_parms_get_str(parms,CP_DUMP_DURATION,value,sizeof(value));
        if(ret >= 0){
            ret = set_duration(adev->cp_nbio_pipe,value);
        }
        str_parms_destroy(parms);
        memset(value,0x00,sizeof(value));
    }
    free(data);
    return NULL;
}
