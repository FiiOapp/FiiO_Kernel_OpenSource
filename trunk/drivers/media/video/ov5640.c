#include <linux/i2c.h>
#include <media/jz_cim.h>
#include <asm/jzsoc.h>
#include "ov5640.h"

#define CIM_SENSOR_NAME	"ov5640"

#ifdef OV5640_DEBUG
#define dprintk(x...)   do{printk("OV5640---\t");printk(x);printk("\n");}while(0)
#else
#define dprintk(x...)
#endif
int download_af_fw;
struct workqueue_struct *work_queue;
struct delayed_work work;
struct timer_list timer;

int AE_Target = 52;
int AE_high, AE_low;
int m_iCombo_NightMode = 0;
static struct resolution_info ov5640_resolution_table[] = {
        { 2592, 1944, 16 },  
	{ 1920, 1080, 16 },
	{ 1280, 720,  16 },
	{ 640,	480,  16 },
	{ 320,	240,  16}
};

static u16 all_regs[] = {
	0x3000,
	0x3002,
	0x3004,
	0x3006,
	0x3008,
	0x300e,
	0x3017,
	0x3018,
	0x3022,
	0x3023,
	0x302c,
	0x302d,
	0x302e,
	0x3031,
	0x3034,
	0x3035,
	0x3036,
	0x3037,
	0x3038,
	0x3039,
	0x3100,
	0x3103,
	0x3108,
	0x3212,
	0x3503,
	0x3600,
	0x3601,
	0x3612,
	0x3618,
	0x3620,
	0x3621,
	0x3622,
	0x3630,
	0x3631,
	0x3632,
	0x3633,
	0x3634,
	0x3635,
	0x3636,
	0x3703,
	0x3704,
	0x3705,
	0x3708,
	0x3709,
	0x370b,
	0x370c,
	0x3715,
	0x3717,
	0x371b,
	0x3731,
	0x3800,
	0x3801,
	0x3802,
	0x3803,
	0x3804,
	0x3805,
	0x3806,
	0x3807,
	0x3808,
	0x3809,
	0x380a,
	0x380b,
	0x380c,
	0x380d,
	0x380e,
	0x380f,
	0x3810,
	0x3811,
	0x3812,
	0x3813,
	0x3814,
	0x3815,
	0x3820,
	0x3821,
	0x3824,
	0x3901,
	0x3905,
	0x3906,
	0x3a00,
	0x3a02,
	0x3a03,
	0x3a08,
	0x3a09,
	0x3a0a,
	0x3a0b,
	0x3a0d,
	0x3a0e,
	0x3a0f,
	0x3a10,
	0x3a11,
	0x3a13,
	0x3a14,
	0x3a15,
	0x3a18,
	0x3a19,
	0x3a1b,
	0x3a1e,
	0x3a1f,
	0x3c04,
	0x3c05,
	0x3c06,
	0x3c07,
	0x3c08,
	0x3c09,
	0x3c0a,
	0x3c0b,
	0x4001,
	0x4004,
	0x4005,
	0x4050,
	0x4051,
	0x4202,
	0x4300,
	0x4407,
	0x440e,
	0x460b,
	0x460c,
	0x4713,
	0x471c,
	0x4740,
	0x4837,
	0x5000,
	0x5001,
	0x501f,
	0x5025,
	0x5180,
	0x5181,
	0x5182,
	0x5183,
	0x5184,
	0x5185,
	0x5186,
	0x5187,
	0x5188,
	0x5189,
	0x518a,
	0x518b,
	0x518c,
	0x518d,
	0x518e,
	0x518f,
	0x5190,
	0x5191,
	0x5192,
	0x5193,
	0x5194,
	0x5195,
	0x5196,
	0x5197,
	0x5198,
	0x5199,
	0x519a,
	0x519b,
	0x519c,
	0x519d,
	0x519e,
	0x5300,
	0x5301,
	0x5302,
	0x5303,
	0x5304,
	0x5305,
	0x5306,
	0x5307,
	0x5309,
	0x530a,
	0x530b,
	0x530c,
	0x5381,
	0x5382,
	0x5383,
	0x5384,
	0x5385,
	0x5386,
	0x5387,
	0x5388,
	0x5389,
	0x538a,
	0x538b,
	0x5480,
	0x5481,
	0x5482,
	0x5483,
	0x5484,
	0x5485,
	0x5486,
	0x5487,
	0x5488,
	0x5489,
	0x548a,
	0x548b,
	0x548c,
	0x548d,
	0x548e,
	0x548f,
	0x5490,
	0x5580,
	0x5583,
	0x5584,
	0x5585,
	0x5586,
	0x5589,
	0x558a,
	0x558b,
	0x5688,
	0x5689,
	0x568a,
	0x568b,
	0x568c,
	0x568d,
	0x568e,
	0x568f,
	0x5800,
	0x5801,
	0x5802,
	0x5803,
	0x5804,
	0x5805,
	0x5806,
	0x5807,
	0x5808,
	0x5809,
	0x580a,
	0x580b,
	0x580c,
	0x580d,
	0x580e,
	0x580f,
	0x5810,
	0x5811,
	0x5812,
	0x5813,
	0x5814,
	0x5815,
	0x5816,
	0x5817,
	0x5818,
	0x5819,
	0x581a,
	0x581b,
	0x581c,
	0x581d,
	0x581e,
	0x581f,
	0x5820,
	0x5821,
	0x5822,
	0x5823,
	0x5824,
	0x5825,
	0x5826,
	0x5827,
	0x5828,
	0x5829,
	0x582a,
	0x582b,
	0x582c,
	0x582d,
	0x582e,
	0x582f,
	0x5830,
	0x5831,
	0x5832,
	0x5833,
	0x5834,
	0x5835,
	0x5836,
	0x5837,
	0x5838,
	0x5839,
	0x583a,
	0x583b,
	0x583c,
	0x583d,
};

