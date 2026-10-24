

#ifndef __SAVESTAVES_H__
#define __SAVESTAVES_H__

struct device;
int core_state_save(const struct device *dev, void *data);
int core_state_load(struct device *dev, const void *data);
#endif
