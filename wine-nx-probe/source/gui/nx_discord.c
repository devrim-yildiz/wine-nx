/* See nx_discord.h for why every function here is a deliberate no-op
 * right now. */
#include "nx_discord.h"

int nx_discord_init( void )
{
    return -1;
}

void nx_discord_shutdown( void )
{
}

int nx_discord_update_rpc( const char *state )
{
    (void)state;
    return -1;
}

int nx_discord_is_connected( void )
{
    return 0;
}
