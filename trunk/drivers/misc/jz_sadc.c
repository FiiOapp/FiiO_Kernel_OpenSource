#include <linux/init.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <linux/jz_sadc.h>

#include <asm/irq.h>
#include <asm/gpio.h>
#include <asm/jzsoc.h>

#define TS_NAME "jz-sdac"

void sadc_init_clock(void)
{
	unsigned int val;
    int div;
	div = 120 - 1;	/* working at 200 KHz */
	val = div  | (1 << 8) | (199 << 16);
	printk("val = 0x%x\n",val);
	OUTREG32(SADC_ADCLK, val);
	//CMSREG32(SADC_ADCLK, div, ADCLK_CLKDIV_MASK);
	//*(unsigned int *)0xb0070028 = val;

}
/************************************************************************/
/*	 sadc interrupt							*/
/************************************************************************/

struct sadc_sub_irq{
    int irq;
    irqreturn_t (*func)(int irq, void * dev_id);
    void * data;
};


struct sadc_sub_irq sadc_sub_irq[MAX_NUM_IRQ];
void init_sadc_sub_irq(void){
    int i;
    for(i=0;i<MAX_NUM_IRQ;++i){
        sadc_sub_irq[i].irq = -1;
        sadc_sub_irq[i].func = NULL;
        sadc_sub_irq[i].data = NULL;
    }
}

int sadc_request_irq(unsigned int irq,irqreturn_t (*func)(int irq, void * dev_id),void *data){
    if(sadc_sub_irq[irq].func  == NULL){
        sadc_sub_irq[irq].irq = irq;
        sadc_sub_irq[irq].func = func;
        sadc_sub_irq[irq].data = data;
        return 0;               //success
    }
        return -1;             //fail
}
int sadc_free_irq(unsigned int irq){
    if(sadc_sub_irq[irq].func != NULL){
        sadc_sub_irq[irq].irq = -1;
        sadc_sub_irq[irq].func = NULL;
        sadc_sub_irq[irq].data = NULL;
        return 0;               //success  
    }
        return -1;             //fail
}
irqreturn_t sadc_interrupt(int irq, void * dev_id)
{
	unsigned int state;
	irqreturn_t (*func)(int irq, void * dev_id);
    state = INREG8(SADC_ADSTATE) & (~INREG8(SADC_ADCTRL));
    
	if(state & ADSTATE_PENU){
        func =sadc_sub_irq[TS_PENUP_IRQ].func;
        if(func != NULL){
            return func(TS_PENUP_IRQ,sadc_sub_irq[TS_PENUP_IRQ].data);    
        }
    }else if(state & ADSTATE_PEND){
        func =sadc_sub_irq[TS_PENDOWN_IRQ].func;
        if(func != NULL){
            return func(TS_PENDOWN_IRQ,sadc_sub_irq[TS_PENDOWN_IRQ].data);    
        }
	}else if(state & ADSTATE_DTCH){
        func =sadc_sub_irq[TS_DATA_READY_IRQ].func;
        if(func != NULL){
            return func(TS_DATA_READY_IRQ,sadc_sub_irq[TS_DATA_READY_IRQ].data);    
        }
    }else if(state & ADSTATE_SLPEND){
        func =sadc_sub_irq[TS_SLPEND_IRQ].func;
        if(func != NULL){
            return func(TS_SLPEND_IRQ,sadc_sub_irq[TS_SLPEND_IRQ].data);    
        }
	}else if(state & ADSTATE_VRDY){
        func =sadc_sub_irq[BAT_DATA_READY_IRQ].func;
        if(func != NULL){
            return func(BAT_DATA_READY_IRQ,sadc_sub_irq[BAT_DATA_READY_IRQ].data);    
        }
	}
    return IRQ_HANDLED;
}

/************************************************************************/
/*	init sadc							*/
/************************************************************************/
static int __init sadc_init(void)
{
    int	error;
	cpm_start_clock(CGM_SADC);                                                                                          //start sadc clock
	udelay(1);
	
#if defined(CONFIG_SOC_JZ4760)
	CLRREG8(SADC_ADENA, ADENA_POWER);
	CLRREG32(CPM_LCR, LCR_VBATIR);
	mdelay(50);
#elif defined(CONFIG_SOC_JZ4760B) || defined(CONFIG_SOC_JZ4770)
	CLRREG8(SADC_ADENA, ADENA_POWER);                                                                 //sadc power on
	mdelay(50);                                                                                                                          //wait sadc power on
	SETREG8(SADC_ADENA, ADENA_PENDEN);                                                               //enable pen down detect
#endif
    
	sadc_init_clock();                                                                                                              //init sadc clock
	OUTREG8(SADC_ADCTRL, ADCTRL_MASK_ALL);                                                      //mask all interrupt
    
	error = request_irq(IRQ_SADC, sadc_interrupt, IRQF_DISABLED, TS_NAME, NULL);
	if (error) {
		printk("unable to get PenDown IRQ %d\n", IRQ_SADC);
		goto err_free_irq;
	}
    return 0;
    
err_free_irq:
	free_irq(IRQ_SADC,NULL);
    
    return 0;
}
static void __exit sadc_exit(void)
{
}
subsys_initcall(sadc_init);
module_exit(sadc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("JZ SADC Core Driver");
MODULE_AUTHOR(" <@ingenic.com>");

