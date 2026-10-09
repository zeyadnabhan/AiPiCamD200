/* d200_cam.h - low level C API of the AiPi-Cam-D200 camera */
#ifndef D200_CAM_H
#define D200_CAM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int d200_cam_begin(uint8_t quality);            /* 0 = ok, -1 = sensor not found */
const char *d200_cam_sensor_name(void);
uint16_t d200_cam_width(void);
uint16_t d200_cam_height(void);
uint32_t d200_cam_frame_count(void);
uint16_t d200_cam_sensor_id(void);              /* 0x2145 when GC2145 answered */

/* Lock the newest JPEG frame newer than *seq (waits up to timeout_ms).
   Returns 1 and fills buf/len/seq. Release it with d200_cam_unlock(buf). */
int d200_cam_lock(const uint8_t **buf, uint32_t *len, uint32_t *seq, uint32_t timeout_ms);
void d200_cam_unlock(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif
