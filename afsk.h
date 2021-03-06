/*
 * Adapted from Polaric Tracker code. 
 * By LA7ECA, ohanssen@acm.org
 */

#if !defined __AFSK_H__
#define __AFSK_H__

 #include "ch.h"
 #include "hal.h"
 
 #define AFSK_MARK  1200
 #define AFSK_SPACE 2200
 
 void tone_setHigh(bool hi);
 void tone_toggle(void);
 void tone_start(void);
 void tone_stop(void);
 
 output_queue_t* afsk_tx_init(void);
 void afsk_tx_start(void);
 void afsk_tx_stop(void);
 void afsk_PTT(bool on);
 
 input_queue_t* afsk_rx_init(void);
 void afsk_rx_enable(void); 
 void afsk_rx_disable(void);
 
 void trx_sq_handler(EXTDriver *extp, expchannel_t channel);

 
#endif