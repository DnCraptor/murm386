#ifndef VIDEO_PROFILE_H
#define VIDEO_PROFILE_H

#include <stdint.h>

typedef enum VideoAdapterProfile {
    VIDEO_ADAPTER_MCGA = 0,
    VIDEO_ADAPTER_EGA128,
    VIDEO_ADAPTER_EGA256,
    VIDEO_ADAPTER_VGA128,
    VIDEO_ADAPTER_VGA256
} VideoAdapterProfile;

#if defined(VIDEO_RUNTIME)
#define VIDEO_ADAPTER_DEFAULT VIDEO_ADAPTER_VGA256
#elif defined(MCGA)
#define VIDEO_ADAPTER_DEFAULT VIDEO_ADAPTER_MCGA
#elif defined(EGA128)
#define VIDEO_ADAPTER_DEFAULT VIDEO_ADAPTER_EGA128
#elif defined(VGA128)
#define VIDEO_ADAPTER_DEFAULT VIDEO_ADAPTER_VGA128
#else
#define VIDEO_ADAPTER_DEFAULT VIDEO_ADAPTER_VGA256
#endif

extern VideoAdapterProfile video_adapter_profile;
extern uint32_t video_vram_mask;

void video_profile_set(VideoAdapterProfile profile);
VideoAdapterProfile video_profile_get(void);
uint32_t video_profile_vram_size(void);
uint32_t video_profile_page_cache_size(void);
uint32_t video_profile_page_cache_count(void);
void video_profile_configure_memory(void);
const char *video_profile_name(void);
const char *video_profile_config_value(void);
int video_profile_parse(const char *value, VideoAdapterProfile *profile);
int video_profile_supports_mode(uint8_t mode);

static inline int video_profile_is_mcga(void)   { return video_adapter_profile == VIDEO_ADAPTER_MCGA; }
static inline int video_profile_is_ega128(void) { return video_adapter_profile == VIDEO_ADAPTER_EGA128; }
static inline int video_profile_is_ega256(void) { return video_adapter_profile == VIDEO_ADAPTER_EGA256; }
static inline int video_profile_is_ega(void)    { return video_profile_is_ega128() || video_profile_is_ega256(); }
static inline int video_profile_is_vga128(void) { return video_adapter_profile == VIDEO_ADAPTER_VGA128; }
static inline int video_profile_is_vga256(void) { return video_adapter_profile == VIDEO_ADAPTER_VGA256; }
static inline int video_profile_has_256k_vram(void) { return video_profile_is_ega256() || video_profile_is_vga256(); }

#endif
