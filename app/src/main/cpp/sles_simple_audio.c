#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <math.h>
#include "sa_spat_svz.h"
#include "audio_buffer_ring.h"
#include "sf1.h"
#include "sf2.h"
#include "sf3.h"

#if 1

#define MODULE_NAME  "HELLO-JNI-SLES"
#define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, MODULE_NAME, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, MODULE_NAME, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, MODULE_NAME, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,MODULE_NAME, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,MODULE_NAME, __VA_ARGS__)
#define LOGF(...) __android_log_print(ANDROID_LOG_FATAL,MODULE_NAME, __VA_ARGS__)

#else

#define LOGV(...)
#define LOGD(...)
#define LOGI(...)
#define LOGW(...)
#define LOGE(...)
#define LOGF(...)
#endif

/* Round a size to an integer multiple of size a */
#define _ROUND_ALIGN(s, a) (((s) / (a) + (((s) % (a)) != 0)) * (a))

#define DEVICE_SHADOW_BUFFER_QUEUE_LEN      4
#define BLOCK_SIZE 512
#define N_IN_CHANS 3
#define N_OUT_CHANS 2

#define SLASSERT(x)   do {\
    LOGI("Checking, x = %u",(unsigned)x);\
    assert(SL_RESULT_SUCCESS == (x));\
    (void) (x);\
    } while (0)

/* Synthesize impulse train */
struct synth_t {
    float phs;
    float freq;
    float crossed;
};
typedef struct synth_t synth_t;

/* Assumes positive frequency */
float synth_tick(synth_t *synth, float sample_rate)
{
    float ret = synth->crossed;
    synth->crossed = 0;
    synth->phs += 1./sample_rate * synth->freq;
    if (synth->phs >= 1.) {
        synth->crossed = 1.;
    }
    while (synth->phs >= 1.) {
        synth->phs -= 1.;
    }
    return ret;
}

struct wavetable_t {
    float *start_ptr;
    float *ptr;
    int totlen;
    int remlen;
};
typedef struct wavetable_t wavetable_t;

void
wavetable_tick(wavetable_t *wt, float *out, int len)
{
    while (len) {
        if (wt->remlen < len) {
            memcpy(out,wt->ptr,wt->remlen*sizeof(float));
            len -= wt->remlen;
            wt->ptr = wt->start_ptr;
            wt->remlen = wt->totlen;
        } else {
            memcpy(out,wt->ptr,len*sizeof(float));
            wt->ptr += len;
            wt->remlen -= len;
            len = 0;
        }
    }
}

void
wavetable_init(wavetable_t *wt, float *dat, int len)
{
    *wt = (wavetable_t) {
        .start_ptr = dat,
        .ptr = dat,
        .totlen = len,
        .remlen = len
    };
}

struct SampleFormat {
    uint32_t   sampleRate_;
    uint32_t   framesPerBuf_;
    uint16_t   channels_;
    uint16_t   pcmFormat_;          //8 bit, 16 bit, 24 bit ...
    uint32_t   representation_;     //android extensions
};
typedef struct SampleFormat SampleFormat;

struct audio_player_t {
    synth_t *synths;
    wavetable_t *wts;
    SampleFormat sampleInfo_;
    volatile int done;
    audio_buffer_ring_t *arng;
    SASpatSVZ *svz;
    SASpatSVZRenderer *rend;
    float *indat;
    float *outs;
};
typedef struct audio_player_t audio_player_t;

void ConvertToSLSampleFormat(SLAndroidDataFormat_PCM_EX *pFormat,
                                    SampleFormat* pSampleInfo_) {

    assert(pFormat);
    memset(pFormat, 0, sizeof(*pFormat));

    pFormat->formatType = SL_DATAFORMAT_PCM;
    if( pSampleInfo_->channels_  <= 1 ) {
        pFormat->numChannels = 1;
        pFormat->channelMask = SL_SPEAKER_FRONT_CENTER;
    } else {
        pFormat->numChannels = N_OUT_CHANS;
        pFormat->channelMask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    }
    pFormat->sampleRate  = pSampleInfo_->sampleRate_;

    pFormat->endianness  = SL_BYTEORDER_LITTLEENDIAN;
    pFormat->bitsPerSample = pSampleInfo_->pcmFormat_;
    pFormat->containerSize = pSampleInfo_->pcmFormat_;

    /*
     * fixup for android extended representations...
     */
    pFormat->representation = pSampleInfo_->representation_;
    switch (pFormat->representation) {
        case SL_ANDROID_PCM_REPRESENTATION_UNSIGNED_INT:
            pFormat->bitsPerSample =  SL_PCMSAMPLEFORMAT_FIXED_8;
            pFormat->containerSize = SL_PCMSAMPLEFORMAT_FIXED_8;
            pFormat->formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
            break;
        case SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT:
            pFormat->bitsPerSample =  SL_PCMSAMPLEFORMAT_FIXED_16; //supports 16, 24, and 32
            pFormat->containerSize = SL_PCMSAMPLEFORMAT_FIXED_16;
            pFormat->formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
            break;
        case SL_ANDROID_PCM_REPRESENTATION_FLOAT:
            pFormat->bitsPerSample =  SL_PCMSAMPLEFORMAT_FIXED_32;
            pFormat->containerSize = SL_PCMSAMPLEFORMAT_FIXED_32;
            pFormat->formatType = SL_ANDROID_DATAFORMAT_PCM_EX;
            break;
        case 0:
            break;
        default:
            assert(0);
    }
}

