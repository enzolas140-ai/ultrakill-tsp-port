/* sles_shim.h - ABI do OpenSL ES 1.0.1 + extensoes Android (subset real).
 *
 * PAPEL ATUAL (rev -L): este header e DOCUMENTACAO/referencia do ABI e nao a
 * fonte compilada. O libOpenSLES.so do port e construido a partir de
 * wrapper/audio/bender_opensles.c (a fonte comprovada, igual a do loader
 * BENDER). Usar este header só se escrever uma implementacao nova - as
 * CONSTANTES abaixo foram conferidas contra bender_opensles.c, que por sua vez
 * foi conferida contra o FMOD real rodando no device.
 *
 * POR QUE ISTO EXISTE (evidencia, nao chute)
 * ------------------------------------------
 * O Unity 2019.4 embarca o FMOD ESTATICO dentro do libunity.so:
 *   strings libunity.so -> "../android/src/fmod_output_opensl.cpp",
 *                          "FMOD OpenSL ES Output", "libOpenSLES.so",
 *                          "slCreateEngine", "SL_IID_ENGINE", "SL_IID_PLAY",
 *                          "SL_IID_BUFFERQUEUE",
 *                          "SL_IID_ANDROIDSIMPLEBUFFERQUEUE",
 *                          "SL_IID_ANDROIDCONFIGURATION", "SL_IID_RECORD",
 *                          "disableOpenSL"
 * e o `tools/dumpelf.py libunity.so --undef` mostra ZERO simbolos sl*. Ou
 * seja: o audio e carregado em runtime (dlopen/dlsym) e a UNICA lib Android
 * que o nosso bridge precisa entregar com ABI de verdade e esta.
 *
 * O stub antigo (wrapper/stubs/opensles_stub.c) devolvia um objeto SEM
 * vtable e ainda exportava slRealizeObject/slGetInterface, que NAO EXISTEM
 * no OpenSL ES real. O FMOD faz `(*engineObject)->Realize(...)`, leu o
 * primeiro campo do nosso struct (um inteiro) como ponteiro de funcao e
 * DESEMBALHOU em lixo - exatamente o SIGBUS "dentro do nativeResume, sem
 * nenhuma linha de [guard]" do log de 2025-11-07 (o audio initialize la
 * dentro). Este header + opensles_sdl.c consertam isso.
 *
 * ABI: todo handle OpenSL ES e COM-like de dois niveis:
 *   typedef struct SLXxxItf_ { ...function pointers... } SLXxxItf_;
 *   typedef const SLXxxItf_ *const *SLXxxItf;
 * entao `(*obj)->Metodo(obj, ...)` funciona. A ORDEM dos campos abaixo e a
 * do header Khronos; trocar a ordem faz o FMOD chamar a funcao errada.
 *
 * IIDs: globais EXPORTADOS de 8 bytes (ponteiro p/ blob opaco de 16). Foi
 * assim que o libOpenSLES.so de referencia deste port foi montado
 * (tools/dumpelf.py: `STT_OBJECT STB_GLOBAL size=8 SL_IID_ENGINE`), entao o
 * FMOD le o valor do simbolo e devolve o MESMO ponteiro - a comparacao por
 * identidade de ponteiro e valida. Exportamos tambem as variantes
 * SL_IID_xxx_ (array de 16) para headers no estilo (&SL_IID_ENGINE_).
 */
#ifndef SLES_SHIM_H
#define SLES_SHIM_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t  SLuint8;   typedef int8_t   SLint8;
typedef uint16_t SLuint16;  typedef int16_t  SLint16;
typedef uint32_t SLuint32;  typedef int32_t  SLint32;
typedef uint64_t SLuint64;  typedef int64_t  SLint64;
typedef int32_t  SLresult;
typedef uint32_t SLenum;
typedef float    SLmillisecond;
typedef int32_t  SLmillibel;
typedef uint32_t SLboolean;
typedef const void *SLInterfaceID;

#define SL_BOOLEAN_FALSE ((SLboolean)0U)
#define SL_BOOLEAN_TRUE  ((SLboolean)1U)

enum {
  SL_RESULT_SUCCESS                = 0,
  SL_RESULT_PRECONDITIONS_VIOLATED = 1,
  SL_RESULT_PARAMETER_INVALID      = 2,
  SL_RESULT_MEMORY_FAILURE         = 3,
  SL_RESULT_RESOURCE_ERROR         = 4,
  SL_RESULT_RESOURCE_LOST_IN_MEDI  = 5,
  SL_RESULT_MEDIA_ERROR            = 6,
  SL_RESULT_SECURITY_FAILURE       = 7,
  SL_RESULT_INTERNAL_ERROR         = 8,
  SL_RESULT_UNKNOWN_INTERNAL_ERROR = 9,
  SL_RESULT_PREVIOUSLY_OCCUPIED    = 10,
  SL_RESULT_CALL_NOT_ALLOWED       = 11,
  SL_RESULT_FEATURE_UNSUPPORTED    = 12,
  SL_RESULT_INTERNAL_NOT_IMPLEMENTED = 13,
  SL_RESULT_EXTENDED               = 100
};

