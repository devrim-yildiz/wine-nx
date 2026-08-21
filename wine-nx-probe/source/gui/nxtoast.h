/* nxtoast: a small global stacked-notification system. Any module can
 * push a short message; nxtoast_frame draws every still-active one,
 * sliding in from the top-right corner, holding, then sliding back out
 * -- pure rect-position animation (this toolkit has no alpha blending
 * on flat fills, see nxgui.h), which reads visually similar to a fade
 * without needing one.
 *
 * Call nxtoast_frame() every frame, last, after everything else has
 * drawn -- it's meant to sit on top of the whole UI, any screen.
 */
#ifndef WINE_NX_TOAST_H
#define WINE_NX_TOAST_H

#include "nxgui.h"

#define NXTOAST_MAX      4
#define NXTOAST_MSG_MAX 96

typedef struct
{
    char message[NXTOAST_MSG_MAX];
    int active;
    int age_frames;   /* counts up from 0 while active; nxtoast_frame owns this entirely */
} nxtoast_slot_t;

typedef struct
{
    nxtoast_slot_t slots[NXTOAST_MAX];
} nxtoast_state_t;

void nxtoast_init( nxtoast_state_t *state );

/* Inserts into the first free slot; if all NXTOAST_MAX are busy, evicts
 * whichever is oldest (about to disappear soonest anyway) rather than
 * dropping the new one -- a fresh notification should always win over
 * a stale one. */
void nxtoast_push( nxtoast_state_t *state, const char *message );

void nxtoast_frame( nxtoast_state_t *state, nxgui_surface_t *surf );

#endif /* WINE_NX_TOAST_H */
