//-------------------------------------------------------------------------
//-------------------------------------------------------------------------

// The raspberry pi firmware at the time this was written defaults
// loading at address 0x8000.  Although this bootloader could easily
// load at 0x0000, it loads at 0x8000 so that the same binaries built
// for the SD card work with this bootloader.  Change the ARMBASE
// below to use a different location.

#include "io.h"
#include "gpio.h"
#include "biguart.h"
#include "interrupt.h"
#include "systimer.h"
#include "stdint.h"

#define ARMBASE 0x10000

#define I_ON() AUX_MU_IER_REG |= (1<<1)
#define I_OFF() AUX_MU_IER_REG &= ~(1<<1)

extern void enable_irq ( void );
extern void disable_irq ( void );

void led_toggle(void){
    if(GPIOREAD(16)){
        GPIOCLR(16);
    }else{
        GPIOSET(16);
    }
}




/*
    NOTE: this is not a universal VIC like one might expect! Each vector number
    is tied to a specific device, so the handler registered to it *must* be the
    handler for that device!
*/

typedef void(*vect_t)(void);

#define NULL_VECT (vect_t)0
#define NUM_VECT 64
//see the table page 113 of the BCM2835 Arm Peripherals datasheet for vector numbers
#define VECT_SYSTIMERM1 1  //triggered when the system timer matches the C1 register
#define VECT_PL011      57 //triggered when the PL011 asserts IRQ status

vect_t vectors[NUM_VECT];

void vic_init(void)
{
    int i;

    for(i=0; i<NUM_VECT; i++){
        vectors[i] = NULL_VECT;
    }
}

void vic_register(int vect_num, vect_t handler)
{
    //if the index is valid
    if((vect_num >= 0) &&
       (vect_num < NUM_VECT))
    {
        //write the new handler into the vector table first so that if we
        //  enable an IRQ that's currently asserted and thus have an interrupt,
        //  we'll jump to a good address.
        vectors[vect_num] = handler;

        //enable the respective interrupt
        if(vect_num < 32)
        {
            //only 1 bits are recognized when writing to the (en/dis)able regs.
            //  using |= here could be problematic since it would likely be
            //  implemented as multiple instructions: at least a read, an or,
            //  and a write. if we interrupted _after_ the read instruction or
            //  the or instruction, and disabled certain bits in the IRQ
            //  routine, the |= would write back the old state of the enable
            //  bits. This would effectively be re-enabling interrupts that we
            //  wanted disabled.
            INTERRUPT_ENABLEIRQ1 = (1<<vect_num); //zeroes are ignored, don't use |=
        }
        else
        {
            INTERRUPT_ENABLEIRQ2 = (1<<(vect_num-32)); //zeroes are ignored, don't use |=
        }
    }
}

void vic_deregister(int vect_num)
{
    //if the index is valid
    if((vect_num >= 0) &&
       (vect_num < NUM_VECT))
    {
        //disable IRQs for this device first. otherwise we might interrupt with a
        //  NULL_VECT in the handler's address.
        if(vect_num < 32)
        {
            INTERRUPT_DISABLEIRQ1 = (1<<vect_num); //zeroes are ignored, don't use |=
        }
        else
        {
            INTERRUPT_DISABLEIRQ2 = (1<<(vect_num-32)); //zeroes are ignored, don't use |=
        }

        //write the new handler
        vectors[vect_num] = NULL_VECT;
    }
}

void c_irq_handler(void)
//void vic_irq_handler(void)
{
    unsigned int i, irqs;

    //handle all vectors in the first set of IRQs
    irqs = INTERRUPT_IRQPEND1;
    for(i=0; i<32; i++)
    {
        //if the current bit is set and there's a handler
        if((irqs & 1) && //if this IRQ has a pending interrupt
           (vectors[i] != NULL_VECT)) //just in case we interrupted a registration or deregistration
        {
            //call the handler
            vectors[i]();
        }

        //on to the next one
        irqs = (irqs>>1);
    }

    //handle all vectors in the first set of IRQs
    irqs = INTERRUPT_IRQPEND2;
    for(i=32; i<64; i++)
    {
        //if the current bit is set and there's a handler
        if((irqs & 1) && //if this IRQ has a pending interrupt
           (vectors[i] != NULL_VECT)) //just in case we interrupted a registration or deregistration
        {
            //call the handler
            vectors[i]();
        }

        //on to the next one
        irqs = (irqs>>1);
    }
}