enum {
  SL_OBJECT_STATE_UNREALIZED = 1,
  SL_OBJECT_STATE_REALIZING  = 2,
  SL_OBJECT_STATE_REALIZED   = 3,
  SL_OBJECT_STATE_RUNNING    = 4
};

enum {
  SL_PLAYSTATE_STOPPED = 1,
  SL_PLAYSTATE_PAUSED  = 2,
  SL_PLAYSTATE_PLAYING = 3
};

/* Locators / formats (SLdataLocator.h, SLdataFormat.h).
 * Valores CONFERIDOS contra audio/bender_opensles.c (o que o FMOD aceita):
 * nao confie em memoria - um locator errado faz o CreateAudioPlayer cair no
 * ramo 'default' e o player nasce mudo. */
enum {
  SL_DATALOCATOR_UNDEFINED      = 0,
  SL_DATALOCATOR_BUFFERQUEUE    = 6,
  SL_DATALOCATOR_OUTPUTMIX      = 4,
  SL_DATALOCATOR_PCMBUFFER      = 5,
  SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE = 0x800007BD,
  SL_DATALOCATOR_ANDROIDFD      = 0x800007C1,
  SL_DATAFORMAT_UNSPECIFIED     = 0,
  SL_DATAFORMAT_MIME            = 3,
  SL_DATAFORMAT_PCM             = 2,
  SL_BYTEORDER_LITTLEENDIAN     = 2,
  SL_BYTEORDER_BIGENDIAN        = 1,
  SL_DATASOURCE_TYPE_AUTODETECT = -1
};

enum {
  SL_ANDROID_UNKNOWN                     = 100,
  SL_ANDROID_MEDIA_ATTACHMENT            = 101,
  SL_ANDROID_VOICE_COMMUNICATION         = 103,
  SL_ANDROID_VOICE_COMMUNICATION_SIGNALING = 104,
  SL_ANDROID_NOTIFICATION                = 105,
  SL_ANDROID_RING_TONE                   = 106,
  SL_ANDROID_MUSIC                       = 107,
  SL_ANDROID_ALARM                       = 108,
  SL_ANDROID_FAST_ACCESS                 = 110
};

/* ---- interfaces (ordem = header oficial) ---- */
typedef struct SLObjectItf_ *SLObjectP;
typedef struct SLObjectItf_ {
  SLresult (*Realize)(SLObjectP self, SLenum requestedState,
                      const SLInterfaceID *pInterfacesRequired);
  SLresult (*Destroy)(SLObjectP self);
  SLresult (*GetState)(SLObjectP self, SLenum *pState);
  SLresult (*GetInterface)(SLObjectP self, SLInterfaceID iid, void **pInterface);
  SLresult (*SetInterface)(SLObjectP self, SLInterfaceID iid, void *pInterface,
                           SLboolean *pResult);
} SLObjectItf_;
typedef const SLObjectItf_ *const *SLObjectItf;

typedef struct SLEngineItf_ *SLEngineP;
typedef struct SLEngineItf_ {
  SLresult (*CreateAudioPlayer)(SLEngineP self, SLObjectItf *pPlayer,
                                const void *pDataSource, const void *pDataSink,
                                SLuint32 cnt, const SLInterfaceID *pIDs,
                                const SLboolean *pReq);
  SLresult (*CreateAudioRecorder)(SLEngineP self, SLObjectItf *pRecorder,
                                  const void *pDataSource, const void *pDataSink,
                                  SLuint32 cnt, const SLInterfaceID *pIDs,
                                  const SLboolean *pReq);
  SLresult (*CreateMixerOutput)(SLEngineP self, void *pMixerOutput,
                                SLObjectP destination, SLuint32 cnt,
                                const SLInterfaceID *pIDs, const SLboolean *pReq);
  SLresult (*EnumerateOutputMix)(SLEngineP self, SLuint32 *pIndex,
                                 SLObjectItf *pOutputMix);
  SLresult (*CreateOutputMix)(SLEngineP self, SLObjectItf *pOutputMix,
                              SLuint32 cnt, const SLInterfaceID *pIDs,
                              const SLboolean *pReq);
  SLresult (*RealizeObject)(SLEngineP self, SLObjectP object,
                            SLenum requestedState, const SLInterfaceID *pIIDs);
  SLresult (*QueryInterfaceSupport)(SLEngineP self, SLInterfaceID iid,
                                    SLboolean *pSupported);
} SLEngineItf_;
typedef const SLEngineItf_ *const *SLEngineItf;

