#ifndef R10_REENT_H
#define R10_REENT_H
struct _reent {int _errno;};
extern struct _reent *_impure_ptr;
void _reclaim_reent(struct _reent *);
#define _REENT_INIT_PTR(p) ((void)(p))
#define _REENT_INIT_PTR_ZEROED(p) ((void)(p))
#define _REENT_CHECK_MP(p) ((void)(p))
#define _REENT_CHECK_RAND48(p) ((void)(p))
#define _REENT_CHECK_MISC(p) ((void)(p))
#define _REENT_CHECK_EMERGENCY(p) ((void)(p))
#define _REENT_CHECK_STDIO(p) ((void)(p))
#define _REENT_CHECK_CLEANUP(p) ((void)(p))
#define _REENT_CHECK_SIGNALS(p) ((void)(p))

#endif
