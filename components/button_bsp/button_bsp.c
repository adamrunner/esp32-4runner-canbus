#include "button_bsp.h"
#include "multi_button.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#define example_key 0  // GPIO 0 for button
#define GPIO_GET(pin) gpio_get_level((gpio_num_t)(pin))

EventGroupHandle_t key_groups;
struct Button button1; //申请按键
#define button1_id 0   //按键的ID
#define button1_active 0 //有效电平
static void clock_task_callback(void *arg)
{
  button_ticks();              //状态回调
}
uint8_t read_button_GPIO(uint8_t button_id)   //返回GPIO电平
{
	switch(button_id)
	{
		case button1_id:
      return GPIO_GET(example_key);
			break;
		default:
			break;
	}
  return 0;
}
void Button_SINGLE_CLICK_Callback(void* btn) //单击事件
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    xEventGroupSetBits( key_groups,(0x01<<0) ); 
  }
}

void Button_DOUBLE_CLICK_Callback(void* btn) //双击事件
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    xEventGroupSetBits( key_groups,(0x01<<1) );
  }
}
void Button_PRESS_DOWN_Callback(void* btn) //按下事件
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    printf("DOWN\n");
  }
}
void Button_PRESS_UP_Callback(void* btn) //弹起事件
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    printf("UP\n");
  }
}
void Button_PRESS_REPEAT_Callback(void* btn) //重复按下事件
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    printf("PRESS_REPEAT : %d\n",user_button->repeat);
  }
}
void Button_LONG_PRESS_START_Callback(void* btn) //长按触发一次事件
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    //printf("LONG_PRESS_START\n");
    xEventGroupSetBits( key_groups,(0x01<<1) );
  }
}
void Button_LONG_PRESS_HOLD_Callback(void* btn) //长按事件一直触发
{
  struct Button *user_button = (struct Button *)btn;
	if(user_button == &button1)
  {
    printf("LONG_PRESS_HOLD\n");
  }
}
void button_Init(void)
{
  /* Initialize GPIO for button */
  gpio_config_t gpio_conf = {0};
  gpio_conf.pin_bit_mask = ((uint64_t)1 << example_key);
  gpio_conf.mode = GPIO_MODE_INPUT;
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&gpio_conf);

  key_groups = xEventGroupCreate();
  //xEventGroupSetBits( TaskEven,(0x01<<2) );
  button_init(&button1, read_button_GPIO, button1_active , button1_id);      // 初始化 初始化对象 回调函数 触发电平 按键ID
  button_attach(&button1, SINGLE_CLICK, Button_SINGLE_CLICK_Callback);       //点击 注册回调函数
  button_attach(&button1, LONG_PRESS_START, Button_LONG_PRESS_START_Callback);       //长按触发 注册回调函数
  button_attach(&button1, DOUBLE_CLICK, Button_DOUBLE_CLICK_Callback);       //双击 注册回调函数
  //button_attach(&button1, PRESS_REPEAT, Button_PRESS_REPEAT_Callback);       //重复按下 注册回调函数
  const esp_timer_create_args_t clock_tick_timer_args =
  {
    .callback = &clock_task_callback,
    .name = "clock_task",
    .arg = NULL,
  };
  esp_timer_handle_t clock_tick_timer = NULL;
  ESP_ERROR_CHECK(esp_timer_create(&clock_tick_timer_args, &clock_tick_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(clock_tick_timer, 1000 * 5));  //5ms
  button_start(&button1); //启动按键
}