typedef struct SLPlayItf_ *SLPlayP;
typedef struct SLPlayItf_ {
  SLresult (*SetPlayState)(SLPlayP self, SLenum state);
  SLresult (*GetPlayState)(SLPlayP self, SLenum *pState);
  SLresult (*GetDuration)(SLPlayP self, SLmillisecond *pDur);
  SLresult (*GetPosition)(SLPlayP self, SLmillisecond *pPos);
  SLresult (*SetTimePosition)(SLPlayP self, SLmillisecond pos);
  SLresult (*GetTimePosition)(SLPlayP self, SLmillisecond *pPos);
  SLresult (*RegisterCallback)(SLPlayP self, void *pCallback, void *pContext);
  SLresult (*SetPriority)(SLPlayP self, SLuint32 priority);
  SLresult (*GetMaxPrequeueCount)(SLPlayP self, SLint32 *pCount);
  SLresult (*GetMaxPlayCount)(SLPlayP self, SLint32 *pCount);
} SLPlayItf_;
typedef const SLPlayItf_ *const *SLPlayItf;

typedef struct SLBufferQueueState_ { SLuint32 count; SLuint32 playIndex; }
  SLBufferQueueState_;

typedef struct SLBufferQueueItf_ *SLBQItfP;
typedef struct SLBufferQueueItf_ {
  SLresult (*Enqueue)(SLBQItfP self, const void *pBuffer, SLuint32 size);
  SLresult (*Clear)(SLBQItfP self);
  SLresult (*GetState)(SLBQItfP self, SLBufferQueueState_ *pState);
  SLresult (*RegisterCallback)(SLBQItfP self, void *pCallback, void *pContext);
  SLresult (*SetCallbackContext)(SLBQItfP self, void *pContext);
} SLBufferQueueItf_;
typedef const SLBufferQueueItf_ *const *SLBufferQueueItf;

typedef struct SLAndroidSimpleBufferQueueItf_ *SLASBQP;
typedef struct SLAndroidSimpleBufferQueueItf_ {
  SLresult (*Enqueue)(SLASBQP self, const void *pBuffer, SLuint32 size);
  SLresult (*Clear)(SLASBQP self);
  SLresult (*GetState)(SLASBQP self, SLBufferQueueState_ *pState);
  SLresult (*RegisterCallback)(SLASBQP self, void *pCallback, void *pContext);
} SLAndroidSimpleBufferQueueItf_;
typedef const SLAndroidSimpleBufferQueueItf_ *const
  *SLAndroidSimpleBufferQueueItf;

typedef struct SLAndroidConfigurationItf_ *SLAndroidCfgP;
typedef struct SLAndroidConfigurationItf_ {
  SLresult (*SetConfiguration)(SLAndroidCfgP self, const char *pPropName,
                               const void *pPropValue, SLuint32 propSize);
  SLresult (*GetConfiguration)(SLAndroidCfgP self, const char *pPropName,
                               const void **pPropValue, SLuint32 *pPropSize);
  SLresult (*AudioPlayerIsFormatSupported)(SLAndroidCfgP self,
                                           const void *pFormat);
} SLAndroidConfigurationItf_;
typedef const SLAndroidConfigurationItf_ *const *SLAndroidConfigurationItf;

typedef struct SLVolumeItf_ *SLVolumeP;
typedef struct SLVolumeItf_ {
  SLresult (*SetVolumeLevel)(SLVolumeP self, SLmillibel level);
  SLresult (*GetVolumeLevel)(SLVolumeP self, SLmillibel *pLevel);
  SLresult (*GetMaxVolumeLevel)(SLVolumeP self, SLmillibel *pLevel);
  SLresult (*SetMute)(SLVolumeP self, SLboolean mute);
  SLresult (*GetMute)(SLVolumeP self, SLboolean *pMute);
  SLresult (*EnableDistanceAttenuation)(SLVolumeP self, SLboolean enable);
  SLresult (*SetDistanceAttenuationCurve)(SLVolumeP self, SLuint32 curve,
                                          SLuint32 count, const void *pDist,
                                          const void *pGain);
  SLresult (*GetDistanceAttenuation)(SLVolumeP self, SLuint32 *pCurve,
                                     SLuint32 *pCount, void *pDist, void *pGain);
  SLresult (*EnableHeading)(SLVolumeP self, SLboolean enable);
  SLresult (*SetHeading)(SLVolumeP self, SLmillibel angle);
  SLresult (*GetHeading)(SLVolumeP self, SLmillibel *pAngle);
  SLresult (*EnableSNM)(SLVolumeP self, SLboolean enable);
  SLresult (*SetSNM)(SLVolumeP self, SLmillibel offset);
  SLresult (*GetSNM)(SLVolumeP self, SLmillibel *pOffset);
} SLVolumeItf_;
typedef const SLVolumeItf_ *const *SLVolumeItf;

