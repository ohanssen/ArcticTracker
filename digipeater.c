
/*
 * Digipeater
 * 
 * Stored parameters used by digipeater (can be changed in command interface)
 *    MYCALL            - my callsign
 *    DIGIPEATER_WIDE1  - true if wide1/fill-in digipeater mode. Meaning that only WIDE1 alias will be reacted on. 
 *    DIGIPEATER_SAR    - true if SAR preemption mode. If an alias SAR is found anywhere in the path, it will 
 *                        preempt others (moved first) and digipeated upon.  
 * 
 * Macros for configuration (defined in defines.h)
 *    HDLC_DECODER_QUEUE_SIZE - size (in packets) of receiving queue. Normally 7.
 *    STACK_DIGIPEATER        - size of stack for digipeater task.
 *    STACK_HLIST_TICK        - size of stack for tick_thread (for heard list).
 *   
 */

#include "defines.h"
#include "config.h"
#include "ax25.h"
#include "afsk.h"
#include "hdlc.h"
#include "ui/ui.h"
#include "radio.h"
#include "heardlist.h"
#include "digipeater.h"
#include <string.h>
   
static bool digi_on = false;
static FBQ rxqueue;
static thread_t* digithr=NULL;

extern fbq_t* outframes; 
extern fbq_t* mon_q;

static void check_frame(FBUF *f);



/***********************************************
 *  digipeater main thread 
 ***********************************************/

static THD_FUNCTION(digipeater, arg)
{
  (void) arg;
  chRegSetThreadName("Digipeater");
  sleep(4000);
  beeps("-.. "); blipUp();
  while (digi_on)
  {
    /* Wait for frame. 
     */
    FBUF frame = fbq_get(&rxqueue);
    if (fbuf_empty(&frame)) {
      fbuf_release(&frame); 
      continue;    
    }
    
    /* Do something about it */
    check_frame(&frame);
    
    /* And dispose it */
    fbuf_release(&frame);
  }
  sleep(500);
  beeps("-.. "); blipDown();
}



/**********************
 *  digipeater_init
 **********************/

void digipeater_init()
{
    FBQ_INIT(rxqueue, HDLC_DECODER_QUEUE_SIZE);
    if (GET_BYTE_PARAM(DIGIPEATER_ON))
      digipeater_activate(true);
}


/***************************************************************
 * Turn digipeater on if argument is true, turn it off
 * if false. 
 ***************************************************************/

void digipeater_on(bool m)
{
     SET_BYTE_PARAM(DIGIPEATER_ON, (m? 1:0) );
     digipeater_activate(m);
}


/***************************************************************
 * Activate the digipeater if argument is true
 * Deactivate if false
 ***************************************************************/

void digipeater_activate(bool m)
{ 
   bool tstart = m && !digi_on;
   bool tstop = !m && digi_on;
   
   digi_on = m;
   FBQ* mq = (digi_on? &rxqueue : NULL);
 
   if (tstart) {
      /* Subscribe to RX packets and start treads */
      hdlc_subscribe_rx(mq, 1);
      digithr = THREAD_DSTART(digipeater, STACK_DIGIPEATER, NORMALPRIO, NULL);  
      hlist_start();
      
      /* Turn on radio */
      radio_require();
   } 
   if (tstop) {
     /* Turn off radio */
      radio_release();
      
      /* Unsubscribe to RX packets and stop threads */
      fbq_signal(&rxqueue);
      if (digithr != NULL)
        chThdWait(digithr);
      digithr = NULL;
      hdlc_subscribe_rx(NULL, 1);
   }
}



/*******************************************************************
 * Check a frame if it is to be digipeated
 * If yes, digipeat it :)
 *******************************************************************/

static void check_frame(FBUF *f)
{
   FBUF newHdr;
   addr_t mycall, from, to; 
   addr_t digis[7], digis2[7];
   bool widedigi = false;
   uint8_t ctrl, pid;
   uint8_t i, j; 
   int8_t  sar_pos = -1;
   uint8_t ndigis =  ax25_decode_header(f, &from, &to, digis, &ctrl, &pid);
   
   if (hlist_duplicate(&from, &to, f, ndigis))
       return;
   GET_PARAM(MYCALL, &mycall);

   /* Copy items in digi-path that has digipeated flag turned on, 
    * i.e. the digis that the packet has been through already 
    */
   for (i=0; i<ndigis && (digis[i].flags & FLAG_DIGI); i++) 
       digis2[i] = digis[i];   

   /* Return if it has been through all digis in path */
   if (i==ndigis)
       return;

   /* Check if the WIDE1-1 alias is next in the list */
   if (GET_BYTE_PARAM(DIGIP_WIDE1_ON) 
           && strncasecmp("WIDE1", digis[i].callsign, 5) == 0 && digis[i].ssid == 1)
       widedigi = true; 
  
   /* Look for SAR alias in the rest of the path 
    * NOTE: Don't use SAR-preemption if packet has been digipeated by others first 
    */    
   if (GET_BYTE_PARAM(DIGIP_SAR_ON) && i<=0) 
     for (j=i; j<ndigis; j++)
       if (strncasecmp("SAR", digis[j].callsign, 3) == 0) 
          { sar_pos = j; break; } 
   
   /* Return if no SAR preemtion and WIDE1 alias not found first */
   if (sar_pos < 0 && !widedigi)
      return;
   
   /* Mark as digipeated through mycall */
   j = i;
   mycall.flags = FLAG_DIGI;
   digis2[j++] = mycall; 
   
   
   /* do SAR preemption if requested  */
   if (sar_pos > -1) 
       str2addr(&digis2[j++], "SAR", true);
 
   /* Otherwise, use wide digipeat method if requested and allowed */
   else if (widedigi) {
       i++;
       str2addr(&digis2[j++], "WIDE1", true);
   }

   /* Copy rest of the path, exept the SAR alias (if used) */
   for (; i<ndigis; i++) 
       if (sar_pos < 0 || i != sar_pos)
          digis2[j++] = digis[i];
   
   /* Write a new header -> newHdr */
   fbuf_new(&newHdr);
   ax25_encode_header(&newHdr, &from, &to, digis2, j, ctrl, pid);

   /* Replace header in original packet with new header. 
    * Do this non-destructively: Just add rest of existing packet to new header 
    */
   fbuf_connect(&newHdr, f, AX25_HDR_LEN(ndigis) );

   /* Send packet */
   beeps("- ");
   fbq_put(outframes, newHdr);  
}




