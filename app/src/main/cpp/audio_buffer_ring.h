#ifndef AUDIO_BUFFER_RING_H
#define AUDIO_BUFFER_RING_H 
#include <stddef.h>

typedef struct audio_buffer_ring_t audio_buffer_ring_t;

struct audio_buffer_ring_init_t {
    size_t n_bufs;
    size_t buf_sz;
    size_t align;
};
typedef struct audio_buffer_ring_init_t audio_buffer_ring_init_t;

size_t
audio_buffer_ring_sz(audio_buffer_ring_init_t *init);

int
audio_buffer_ring_init(audio_buffer_ring_t *rng, 
        audio_buffer_ring_init_t *init);

void *
audio_buffer_ring_get_cur_dat(audio_buffer_ring_t *rng);

void
audio_buffer_ring_rotate(audio_buffer_ring_t *rng);

#endif /* AUDIO_BUFFER_RING_H */
