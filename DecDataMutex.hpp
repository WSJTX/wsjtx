#ifndef DEC_DATA_MUTEX_HPP
#define DEC_DATA_MUTEX_HPP

class QMutex;

QMutex& dec_data_mutex();
bool dec_data_input_blocked ();
void set_dec_data_input_blocked (bool blocked);

#endif
