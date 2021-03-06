/*
 * AFSK modulation for transmitter. 
 * 
 * Adapted from Polaric Tracker code. 
 * By LA7ECA, ohanssen@acm.org and LA3T.
 */

#include "ch.h"
#include "hal.h"
#include "radio.h"
#include "afsk.h"
#include "defines.h"


#define CLOCK_FREQ 1200



static uint8_t _buf[AFSK_TX_QUEUE_SIZE];
static output_queue_t oq; 


static bool transmit = false; 


output_queue_t* afsk_tx_init()
{
  oqObjectInit(&oq, _buf, AFSK_TX_QUEUE_SIZE, NULL, NULL);
  return &oq;
}


/*********************************************************************
 * Turn on/off transmitter and tone generator
 *********************************************************************/

void afsk_PTT(bool on) {
   transmit = on; 
   radio_PTT(on);
   if (on) 
      tone_start();
   else
      tone_stop();
}



/**************************************************************************
 * Get next bit from stream
 * Note: see also get_bit() in hdlc_decoder.c 
 * Note: The next_byte must ALWAYS be called before get_bit is called
 * to get new bytes from stream. 
 *************************************************************************/

static uint8_t bits;
static uint8_t bit_count = 0;

static uint8_t get_bit(void)
{
   if (bit_count == 0) 
     return 0;
   uint8_t bit = bits & 0x01;
   bits >>= 1;
   bit_count--;
   return bit;
}


static void next_byte(void)
{
   if (bit_count == 0) 
   {
      /* Turn off TX if queue is empty (have reached end of frame) */
      if (oqIsEmptyI(&oq)) { 
         afsk_PTT(false);  
         return;
      }
      bits = oqGetI(&oq); 
      bit_count = 8;    
   } 
}



/*******************************************************************************
 * If transmitting, this function should be called periodically, 
 * at same rate as wanted baud rate.
 *
 * It is responsible for transmitting frames by toggling the frequency of
 * the tone generated by the timer handler below. 
 *******************************************************************************/ 

static void afsk_txBitClock(GPTDriver *gptp) {
     (void)gptp;
     
     if (!transmit) {
       if (oqIsEmptyI(&oq))
         return;
       else {
         /* If bytes in queue, start transmitting */
         next_byte();
         afsk_PTT(true);
       }
     }       
     if ( ! get_bit() ) 
       /* Toggle TX frequency */ 
       tone_toggle(); 
     
     /* Update byte from stream if necessary. We do this 
      * separately, after the get_bit, to make the timing more precise 
      */  
     next_byte();  
}
 

 
 /* 
  * We may either set the main system tick frequency to 1200 (or a multiple) or
  * set up a separate GPT timer. 
  */
 
 static const GPTConfig bitclock_cfg = {
   CLOCK_FREQ,  
   afsk_txBitClock   /* Timer callback.*/
 };
 
 
 
 /***********************************************************
  *  Start transmitter.
  ***********************************************************/
 
 void afsk_tx_start() {
   gptStart(&AFSK_TX_GPT, &bitclock_cfg);
   gptStartContinuous(&AFSK_TX_GPT, 1);  
 }
 
 
 
 /***********************************************************
  *  Stop transmitter.
  ***********************************************************/
 
 void afsk_tx_stop() {
   gptStopTimer(&AFSK_TX_GPT);
   gptStop(&AFSK_TX_GPT);
 }
 
 
 