struct i2c_client * g_client = NULL;

void dump_all_regs(struct i2c_client *client)
{
	u8 val;
	int i;

	printk("============ov5640 dump_all_regs start===============\n");
	for (i = 0; i < sizeof(all_regs) / sizeof(u16); i++) {
		val = sensor_read_reg16(client, all_regs[i]);
		printk("===>reg[0x%04x] = 0x%02x\n", all_regs[i], val);
	}
	printk("============ov5640 dump_all_regs end===============\n");
}

void __dump_all_regs(void) {
	if (g_client)
		dump_all_regs(g_client);
}

int ov5640_reg_writes(struct i2c_client *client, const struct ov5640_reg reglist[])
{
        int err = 0;
        int i = 0;
        while (reglist[i].reg != 0xffff) {
		err = sensor_write_reg16(client, reglist[i].reg, reglist[i].val);
                if (err)
                        return err;
                i++;
        }
        return 0;
}

static void init_set(struct i2c_client *client)
{
	/*** VGA preview (640X480) 30fps 24MCLK input ***********/

        int i = 2;
	printk("%s %s()\n", __FILE__, __func__);
	sensor_write_reg16(client,ov5640_init_table[0].reg,ov5640_init_table[0].val);
   	sensor_write_reg16(client,ov5640_init_table[1].reg,ov5640_init_table[1].val);
        mdelay(10);
	while(ov5640_init_table[i].reg != 0xffff){
		sensor_write_reg16(client,ov5640_init_table[i].reg,ov5640_init_table[i].val);	
		i++;
	}
	
	
}

#if 0
static void capture_reg_set(struct i2c_client *client)
{
}
#endif

int ov5640_set_effect(struct i2c_client *client)
{
        
	printk("%s %s()\n", __FILE__, __func__);
	ov5640_reg_writes(client, ov5640_effect_normal_regs);
	return 0;
}
int ov5640_set_balance(struct i2c_client *client)
{
	
	printk("%s %s()\n", __FILE__, __func__);
	ov5640_reg_writes(client, ov5640_wb_auto_regs);
	return 0;
}
int ov5640_af_init(struct i2c_client *client)
{
        
	printk("%s %s()\n", __FILE__, __func__);
	ov5640_reg_writes(client, ov5640_af_regs);
	printk("%s %s()\n", __FILE__, __func__);
        return 0;
}
void ov5640_late_work(struct i2c_client *client)
{
        //download auto-focus firmware
	if(download_af_fw){
                ov5640_af_init(client);
                //dev_info(&s->client->dev, "%s\n", __func__);
        }
}

