#ifndef HLE_JPEG_STREAM_H
#define HLE_JPEG_STREAM_H
struct hle_t;
int jpeg_task_pending(const struct hle_t *hle);
unsigned jpeg_state_size(void);
void jpeg_state_save(void *out);
void jpeg_state_load(const void *in);
#endif