void render_callback (
            float **in,
            size_t n_in_chans,
            size_t n_frames,
            void *aux)
{
    audio_player_t *audio_player = aux;
    //synth_t *synths = audio_player->synths;
    SampleFormat sample_info = audio_player->sampleInfo_;
    float sample_rate = sample_info.sampleRate_;
    sample_rate *= 1.e-3;
    int n, m;
    for (n = 0;
         n < n_in_chans;
         n++) {
        in[n] = &audio_player->indat[n*BLOCK_SIZE];
        wavetable_tick(&audio_player->wts[n],in[n],n_frames);
        //for (m = 0; m < n_frames; m++) {
        //    in[n][m] = synth_tick(&synths[n],sample_rate);
        //}
    }
}

void
audio_callback(SLAndroidSimpleBufferQueueItf bq, void *userData)
{
    audio_player_t *audio_player = userData;
    SampleFormat sample_info = audio_player->sampleInfo_;
    memset(audio_player->outs,0,
            sizeof(float)*sample_info.framesPerBuf_*sample_info.channels_);
    float *outs[] = {audio_player->outs,audio_player->outs+sample_info.framesPerBuf_};
    SASpatSVZRenderer_render(audio_player->rend,
            render_callback,outs,sample_info.framesPerBuf_,userData);
    int16_t *curbuf = audio_buffer_ring_get_cur_dat(audio_player->arng);
    audio_buffer_ring_rotate(audio_player->arng);
    int n, m;
    for (n = 0; n < sample_info.channels_; n++) {
        for (m = 0; m < sample_info.framesPerBuf_; m++) {
            curbuf[sample_info.channels_*m+n] = outs[n][m] * 0x7fff;
        }
    }
    if (audio_player->done == 0) {
        (*bq)->Enqueue(bq,curbuf,
                sizeof(int16_t)*sample_info.framesPerBuf_*sample_info.channels_);
    }
}