int ov5640_set_focus(struct i2c_client *client)
{
	
	int i = 60;    
	u8 reg_value = 0;
	sensor_write_reg16(client, 0x3023, 0x01); 
	sensor_write_reg16(client, 0x3022, 0x03); 
   	while(i > 0){
		reg_value = sensor_read_reg16(client, 0x3023);
		if (reg_value == 0)
                        break;
		msleep(50);     //50ms
                i--;
        }
	//printk("Reg(0x3023)=0x%02x, i=%d\n", reg_value, i);
        return 0;
}
int ov5640_set_AE_target(struct i2c_client *client, int target)
{
	// stable in high
	int fast_high, fast_low;

	AE_low = target * 23 / 25; // 0.92
	AE_high = target * 27 / 25; // 1.08

	fast_high = AE_high<<1;
	if(fast_high>255)
		fast_high = 255;
	fast_low = AE_low>>1;

	sensor_write_reg16(client, 0x3a0f, AE_high);
	sensor_write_reg16(client, 0x3a10, AE_low);
	sensor_write_reg16(client, 0x3a1b, AE_high);
	sensor_write_reg16(client, 0x3a1e, AE_low);
	sensor_write_reg16(client, 0x3a11, fast_high);
	sensor_write_reg16(client, 0x3a1f, fast_low);
	return 0;
}

void ov5640_set_night_mode(struct i2c_client *client, int mode)
{
	int val;

	if (mode != 0) {
		//turn on night mode
		val = sensor_read_reg16(client, 0x3a00);
		val |= 0x04;
		sensor_write_reg16(client, 0x3a00, val);
	} else {
		//turn off night mode
		val = sensor_read_reg16(client, 0x3a00);
		val &= ~0x04;
		sensor_write_reg16(client, 0x3a00, val);
	}
}

static void preview_set(struct i2c_client *client)
{
	ov5640_reg_writes(client,ov5640_preview_set);
//	ov5640_set_AE_target(client,AE_Target);
//	ov5640_set_night_mode(client,m_iCombo_NightMode);
//	ov5640_af_init(client);
	ov5640_set_focus(client);
	g_client = client;	//twxie
}

static void size_switch(struct i2c_client *client, int width, int height)
{

	if ((width == 320) && (height == 240)) {
		ov5640_reg_writes(client, ov5640_qvga_regs);
	} else if ((width == 640) && (height == 480)) {
		ov5640_reg_writes(client, ov5640_vga_regs);
	} else if ((width == 1280) && (height == 720)) {
		ov5640_reg_writes(client, ov5640_720p_regs);
	} else if ((width == 1920) && (height == 1080)) {
		ov5640_reg_writes(client, ov5640_1080p_regs);
	} else if ((width == 2592) && (height == 1944)) {
		ov5640_reg_writes(client, ov5640_5mp_regs);
	}
}
static void capture_set(struct i2c_client *client)
{
	ov5640_reg_writes(client,ov5640_preview_set);
}

static void ov5640_power_down(int pd_pin)
{
	/* Power Down: active high */
#if defined(CONFIG_JZ4760_LEPUS) || defined(CONFIG_JZ4760B_LEPUS)
	__gpio_as_output(pd_pin);
	__gpio_set_pin(pd_pin);
#elif defined(CONFIG_JZ4770_PISCES)|| defined(CONFIG_JZ4775_MENSA)
	__gpio_as_output1(pd_pin);
#elif defined(CONFIG_JZ4750D_CETUS)                                                                      
       __gpio_as_output(pd_pin);                                                                       
       __gpio_set_pin(pd_pin);
#endif	
	mdelay(5);
}