/*
void c_irq_handler ( void )
{
    disable_irq();
    //get a copy of the IRQ pending register so we don't have to read it multiple times
    irq1s = INTERRUPT_IRQPEND1;
    irq2s = INTERRUPT_IRQPEND2;

    //determine the source of the interrupt
    if(irq1s & (1<<IRQSYSTIMERC1)) //if system timer 1 has gone off
    {
        //if so, handle it
        //GPIOSET(16); //led off
        led_toggle(); //this should make the LED blink every N seconds
        systimer_handler();
    }
    if(irq2s & (1<<(IRQUART-32))) //if the pl011 uart has gone off
    {
        //led_toggle(); //if the LED is dim, we're not ACKing interrupts
        uart_handler();
    }
    enable_irq();
}
*/

int notmain ( void )
{
    unsigned int ra, rb, rc;

    //make gpio pin tied to the led an output
    GPIOMODE(16, FSEL_OUTPUT); //led output
    GPIOCLR(16); //led on

    //initialize interrupts
    vic_init();
    vic_register(VECT_SYSTIMERM1, systimer_handler);
    vic_register(VECT_PL011, uart_handler);
    vic_deregister(VECT_SYSTIMERM1);

    //initialize hardware
    systimer_init(1000000); //go off once a second
    uartInit();
    uartPutln("Booted!");
    //uartPut32(0x12345678);

    //test the UART's interrupt bit
    /*
    iuartInit();
    GPIOSET(16); //led off
    PL011_DR = '0'; //fill the transmit FIFO
    PL011_DR = '1';
    PL011_DR = '2';
    PL011_DR = '3';
    PL011_DR = '4';
    PL011_DR = '5';
    PL011_DR = '6';
    PL011_DR = '7';
    PL011_DR = '8'; //characters past (and including) this one aren't printed...
    PL011_DR = '9';
    PL011_DR = 'A';
    PL011_DR = 'B';
    PL011_DR = 'C';
    PL011_DR = 'D';
    PL011_DR = 'E';
    PL011_DR = 'F';
    ra = INTERRUPT_IRQPEND2; //get the upper 32bits of IRQ pending
    while((rb = INTERRUPT_IRQPEND2) == ra){} //wait for IRQ pending to change
    rc = PL011_RIS; //get raw interrupt status
    uartPutln("");
    uartPut32(ra); //print the old value of IRQ pending
    uartPut32(rb); //print the new value
    uartPut32(rc); //print the PL011 interrupt status
    for(;;){} //chill
    */

    //test the UART with an interrupt
    /*
    GPIOSET(16); //led off
    iuartInit();
    enable_irq();
    PL011_DR = '1'; //fill the transmit FIFO
    PL011_DR = '2';
    PL011_DR = '3';
    PL011_DR = '4';
    PL011_DR = '5';
    PL011_DR = '6';
    PL011_DR = '7';
    PL011_DR = '8';
    for(;;){} //only do interrupts
    */

    //test interrupts fully
    GPIOSET(16); //led off
    iuartInit();
    enable_irq();
    //PL011_DR = '?';
    for(;;){
        iuartPutln("Interrupts!");
        if(ra != (rb = systimer_get()))
        {
            ra = rb;
            led_toggle();
        }
    }

    //test system timer AND UART
    /*
    while(1)
    {
        iuartPutln("Interrupts!");
    }*/
    /*
        while(systimer_get() == ra) continue;
        ra = systimer_get();

        GPIOCLR(16); //LED on

        while(systimer_get() == ra) continue;
        ra = systimer_get();

        GPIOSET(16); //LED off
    }*/


    return(0);
}
//-------------------------------------------------------------------------
//-------------------------------------------------------------------------


//-------------------------------------------------------------------------
//
// Copyright (c) 2012 David Welch dwelch@dwelch.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------
