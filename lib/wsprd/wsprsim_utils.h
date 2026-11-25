#ifndef WSPRSIM_UTILS_H
#define WSPRSIM_UTILS_H

/* The channel symbols for an 11-byte packed message (data with tail). */
void wspr_channel_symbols_from_data(unsigned char const *data, unsigned char *symbols);

#endif