static void ov5640_power_up(int pd_pin)
{
#if defined(CONFIG_JZ4760_LEPUS) || defined(CONFIG_JZ4760B_LEPUS)
	__gpio_as_output(pd_pin);
	__gpio_clear_pin(pd_pin);
#elif defined(CONFIG_JZ4770_PISCES)|| defined(CONFIG_JZ4775_MENSA)
	__gpio_as_output0(pd_pin);
#elif defined(CONFIG_JZ4750D_CETUS)                                                                       
        __gpio_as_output(pd_pin);                                                                      
        __gpio_clear_pin(pd_pin);
#endif	
	mdelay(5);
}

static int ov5640_set_mclk(unsigned int mclk)
{
	/*
	 * It has a 24MHz oscillator to supply Mclk on RD47xx_Camera_Board.
	 */

	// __cim_set_master_clk(__cpm_get_hclk(), c->mclk);
	return 0;
}

static void ov5640_reset(int rst_pin)
{
	
	/* Reset: active-low */
#if defined(CONFIG_JZ4770_F4770) || defined(CONFIG_JZ4770_PISCES) || defined(CONFIG_JZ4775_F4775) || defined(CONFIG_JZ4780_F4780) || defined(CONFIG_JZ4775_MENSA)
	__gpio_as_output0(rst_pin);
	mdelay(50);
	__gpio_as_output1(rst_pin);
	mdelay(50);
#elif defined(CONFIG_JZ4750D_CETUS)
	__gpio_clear_pin(rst_pin);                                                                
        mdelay(50);                                                                                       
        __gpio_set_pin(rst_pin);                                                                  
        mdelay(50);      
#else
	__gpio_clear_pin(rst_pin);
	mdelay(50);
	__gpio_set_pin(rst_pin);
	mdelay(50);
#endif
}

static int jz_cim_set_foucs(struct jz_sensor_desc *desc)
{
	ov5640_set_focus(desc->client);
	return 0;
}
static int ov5640_set_power(struct jz_sensor_desc *desc, int state)
{
	switch (state)
	{
		case 0:
			ov5640_power_up(desc->pd_pin);
			break;
		case 1:
			ov5640_power_down(desc->pd_pin);
			break;
		case 2:
			break;
		default:
			printk("%s : invalid state %d! \n", __func__, state);
			return -EINVAL;
	}
	return 0;
}

static int ov5640_sensor_init(struct jz_sensor_desc *desc)
{
	
	ov5640_set_mclk(desc->mclk);
	ov5640_reset(desc->rst_pin);
	init_set(desc->client);
	download_af_fw = 1;
     //   queue_delayed_work(work_queue, &work, HZ/2);
	
	ov5640_set_balance(desc->client);
	ov5640_set_effect(desc->client);
	
	ov5640_af_init(desc->client);
	ov5640_set_focus(desc->client);
	
	return 0;
}

static int ov5640_sensor_probe(struct jz_sensor_desc *desc)
{
	u8 chipid_high = 0;
	u8 chipid_low = 0;
	u8 revision = 0;
	ov5640_power_up(desc->pd_pin);
	ov5640_reset(desc->rst_pin);
	mdelay(10);

	chipid_high = sensor_read_reg16(desc->client, 0x300a);	//read product id MSBs
	chipid_low = sensor_read_reg16(desc->client, 0x300b);	//read product id MSBs
	revision = sensor_read_reg16(desc->client, 0x302a);	//read product id MSBs
	ov5640_power_down(desc->pd_pin);

	printk("%s L%d: chipid_high = 0x%02x,chipid_low = 0x%02x,revision = 0x%02x\n", __func__, __LINE__, chipid_high,chipid_low,revision);

	if((chipid_high != 0x56)||(chipid_low != 0x40)) {
		printk("==>%s: error! chipid is 0x%02x%02x, should be 0x5640!\n", __func__, chipid_high,chipid_low);
		return -1;
	}

	return 0;
}

static int ov5640_set_resolution(struct jz_sensor_desc *desc, struct resolution_info *res)
{
	
	size_switch(desc->client, res->width, res->height);
	return 0;
}

