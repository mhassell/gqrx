#include <gnuradio/io_signature.h>
#include "shm_iq_sink.hpp"
#include "shm_iq_sink_impl.h"

shm_iq_sink::sptr
shm_iq_sink::make(unsigned long sample_rate)
{
    return boost::make_shared<shm_iq_sink_impl>(sample_rate);
}
