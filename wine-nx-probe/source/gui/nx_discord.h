/* nx_discord: the backend half of the settings screen's Discord Rich
 * Presence panel -- STUBS, ON PURPOSE, RIGHT NOW.
 *
 * Discord's Rich Presence works by IPC to a Discord client process
 * running on the SAME machine as the game (a named pipe on Windows/
 * Mac, a unix domain socket on Linux, or Discord's GameSDK wrapping
 * the same thing). Horizon OS has no Discord client and never will --
 * there is nothing local for these calls to reach. This is not a
 * missing-feature gap the way, say, Zink or D3D backends are (see
 * launcher_main.c's resolve_runtime_nro comment) -- it's a fact about
 * the platform.
 *
 * The real, buildable path (confirmed with the user, deliberately not
 * built as part of this GUI pass) is a relay: this launcher POSTs its
 * current state over the local network to a small companion process
 * running on the user's PC, and THAT process holds the real Discord
 * IPC connection (e.g. via `pypresence` in Python) and forwards state
 * updates to it. nx_discord_update_rpc()'s signature is shaped for
 * exactly that swap-in later -- an HTTP POST to a configured relay URL
 * instead of the current no-op -- without the settings screen or
 * anything else calling it needing to change at all.
 *
 * Every function below is safe to call at any time, whether or not
 * Discord integration is enabled in settings; they just don't do
 * anything observable yet.
 */
#ifndef WINE_NX_DISCORD_H
#define WINE_NX_DISCORD_H

/* Returns 0 on success. Currently always returns -1 (not implemented)
 * -- see this file's own header comment. */
int nx_discord_init( void );

void nx_discord_shutdown( void );

/* `state` is a short human-readable string, e.g. "IN THE LAUNCHER" or
 * a game's title -- exactly what would show as the second line of a
 * real Discord presence card. Returns 0 on success; currently always
 * returns -1 without doing anything. */
int nx_discord_update_rpc( const char *state );

/* Always 0 right now -- there is no connection to be up. Kept as a
 * real query (not hardcoded at call sites) so the settings screen's
 * CONNECTED/NOT CONNECTED badge is already correct the moment this
 * file's internals change, with no UI-side edit needed. */
int nx_discord_is_connected( void );

#endif /* WINE_NX_DISCORD_H */