static int ov5640_set_function(struct jz_sensor_desc *desc, enum sensor_mode_t mode)
{
	
	switch (mode)
	{
	case SENSOR_MODE_PREVIEW:
		preview_set(desc->client);
		break;
	case SENSOR_MODE_CAPTURE:
		capture_set(desc->client);
		break;
	default:
		printk("%s: invalid function!\n", __func__);
		break;
	}

	return 0;
}

static struct jz_sensor_ops ov5640_sensor_ops = {
	.sensor_init		= ov5640_sensor_init,
	.sensor_probe		= ov5640_sensor_probe,
	.sensor_set_resolution	= ov5640_set_resolution,
	.sensor_set_power	= ov5640_set_power,
	.sensor_set_function	= ov5640_set_function,
	.sensor_set_foucs	= jz_cim_set_foucs,	//zwu
};

static int ov5640_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct jz_sensor_desc *desc = NULL;
	struct jz_sensor_platform_data *priv = client->dev.platform_data;

	printk("\n\n=====ov5640_probe=====\n\n");
	if (!id)
		return -EINVAL;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		printk(KERN_ERR "%s: kzalloc failed!\n", __func__);
		return -ENOMEM;
	}
	desc->client = client;
	strcpy(desc->name, id->name);
#if 0
	INIT_DELAYED_WORK(&work, ov5640_late_work);
	work_queue = create_singlethread_workqueue("ov5640_work_queue");
        if (work_queue == NULL) {
               pr_err("%s L%d: error!\n", __func__, __LINE__);
                return -ENOMEM;
        }
#endif	
	desc->cim_id = priv->cim_id;	/* attached to cim 0/1 */
	desc->rst_pin = priv->rst_pin;
	desc->pd_pin = priv->pd_pin;
	desc->mclk = 24000000;
	desc->i2c_clk = 400000;		/* set i2c speed: 400KHZ */
	desc->bus_width = 8;
	desc->wait_frames = 2;
#ifdef CONFIG_VIDEO_CIM_IN_FMT_YUV444
	desc->fourcc = V4L2_PIX_FMT_YUV444;
#else
	desc->fourcc = V4L2_PIX_FMT_YUYV;
#endif
	desc->ops = &ov5640_sensor_ops;
	/* max capture resolution: 2048 x 1536 x 16 bits */
	memcpy(&desc->cap_res, &ov5640_resolution_table[0], sizeof(struct resolution_info));
	/* max preview resolution: 1024 x 768 x 16 bits */
	memcpy(&desc->pre_res, &ov5640_resolution_table[1], sizeof(struct resolution_info));
	desc->resolution_table	= ov5640_resolution_table;
	desc->table_nr = ARRAY_SIZE(ov5640_resolution_table);

	sensor_set_i2c_speed(client, desc->i2c_clk);

	/* attaching sensor to CIM host controller */
	jz_sensor_register(desc);

	i2c_set_clientdata(client, desc);

	return 0;
}

static int ov5640_remove(struct i2c_client *client)
{
//	struct jz_sensor_desc *desc = container_of(client, struct jz_sensor_desc, client);
	struct jz_sensor_desc *desc = i2c_get_clientdata(client);

	printk(KERN_INFO "%s: %s probed\n", __func__, desc->name);

	i2c_set_clientdata(client, NULL);
	kfree(desc);

	return 0;
}

/*
 * i2c_device_id.name must be same with i2c_board_info.type !
 */
static const struct i2c_device_id ov5640_id[] = {
	{ CIM_SENSOR_NAME, 0 },
	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_driver = {
	.probe		= ov5640_probe,
	.remove		= ov5640_remove,
	.id_table	= ov5640_id,
	.driver	= {
		.name = "ov5640",
	},
};

static int __init ov5640_init(void)
{
	return i2c_add_driver(&ov5640_driver);
}

static void __exit ov5640_exit(void)
{
	i2c_del_driver(&ov5640_driver);
}

module_init(ov5640_init);
module_exit(ov5640_exit);
