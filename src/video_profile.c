#include "video_profile.h"
#include "mem.h"
#include <string.h>
#include <strings.h>

VideoAdapterProfile video_adapter_profile = VIDEO_ADAPTER_DEFAULT;
uint32_t video_vram_mask =
    VIDEO_ADAPTER_DEFAULT == VIDEO_ADAPTER_MCGA ? 0x0ffffu :
    (VIDEO_ADAPTER_DEFAULT == VIDEO_ADAPTER_EGA128 || VIDEO_ADAPTER_DEFAULT == VIDEO_ADAPTER_VGA128) ? 0x1ffffu : 0x3ffffu;

void video_profile_set(VideoAdapterProfile profile)
{
#if defined(NO_PAGING)
    (void)profile;
    video_adapter_profile = VIDEO_ADAPTER_VGA256;
#else
    if ((unsigned)profile > (unsigned)VIDEO_ADAPTER_VGA256)
        profile = VIDEO_ADAPTER_DEFAULT;
    video_adapter_profile = profile;
#endif
    video_vram_mask =
        video_adapter_profile == VIDEO_ADAPTER_MCGA ? 0x0ffffu :
        (video_adapter_profile == VIDEO_ADAPTER_EGA128 || video_adapter_profile == VIDEO_ADAPTER_VGA128) ? 0x1ffffu : 0x3ffffu;
}

VideoAdapterProfile video_profile_get(void)
{
    return video_adapter_profile;
}

uint32_t video_profile_vram_size(void)
{
    return video_vram_mask + 1u;
}

uint32_t video_profile_page_cache_size(void)
{
    switch (video_adapter_profile) {
    case VIDEO_ADAPTER_MCGA:   return 192u << 10;
    case VIDEO_ADAPTER_EGA128:
    case VIDEO_ADAPTER_VGA128: return 128u << 10;
    case VIDEO_ADAPTER_EGA256:
    default:                   return 40u << 10;
    }
}

uint32_t video_profile_page_cache_count(void)
{
    return video_profile_page_cache_size() >> 11;
}

const char *video_profile_name(void)
{
    switch (video_adapter_profile) {
    case VIDEO_ADAPTER_MCGA:   return "MCGA 64 KB";
    case VIDEO_ADAPTER_EGA128: return "EGA 128 KB";
    case VIDEO_ADAPTER_EGA256: return "EGA 256 KB";
    case VIDEO_ADAPTER_VGA128: return "VGA 128 KB";
    default:                   return "VGA VBE 1.2 256 KB";
    }
}

const char *video_profile_config_value(void)
{
    switch (video_adapter_profile) {
    case VIDEO_ADAPTER_MCGA: return "MCGA";
    case VIDEO_ADAPTER_EGA128: return "EGA128";
    case VIDEO_ADAPTER_EGA256: return "EGA256";
    case VIDEO_ADAPTER_VGA128: return "VGA128";
    default: return "VGA256";
    }
}

int video_profile_parse(const char *value, VideoAdapterProfile *profile)
{
    VideoAdapterProfile p;
    if (!value || !profile) return 0;
    if (!strcasecmp(value, "MCGA")) p = VIDEO_ADAPTER_MCGA;
    else if (!strcasecmp(value, "EGA128") || !strcasecmp(value, "EGA")) p = VIDEO_ADAPTER_EGA128;
    else if (!strcasecmp(value, "EGA256")) p = VIDEO_ADAPTER_EGA256;
    else if (!strcasecmp(value, "VGA128")) p = VIDEO_ADAPTER_VGA128;
    else if (!strcasecmp(value, "VGA256") || !strcasecmp(value, "VGA")) p = VIDEO_ADAPTER_VGA256;
    else return 0;
    *profile = p;
    return 1;
}

int video_profile_supports_mode(uint8_t mode)
{
    if (mode <= 0x07) return 1;
    switch (video_adapter_profile) {
    case VIDEO_ADAPTER_MCGA:
        return mode == 0x11 || mode == 0x13;
    case VIDEO_ADAPTER_EGA128:
    case VIDEO_ADAPTER_EGA256:
        return mode >= 0x0D && mode <= 0x10;
    case VIDEO_ADAPTER_VGA128:
        return (mode >= 0x0D && mode <= 0x11) || mode == 0x13;
    default:
        return mode >= 0x0D && mode <= 0x13;
    }
}

#if !defined(NO_PAGING)
void video_profile_configure_memory(void)
{
    extern uint8_t gfx_buffer[];
    extern uint8_t __ram_4_ext_region_start__;

    ram_pages_size = video_profile_page_cache_size();
    if (video_profile_has_256k_vram())
        ram_pages = &__ram_4_ext_region_start__;
    else
        ram_pages = gfx_buffer + video_profile_vram_size();
}
#else
void video_profile_configure_memory(void)
{
}
#endif
