#include "audio_buffer_ring.h"

/* Round a size to an integer multiple of size a */
#define _ROUND_ALIGN(s, a) (((s) / (a) + (((s) % (a)) != 0)) * (a))

typedef struct _ring_member_t _ring_member_t;
struct _ring_member_t
{
    _ring_member_t* next;
    void* dat;
};

struct audio_buffer_ring_t
{
    _ring_member_t* cur;
};

size_t
audio_buffer_ring_sz(audio_buffer_ring_init_t* init)
{
    size_t sz = _ROUND_ALIGN(sizeof(audio_buffer_ring_t), init->align);
    sz += _ROUND_ALIGN(init->n_bufs * sizeof(_ring_member_t), init->align);
    sz += init->n_bufs * _ROUND_ALIGN(init->buf_sz, init->align);
    return sz;
}

int
audio_buffer_ring_init(audio_buffer_ring_t* rng, audio_buffer_ring_init_t* init)
{
    char* ptr =
      (char*)rng + _ROUND_ALIGN(sizeof(audio_buffer_ring_t), init->align);
    char* ptrdat =
      ptr + _ROUND_ALIGN(init->n_bufs * sizeof(_ring_member_t), init->align);
    size_t nbufs = init->n_bufs;
    rng->cur = (_ring_member_t*)ptr;
    _ring_member_t* last = rng->cur;
    while (nbufs--) {
        last->next = (_ring_member_t*)(ptr + sizeof(_ring_member_t));
        last->dat = ptrdat;
        if (nbufs) {
            last = last->next;
            ptr = (char*)last;
            ptrdat += _ROUND_ALIGN(init->buf_sz, init->align);
        }
    }
    /* Make into ring */
    last->next = rng->cur;
    return 0;
}

void*
audio_buffer_ring_get_cur_dat(audio_buffer_ring_t* rng)
{
    return rng->cur->dat;
}

void
audio_buffer_ring_rotate(audio_buffer_ring_t *rng)
{
    rng->cur = rng->cur->next;
}
