/* Initial MOS build configuration; compiler backend is unchanged. */
#ifndef TCC_MURM386_CONFIG_H
#define TCC_MURM386_CONFIG_H
#define TCC_VERSION "0.9.27-murm386"
#ifndef ONE_SOURCE
#define ONE_SOURCE 1
#endif
#if defined(__arm__)
#define TCC_MURM386 1
#endif
#define TCC_TARGET_ARM_THUMB 1
#define TCC_ARM_EABI 1
#define TCC_ARM_VFP 1
#define CONFIG_TCC_STATIC 1
#define CONFIG_TCCDIR "/TCC/LIB"
#define CONFIG_TCC_SYSINCLUDEPATHS "/TCC/INCLUDE"
#define CONFIG_TCC_LIBPATHS "/TCC/LIB"
#endif