void
make_sound(int samplerate, int framesperbuf)
{
    static synth_t synths[] = {{
            .phs = 0.,
            .freq = 0.5,
            .crossed = 1.
        },{
            .phs = 0.333,
            .freq = 0.5,
            .crossed = 1.
        },{
            .phs = 0.666,
            .freq = 0.5,
            .crossed = 1.
        }
    };
    static wavetable_t wts[N_IN_CHANS];
    wavetable_init(&wts[0],(float*)sf1,sizeof(sf1)/sizeof(float));
    wavetable_init(&wts[1],(float*)sf2,sizeof(sf2)/sizeof(float));
    wavetable_init(&wts[2],(float*)sf3,sizeof(sf3)/sizeof(float));
    audio_player_t _ap = {
        .synths = synths,
        .wts = wts,
        .sampleInfo_ = (SampleFormat) {
            .sampleRate_ = samplerate * 1000,
            .framesPerBuf_ = framesperbuf,
            .channels_ = 2,
            .pcmFormat_ = SL_PCMSAMPLEFORMAT_FIXED_16,
            /* doesn't seem representation_ is used */
        },
        .done = 0,
    };
    audio_buffer_ring_init_t rngi = {
        .n_bufs = DEVICE_SHADOW_BUFFER_QUEUE_LEN,
        .buf_sz = _ap.sampleInfo_.framesPerBuf_*_ap.sampleInfo_.channels_ * sizeof(int16_t),
        .align = 16, /* TODO: calloc does always allocate to 16 byte boundary no? */
    };
    audio_player_t *ap = calloc(_ROUND_ALIGN(sizeof(audio_player_t),16)
            + audio_buffer_ring_sz(&rngi),1);
    if (!ap) {
        goto cleanup;
    }
    *ap = _ap;
    ap->arng = (audio_buffer_ring_t*)((char*)ap + _ROUND_ALIGN(sizeof(audio_player_t),16));
    audio_buffer_ring_init(ap->arng,&rngi);
    float sample_rate = ap->sampleInfo_.sampleRate_;
    sample_rate *= 1.e-3;
    int16_t *wtdat = NULL;
    SLObjectItf  slEngineObj_ = NULL;
    SLEngineItf  slEngineItf_ = NULL;
    SLresult result;
    SLObjectItf outputMixObjectItf_ = NULL;
    SLObjectItf playerObjectItf_ = NULL;
    SLPlayItf   playItf_ = NULL;
    SLAndroidSimpleBufferQueueItf playBufferQueueItf_;
    result = slCreateEngine(&slEngineObj_, 0, NULL, 0, NULL, NULL);
    SLASSERT(result);
    result = (*slEngineObj_)->Realize(slEngineObj_, SL_BOOLEAN_FALSE);
    SLASSERT(result);
    result = (*slEngineObj_)->GetInterface(slEngineObj_, SL_IID_ENGINE, &slEngineItf_);
    SLASSERT(result);
    result = (*slEngineItf_)->CreateOutputMix(slEngineItf_, &outputMixObjectItf_,
                                          0, NULL, NULL);
    SLASSERT(result);

    // realize the output mix
    result = (*outputMixObjectItf_)->Realize(outputMixObjectItf_, SL_BOOLEAN_FALSE);
    SLASSERT(result);

    // configure audio source
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
            SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE,
            DEVICE_SHADOW_BUFFER_QUEUE_LEN };

    SLAndroidDataFormat_PCM_EX format_pcm;
    ConvertToSLSampleFormat(&format_pcm, &ap->sampleInfo_);
    SLDataSource audioSrc = {&loc_bufq, &format_pcm};

    // configure audio sink
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObjectItf_};
    SLDataSink audioSnk = {&loc_outmix, NULL};
    /*
     * create fast path audio player: SL_IID_BUFFERQUEUE and SL_IID_VOLUME interfaces ok,
     * NO others!
     */
    SLInterfaceID  ids[2] = { SL_IID_BUFFERQUEUE, SL_IID_VOLUME};
    SLboolean      req[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};
    result = (*slEngineItf_)->CreateAudioPlayer(slEngineItf_, &playerObjectItf_, &audioSrc, &audioSnk,
                                            sizeof(ids)/sizeof(ids[0]), ids, req);
    SLASSERT(result);

    // realize the player
    result = (*playerObjectItf_)->Realize(playerObjectItf_, SL_BOOLEAN_FALSE);
    SLASSERT(result);

    // get the play interface
    result = (*playerObjectItf_)->GetInterface(playerObjectItf_, SL_IID_PLAY, &playItf_);
    SLASSERT(result);

    // get the buffer queue interface
    result = (*playerObjectItf_)->GetInterface(playerObjectItf_, SL_IID_BUFFERQUEUE,
                                             &playBufferQueueItf_);
    SLASSERT(result);

    // register callback on the buffer queue
    result = (*playBufferQueueItf_)->RegisterCallback(playBufferQueueItf_, audio_callback, ap);
    SLASSERT(result);

    result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
    SLASSERT(result);

    /* Space for data that render_callback uses for input data. The
       SASpatSVZRenderer will never request more than BLOCK_SIZE frames,
       so we allocate that much space for the input data. 
       TODO: SASpatSVZRenderer should do this.
       3 because 3 input channels 
       2*_ap.sampleInfo_.framesPerBuf_ because that's
       the amount of storage SASpatSVZRenderer has for 1 channel.
     */
    ap->indat = calloc(BLOCK_SIZE*3,
            sizeof(float));
    if (!ap->indat) {
        goto cleanup;
    }
    ap->outs = calloc(_ap.sampleInfo_.framesPerBuf_,
            sizeof(float));
    if (!ap->outs) {
        goto cleanup;
    }

    SASpatSVZ_SampleRate _sample_rate = SASpatSVZ_SampleRate_get_closest(sample_rate);
    ap->svz = SASpatSVZ_new(N_IN_CHANS, BLOCK_SIZE, _sample_rate);
    if (!ap->svz) {
        goto cleanup;
    }
    ap->rend = SASpatSVZRenderer_new(ap->svz,
            2*_ap.sampleInfo_.channels_);
    if (!ap->rend) {
        goto cleanup;
    }
    SASpatSVZ_choose_preset(ap->svz,0,0);
    SASpatSVZ_choose_preset(ap->svz,1,1);
    SASpatSVZ_choose_preset(ap->svz,2,2);

    /* Enque something before starting to play? */
    audio_callback(playBufferQueueItf_,ap);

    result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
    SLASSERT(result);

    LOGI("Starting to play");
    size_t num_presets = SASpatSVZ_get_num_presets();
    LOGI("number of presets: %zu",num_presets);
    /* Play for 5 seconds (?) */
    //sleep(5);
    while (!ap->done);

    result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
    SLASSERT(result);
cleanup:
    if (ap) {
        if (ap->indat) {
            free(ap->indat);
        }
        if (ap->outs) {
            free(ap->outs);
        }
        if (ap->svz) {
            SASpatSVZ_free(ap->svz);
        }
        if (ap->rend) {
            SASpatSVZRenderer_free(ap->rend);
        }
        free(ap);
    }
    if (playerObjectItf_ != NULL) {
        (*playerObjectItf_)->Destroy(playerObjectItf_);
    }
    if (outputMixObjectItf_) {
        (*outputMixObjectItf_)->Destroy(outputMixObjectItf_);
    }
    (*slEngineObj_)->Destroy(slEngineObj_);
}

