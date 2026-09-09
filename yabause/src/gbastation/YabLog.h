#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void YabLogInit(const char* path);
void YabLogWrite(const char* fmt, ...);
void YabLogShutdown(void);
#ifdef __cplusplus
}
#endif
