#ifndef N64EMU_UNIT_SUPPORT_H
#define N64EMU_UNIT_SUPPORT_H
#include "headless.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "core/storage.h"
#include "core/clock.h"
struct test_storage {uint8_t* bytes;size_t length,start,saved;unsigned saves;};
static uint8_t* test_data(const void* p){return ((const struct test_storage*)p)->bytes;}
static size_t test_size(const void* p){return ((const struct test_storage*)p)->length;}
static void test_save(void* p,size_t start,size_t length){struct test_storage* s=p;assert(start<=s->length && length<=s->length-start);s->start=start;s->saved=length;++s->saves;}
static const struct storage_backend_interface test_storage_api={test_data,test_size,test_save};
static time_t test_time(void* p){return *(time_t*)p;}
static const struct clock_backend_interface test_clock_api={test_time};
#endif