typedef struct SLRecordItf_ *SLRecordP;
typedef struct SLRecordItf_ {
  SLresult (*SetRecordState)(SLRecordP self, SLenum state);
  SLresult (*GetRecordState)(SLRecordP self, SLenum *pState);
  SLresult (*SetDuration)(SLRecordP self, SLmillisecond dur);
  SLresult (*GetPosition)(SLRecordP self, SLmillisecond *pPos);
  SLresult (*GetDuration)(SLRecordP self, SLmillisecond *pDur);
  SLresult (*SetTimePosition)(SLRecordP self, SLmillisecond pos);
  SLresult (*GetTimePosition)(SLRecordP self, SLmillisecond *pPos);
  SLresult (*RegisterCallback)(SLRecordP self, void *cb, void *ctx);
  SLresult (*SetPriority)(SLRecordP self, SLuint32 priority);
} SLRecordItf_;
typedef const SLRecordItf_ *const *SLRecordItf;

/* ---- structs de dados passados pelo FMOD ---- */
typedef struct SLDataFormat_PCM_ {
  SLuint32 formatType;
  SLuint32 numChannels;
  SLuint32 samplesPerSec;
  SLuint32 bitsPerSample;
  SLuint32 containerSize;
  SLuint32 channelMask;
  SLuint32 speakerMap[20];
} SLDataFormat_PCM_;

typedef struct SLDataLocator_AndroidSimpleBufferQueue_ {
  SLuint32 locType;
  SLuint32 numBuffers;
} SLDataLocator_ASBQ_;

typedef struct SLDataLocator_BufferQueue_ {
  SLuint32 locType;
  SLuint32 numBuffers;
} SLDataLocator_BQ_;

typedef struct SLDataLocator_OutputMix_ {
  SLuint32 locType;
  SLObjectP outputMix;
} SLDataLocator_OutputMix_;

typedef struct SLDataSource_ { const void *pLocator; const void *pFormat; } SLDataSource_;
typedef struct SLDataSink_   { const void *pLocator; const void *pFormat; } SLDataSink_;

/* ---- unica funcao livre do OpenSL ES ---- */
SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 engineOptionCnt,
                        const void *pEngineOptions, SLuint32 engineIfCnt,
                        const SLInterfaceID *pEngineIids,
                        const SLboolean *pEngineReq);

/* ---- globais de IID (exportados com tamanho 8 = ponteiro) ---- */
extern void *SL_IID_ENGINE, *SL_IID_PLAY, *SL_IID_RECORD, *SL_IID_VOLUME;
extern void *SL_IID_BUFFERQUEUE, *SL_IID_ANDROIDSIMPLEBUFFERQUEUE;
extern void *SL_IID_ANDROIDCONFIGURATION, *SL_IID_OUTPUTMIX;
extern void *SL_IID_MIXER, *SL_IID_MIXEROUTPUT, *SL_IID_AUDIOPLAYER;
extern void *SL_IID_EFFECTPRESET, *SL_IID_EFFECTSEND;
extern void *SL_IID_ANDROIDBUNDLEAUDIO, *SL_IID_MIMECATEGORY, *SL_IID_BASSBOOST;
extern void *SL_IID_ENVIRONMENTALREVERB, *SL_IID_EQUALIZER;
/* variantes (&SL_IID_x_) para headers no estilo Khronos puro */
extern const SLuint8 SL_IID_ENGINE_[16], SL_IID_PLAY_[16], SL_IID_VOLUME_[16];
extern const SLuint8 SL_IID_BUFFERQUEUE_[16];
extern const SLuint8 SL_IID_ANDROIDSIMPLEBUFFERQUEUE_[16];
extern const SLuint8 SL_IID_ANDROIDCONFIGURATION_[16];
extern const SLuint8 SL_IID_RECORD_[16], SL_IID_OUTPUTMIX_[16];
extern const SLuint8 SL_IID_MIXER_[16], SL_IID_MIXEROUTPUT_[16];
extern const SLuint8 SL_IID_AUDIOPLAYER_[16];

#endif /* SLES_SHIM_H */
