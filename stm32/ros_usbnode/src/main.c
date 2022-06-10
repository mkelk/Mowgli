/*
 * Project Mowgli - STM32 ROS SERIAL USB
 * (c) Cybernet / cn@warp.at
 * 
 *  Version 1.0 
 *  
 *  compile with -DBOARD_YARDFORCE500 to enable the YF500 GForce pinout
 * 
 *  ROS integration howto taken from here: https://github.com/Itamare4/ROS_stm32f1_rosserial_USB_VCP (Itamar Eliakim)
 *  
 */

#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal_uart.h"
#include "stm32f1xx_hal_adc.h"
#include "main.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
// stm32 custom
#include "board.h"
#include "panel.h"
#include "lis3dh_reg.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"
#include "nbt.h"

// ros
#include "cpp_main.h"
#include "ringbuffer.h"

static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len);
static int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len);


static nbt_t main_chargecontroller_nbt;
static nbt_t main_statusled_nbt;
static nbt_t throttle_motor_callback;

enum rx_status_enum { RX_WAIT, RX_VALID, RX_CRC_ERROR};

static uint8_t drivemotors_rcvd_data;
uint8_t  drivemotors_rx_buf[32];
uint32_t drivemotors_rx_buf_idx = 0;
uint8_t  drivemotors_rx_buf_crc = 0;
uint8_t  drivemotors_rx_LENGTH = 0;
uint8_t  drivemotors_rx_CRC = 0;
uint8_t  drivemotors_rx_STATUS = RX_WAIT;


// enum master_rx_status_enum { RX_WAIT, RX_VALID, RX_CRC_ERROR};

static uint8_t master_rcvd_data;
uint8_t  master_rx_buf[32];
uint32_t master_rx_buf_idx = 0;
uint8_t  master_rx_buf_crc = 0;
uint8_t  master_rx_LENGTH = 0;
uint8_t  master_rx_CRC = 0;
uint8_t  master_rx_STATUS = RX_WAIT;
int    blade_motor = 0;

static uint8_t panel_rcvd_data;

// exported via rostopics
uint16_t chargecontrol_pwm_val=50;
uint8_t  chargecontrol_is_charging=0;
uint16_t right_encoder_val=0;
uint16_t left_encoder_val=0;
int16_t right_wheel_speed_val=0;
int16_t left_wheel_speed_val=0;

UART_HandleTypeDef MASTER_USART_Handler; // UART  Handle
UART_HandleTypeDef DRIVEMOTORS_USART_Handler; // UART  Handle
UART_HandleTypeDef BLADEMOTOR_USART_Handler; // UART  Handle

I2C_HandleTypeDef I2C_Handle;
ADC_HandleTypeDef ADC_Handle;
TIM_HandleTypeDef TIM1_Handle;


/*
 * Master UART receive ISR
 * DriveMotors UART receive ISR
 * PANEL UART receive ISR
 */ 
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{    
       

       if (huart->Instance == MASTER_USART_INSTANCE)
       {
         /*
            * MASTER Message handling
            */
           if (master_rx_buf_idx == 0 && master_rcvd_data == 0x55)           /* PREAMBLE */  
           {                                  
               master_rx_buf_crc = master_rcvd_data;        
               master_rx_buf[master_rx_buf_idx++] = master_rcvd_data;                                   
           }           
           else if (master_rx_buf_idx == 1 && master_rcvd_data == 0xAA)        /* PREAMBLE */    
           {                   
               master_rx_buf_crc += master_rcvd_data;
               master_rx_buf[master_rx_buf_idx++] = master_rcvd_data;               
           }
           else if (master_rx_buf_idx == 2) /* LEN */
           {    
               master_rx_LENGTH = master_rcvd_data;
               master_rx_buf[master_rx_buf_idx++] = master_rcvd_data;               
               master_rx_buf_crc += master_rcvd_data;                              
           }
           else if (master_rx_buf_idx >= 3 && master_rx_buf_idx <= 2+master_rx_LENGTH) /* DATA bytes */
           {
               master_rx_buf[master_rx_buf_idx] = master_rcvd_data;
               master_rx_buf_idx++;
               master_rx_buf_crc += master_rcvd_data;               
           }
           else if (master_rx_buf_idx >= 3+master_rx_LENGTH)    /* CRC byte */
           {
               master_rx_CRC = master_rcvd_data;
               master_rx_buf[master_rx_buf_idx] = master_rcvd_data;
               master_rx_buf_idx++;               
               if (master_rx_buf_crc == master_rcvd_data)
               {                   
                   // message valid, reader must set back STATUS to RX_WAIT
                   master_rx_STATUS = RX_VALID;
                   //master_rx_buf_idx = 0;
               }
               else
               {                   
                   // crc failed, reader must set back STATUS to RX_WAIT
                   master_rx_STATUS = RX_CRC_ERROR;                   
                   master_rx_buf_idx = 0;
               }
           }
           else
           {
               master_rx_STATUS = RX_WAIT;
               master_rx_buf_idx = 0;               
           }           
           HAL_UART_Receive_IT(&MASTER_USART_Handler, &master_rcvd_data, 1);   // rearm interrupt
       }
       else if (huart->Instance == DRIVEMOTORS_USART_INSTANCE)
       {
         /*
            * DRIVE MOTORS Message handling
            */
            //debug_printf("HAL_UART_RxCpltCallback - DRIVEMOTORS_USART_INSTANCE\r\n");
           if (drivemotors_rx_buf_idx == 0 && drivemotors_rcvd_data == 0x55)           /* PREAMBLE */  
           {                                   
               drivemotors_rx_buf_crc = drivemotors_rcvd_data;        
               drivemotors_rx_buf[drivemotors_rx_buf_idx++] = drivemotors_rcvd_data;                                   
           }           
           else if (drivemotors_rx_buf_idx == 1 && drivemotors_rcvd_data == 0xAA)        /* PREAMBLE */    
           {                                  
               drivemotors_rx_buf_crc += drivemotors_rcvd_data;
               drivemotors_rx_buf[drivemotors_rx_buf_idx++] = drivemotors_rcvd_data;               
           }
           else if (drivemotors_rx_buf_idx == 2) /* LEN */
           {    
               drivemotors_rx_LENGTH = drivemotors_rcvd_data;
               drivemotors_rx_buf[drivemotors_rx_buf_idx++] = drivemotors_rcvd_data;               
               drivemotors_rx_buf_crc += drivemotors_rcvd_data;                              
           }
           else if (drivemotors_rx_buf_idx >= 3 && drivemotors_rx_buf_idx <= 2+drivemotors_rx_LENGTH) /* DATA bytes */
           {
               drivemotors_rx_buf[drivemotors_rx_buf_idx] = drivemotors_rcvd_data;
               drivemotors_rx_buf_idx++;
               drivemotors_rx_buf_crc += drivemotors_rcvd_data;               
           }
           else if (drivemotors_rx_buf_idx >= 3+drivemotors_rx_LENGTH)    /* CRC byte */
           {
               drivemotors_rx_CRC = drivemotors_rcvd_data;
               drivemotors_rx_buf[drivemotors_rx_buf_idx] = drivemotors_rcvd_data;
               drivemotors_rx_buf_idx++;               
               if (drivemotors_rx_buf_crc == drivemotors_rcvd_data)
               {                   
                    if (NBT_handler(&throttle_motor_callback))
                    {            
                        debug_printf("HAL_UART_RxCpltCallback - DRIVEMOTORS_USART_INSTANCE - RX_VALID\r\n");
                        msgPrint(drivemotors_rx_buf, drivemotors_rx_buf_idx);             
                    }        
                   
                   // message valid, reader must set back STATUS to RX_WAIT
                   drivemotors_rx_STATUS = RX_VALID;
                   //drivemotors_rx_buf_idx = 0;
               }
               else
               {                   
                   // crc failed, reader must set back STATUS to RX_WAIT
                   drivemotors_rx_STATUS = RX_CRC_ERROR;                   
                   drivemotors_rx_buf_idx = 0;
               }
           }
           else
           {
               drivemotors_rx_STATUS = RX_WAIT;
               drivemotors_rx_buf_idx = 0;               
           }
           HAL_UART_Receive_IT(&DRIVEMOTORS_USART_Handler, &drivemotors_rcvd_data, 1);   // rearm interrupt               
       }       
       else if(huart->Instance == PANEL_USART_INSTANCE)
       {
           PANEL_Handle_Received_Data(panel_rcvd_data);
           HAL_UART_Receive_IT(&PANEL_USART_Handler, &panel_rcvd_data, 1);   // rearm interrupt               
       }
}


int main(void)
{    
    uint8_t blademotor_init[] =  { 0x55, 0xaa, 0x12, 0x20, 0x80, 0x00, 0xac, 0x0d, 0x00, 0x02, 0x32, 0x50, 0x1e, 0x04, 0x00, 0x15, 0x21, 0x05, 0x0a, 0x19, 0x3c, 0xaa };    
    uint8_t drivemotors_init[] = { 0x55, 0xaa, 0x08, 0x10, 0x80, 0xa0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x37};

    HAL_Init();
    SystemClock_Config();

    __HAL_RCC_AFIO_CLK_ENABLE();

    MASTER_USART_Init();
    debug_printf(" * Master USART (debug) initialized\r\n");

    LED_Init();
    debug_printf(" * LED initialized\r\n");
    TF4_Init();
    debug_printf(" * 24V switched on\r\n");
    PAC5223RESET_Init();
    debug_printf(" * PAC 5223 out of reset\r\n");
    PAC5210RESET_Init();
    debug_printf(" * PAC 5210 out of reset\r\n");
    
    I2C_Init();
    debug_printf(" * I2C(Accelerometer) initialized\r\n");
    ADC1_Init();
    debug_printf(" * ADC1 initialized\r\n");
    TIM1_Init();   
    debug_printf(" * Timer1 (Charge PWM) initialized\r\n");
    MX_USB_DEVICE_Init();
    debug_printf(" * USB CDC initialized\r\n");
    PANEL_Init();
    debug_printf(" * Panel initialized\r\n");

    // ADC Timer
    HAL_TIM_PWM_Start(&TIM1_Handle, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&TIM1_Handle, TIM_CHANNEL_1);
    debug_printf(" * ADC Timers initialized\r\n");

    // Init Drive Motors and Blade Motor
    #ifdef DRIVEMOTORS_USART_ENABLED
        DRIVEMOTORS_USART_Init();
        debug_printf(" * Drive Motors USART initialized\r\n");
    #endif
    #ifdef BLADEMOTOR_USART_ENABLED
        BLADEMOTOR_USART_Init();
        debug_printf(" * Blade Motor USART initialized\r\n");
    #endif
    

    HAL_UART_Receive_IT(&MASTER_USART_Handler, &master_rcvd_data, 1);
    debug_printf(" * Master Interrupt enabled\r\n");
    HAL_UART_Receive_IT(&DRIVEMOTORS_USART_Handler, &drivemotors_rcvd_data, 1);
    debug_printf(" * Drive Motors Interrupt enabled\r\n");
    HAL_UART_Receive_IT(&PANEL_USART_Handler, &panel_rcvd_data, 1);   // rearm interrupt               
    debug_printf(" * Panel Interrupt enabled\r\n");

    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_PIN, 0);
    HAL_GPIO_WritePin(TF4_GPIO_PORT, TF4_PIN, 1);
    HAL_GPIO_WritePin(PAC5223RESET_GPIO_PORT, PAC5223RESET_PIN, 1);     // take Blade PAC out of reset if HIGH
    HAL_GPIO_WritePin(PAC5210RESET_GPIO_PORT, PAC5210RESET_PIN, 0);     // take Drive Motor PAC out of reset if LOW

    // send some init messages - needs further investigation, drivemotors are happy without it
    HAL_UART_Transmit(&DRIVEMOTORS_USART_Handler, drivemotors_init, 12, HAL_MAX_DELAY);
    HAL_Delay(100);
    HAL_UART_Transmit(&DRIVEMOTORS_USART_Handler, drivemotors_init, 12, HAL_MAX_DELAY);
    HAL_Delay(100);
    debug_printf(" * Drive Motors initialized\r\n");

    HAL_UART_Transmit(&BLADEMOTOR_USART_Handler, blademotor_init, 22, HAL_MAX_DELAY);
    HAL_Delay(100);
    HAL_UART_Transmit(&BLADEMOTOR_USART_Handler, blademotor_init, 22, HAL_MAX_DELAY);
    HAL_Delay(100);
    debug_printf(" * Blade Motor initialized\r\n");
    debug_printf(" * HW Init completed\r\n");    
    
    // Initialize Main Timers
	NBT_init(&main_chargecontroller_nbt, 10);
    NBT_init(&main_statusled_nbt, 1000);
    NBT_init(&throttle_motor_callback, 1000);
    debug_printf(" * NBT Main timers initialized\r\n");     

    // Initialize ROS
    init_ROS();
    debug_printf(" * ROS serial node initialized\r\n");     
    debug_printf("\r\n >>> entering main loop ...\r\n\r\n");     
    while (1)
    {
        chatter_handler();
        motors_handler();    
        panel_handler();
        spinOnce();                       
        if (drivemotors_rx_STATUS == RX_VALID)                    // valid frame received from DRIVEMOTORS USART
        {
            // debug_printf("Handling valid drivemotors\r\n");     
            //  msgPrint(drivemotors_rx_buf, drivemotors_rx_buf_idx);             

            uint8_t direction = drivemotors_rx_buf[5];

            // we need to adjust for direction (+/-) !
            if ((direction & 0x30) == 0x30)
            {            
                left_wheel_speed_val = -1 * drivemotors_rx_buf[7];
            }
            else 
            {
                left_wheel_speed_val =  drivemotors_rx_buf[7];
            }
            if ( (direction & 0xc0) == 0xc0)
            {            
                right_wheel_speed_val = -1 * drivemotors_rx_buf[6];
            }
            else 
            {
                right_wheel_speed_val =  drivemotors_rx_buf[6];
            }
                        
            left_encoder_val = (drivemotors_rx_buf[16]<<8)+drivemotors_rx_buf[15];
            right_encoder_val = (drivemotors_rx_buf[14]<<8)+drivemotors_rx_buf[13];            
            if (drivemotors_rx_buf[5]>>4)       // stuff is moving
            {
                debug_printf("Stuff is moving\r\n");
                msgPrint(drivemotors_rx_buf, drivemotors_rx_buf_idx);             
            }                    
            drivemotors_rx_buf_idx = 0;
            drivemotors_rx_STATUS = RX_WAIT;                    // ready for next message            
            //  HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);         // flash LED             
        }     
        /* not used atm - we control the bot via ROS
        if (master_rx_STATUS == RX_VALID)                        // valid frame received by MASTER USART
        {
            
                int i;
                //debug_printf("master_rx_buf_crc = 0x%02x\r\n", master_rx_buf_crc);            
                //debug_printf("master_rx_CRC = 0x%02x\r\n", master_rx_CRC);            
                //debug_printf("master_rx_LENGTH = %d\r\n", master_rx_LENGTH);            
                debug_printf("tx: ");
                for (i=0;i<master_rx_buf_idx;i++)
                {
                    debug_printf(" %02x", master_rx_buf[i]);
                }            
                debug_printf("\r\n");
            

            // until we have some kind of protocol we discrimate what goes where simply by message length
            // drive motors always get a 12 byte message relayed
            if (master_rx_buf_idx == 12)
                HAL_UART_Transmit(&DRIVEMOTORS_USART_Handler, master_rx_buf, master_rx_buf_idx, HAL_MAX_DELAY);
            // blade motor always gets a 12 byte message relayed
            if (master_rx_buf_idx == 7)
                HAL_UART_Transmit(&BLADEMOTOR_USART_Handler, master_rx_buf, master_rx_buf_idx, HAL_MAX_DELAY);

            master_rx_STATUS = RX_WAIT; // ready for next message
            master_rx_buf_idx = 0;

            HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_PIN);         // flash LED             
        }
        */
        broadcast_handler();   
        if (NBT_handler(&main_chargecontroller_nbt))
	    {            
			ChargeController(); 
	    }
        if (NBT_handler(&main_statusled_nbt))
	    {            
			StatusLEDUpdate(); 
	    }        
    }
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // we never get here ...
    /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}

/**
 * @brief Init the Master Serial Port  - this what connects to the upstream controller
 * @retval None
 */
void MASTER_USART_Init()
{
    // enable port and usart clocks
    MASTER_USART_GPIO_CLK_ENABLE();
    MASTER_USART_USART_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct;
    // RX
    GPIO_InitStruct.Pin = MASTER_USART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(MASTER_USART_RX_PORT, &GPIO_InitStruct);

    // TX
    GPIO_InitStruct.Pin = MASTER_USART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    //GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(MASTER_USART_TX_PORT, &GPIO_InitStruct);

    MASTER_USART_Handler.Instance = MASTER_USART_INSTANCE;     // USART1 (DEV)
    MASTER_USART_Handler.Init.BaudRate = 115200;               // Baud rate
    MASTER_USART_Handler.Init.WordLength = UART_WORDLENGTH_8B; // The word is  8  Bit format
    MASTER_USART_Handler.Init.StopBits = USART_STOPBITS_1;     // A stop bit
    MASTER_USART_Handler.Init.Parity = UART_PARITY_NONE;       // No parity bit
    MASTER_USART_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE; // No hardware flow control
    MASTER_USART_Handler.Init.Mode = USART_MODE_TX_RX;         // Transceiver mode

    HAL_UART_Init(&MASTER_USART_Handler); // HAL_UART_Init() Will enable  UART1

    // enable IRQ
    HAL_NVIC_SetPriority(MASTER_USART_IRQ, 0, 0);
	HAL_NVIC_EnableIRQ(MASTER_USART_IRQ);     
}


/**
 * @brief Init the Drive Motor Serial Port (PAC5210)
 * @retval None
 */
void DRIVEMOTORS_USART_Init()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // enable port and usart clocks
    DRIVEMOTORS_USART_GPIO_CLK_ENABLE();
    DRIVEMOTORS_USART_USART_CLK_ENABLE();
    
    // RX
    GPIO_InitStruct.Pin = DRIVEMOTORS_USART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(DRIVEMOTORS_USART_RX_PORT, &GPIO_InitStruct);

    // TX
    GPIO_InitStruct.Pin = DRIVEMOTORS_USART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    // GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(DRIVEMOTORS_USART_TX_PORT, &GPIO_InitStruct);

    // Alternate Pin Set ?
    __HAL_AFIO_REMAP_USART2_ENABLE();

    DRIVEMOTORS_USART_Handler.Instance = DRIVEMOTORS_USART_INSTANCE;// USART2
    DRIVEMOTORS_USART_Handler.Init.BaudRate = 115200;               // Baud rate
    DRIVEMOTORS_USART_Handler.Init.WordLength = UART_WORDLENGTH_8B; // The word is  8  Bit format
    DRIVEMOTORS_USART_Handler.Init.StopBits = USART_STOPBITS_1;     // A stop bit
    DRIVEMOTORS_USART_Handler.Init.Parity = UART_PARITY_NONE;       // No parity bit
    DRIVEMOTORS_USART_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE; // No hardware flow control
    DRIVEMOTORS_USART_Handler.Init.Mode = USART_MODE_TX_RX;         // Transceiver mode
    
    HAL_UART_Init(&DRIVEMOTORS_USART_Handler); 

      // enable IRQ
    HAL_NVIC_SetPriority(DRIVEMOTORS_USART_IRQ, 0, 0);
 	HAL_NVIC_EnableIRQ(DRIVEMOTORS_USART_IRQ);     
}

/**
 * @brief Init the Blade Motor Serial Port (PAC5223)
 * @retval None
 */
void BLADEMOTOR_USART_Init()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // enable port and usart clocks
    BLADEMOTOR_USART_GPIO_CLK_ENABLE();
    BLADEMOTOR_USART_USART_CLK_ENABLE();
    
    // RX
    GPIO_InitStruct.Pin = BLADEMOTOR_USART_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(BLADEMOTOR_USART_RX_PORT, &GPIO_InitStruct);

    // TX
    GPIO_InitStruct.Pin = BLADEMOTOR_USART_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    // GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(BLADEMOTOR_USART_TX_PORT, &GPIO_InitStruct);

    // Alternate Pin Set ?
//    __HAL_AFIO_REMAP_USART2_ENABLE();

    BLADEMOTOR_USART_Handler.Instance = BLADEMOTOR_USART_INSTANCE;// USART3
    BLADEMOTOR_USART_Handler.Init.BaudRate = 115200;               // Baud rate
    BLADEMOTOR_USART_Handler.Init.WordLength = UART_WORDLENGTH_8B; // The word is  8  Bit format
    BLADEMOTOR_USART_Handler.Init.StopBits = USART_STOPBITS_1;     // A stop bit
    BLADEMOTOR_USART_Handler.Init.Parity = UART_PARITY_NONE;       // No parity bit
    BLADEMOTOR_USART_Handler.Init.HwFlowCtl = UART_HWCONTROL_NONE; // No hardware flow control
    BLADEMOTOR_USART_Handler.Init.Mode = USART_MODE_TX_RX;         // Transceiver mode
    

    HAL_UART_Init(&BLADEMOTOR_USART_Handler); 
}


/**
 * @brief Init LED
 * @retval None
 */
void LED_Init()
{
    LED_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin = LED_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief Init TF4 (24V Power Switch)
 * @retval None
 */
void TF4_Init()
{
    TF4_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin = TF4_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(TF4_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief PAC 5223 Reset Line (Blade Motor)
 * @retval None
 */
void PAC5223RESET_Init()
{
    PAC5223RESET_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin = PAC5223RESET_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(PAC5223RESET_GPIO_PORT, &GPIO_InitStruct);
}

/**
 * @brief PAC 5210 Reset Line (Drive Motors)
 * @retval None
 */
void PAC5210RESET_Init()
{
    PAC5210RESET_GPIO_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct;
    GPIO_InitStruct.Pin = PAC5210RESET_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(PAC5210RESET_GPIO_PORT, &GPIO_InitStruct);


    // PD7 (->PAC5210 PC4), PD8 (->PAC5210 PC3)
     __HAL_RCC_GPIOD_CLK_ENABLE();    
    GPIO_InitStruct.Pin = GPIO_PIN_7| GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7 | GPIO_PIN_8 , 1);
}


/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C Initialization Function
  * @param None
  * @retval None
  */ 
void I2C_Init(void)
{
   GPIO_InitTypeDef GPIO_InitStruct = {0};
   GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7;
   GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
   GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
   HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

   /* Peripheral clock enable */
   __HAL_RCC_I2C1_CLK_ENABLE();

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  I2C_Handle.Instance = I2C1;
  I2C_Handle.Init.ClockSpeed = 100000;
  I2C_Handle.Init.DutyCycle = I2C_DUTYCYCLE_2;
  I2C_Handle.Init.OwnAddress1 = 0;
  I2C_Handle.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  I2C_Handle.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  I2C_Handle.Init.OwnAddress2 = 0;
  I2C_Handle.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  I2C_Handle.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&I2C_Handle) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}


/*
 * VREF+ = 3.3v
 */
void ADC1_Init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    /**ADC1 GPIO Configuration
    PA1     ------> Charge Current
    PA2     ------> Charge Voltage
    PA3     ------> Battery Voltage
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USER CODE BEGIN ADC1_Init 0 */

    /* USER CODE END ADC1_Init 0 */

   // ADC_ChannelConfTypeDef sConfig = {0};

    /* USER CODE BEGIN ADC1_Init 1 */

    /* USER CODE END ADC1_Init 1 */

    /** Common config
     */
    ADC_Handle.Instance = ADC1;
    ADC_Handle.Init.ScanConvMode = ADC_SCAN_DISABLE;
    ADC_Handle.Init.ContinuousConvMode = DISABLE;
    ADC_Handle.Init.DiscontinuousConvMode = DISABLE;
    ADC_Handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    ADC_Handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    ADC_Handle.Init.NbrOfConversion = 1;
    if (HAL_ADC_Init(&ADC_Handle) != HAL_OK)
    {
        Error_Handler();
    }

    // calibrate  - important for accuracy !
    HAL_ADCEx_Calibration_Start(&ADC_Handle);
}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
 void TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  __HAL_RCC_TIM1_CLK_ENABLE();
  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  TIM1_Handle.Instance = TIM1;
  TIM1_Handle.Init.Prescaler = 0;
  TIM1_Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
  TIM1_Handle.Init.Period = 1400;
  TIM1_Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  TIM1_Handle.Init.RepetitionCounter = 0;
  TIM1_Handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&TIM1_Handle) != HAL_OK)
  {
    Error_Handler();
  }

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&TIM1_Handle, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&TIM1_Handle, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 120;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&TIM1_Handle, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_ENABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_ENABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_1;
  sBreakDeadTimeConfig.DeadTime = 40;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&TIM1_Handle, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */

  GPIO_InitTypeDef GPIO_InitStruct = {0};  
  CHARGE_GPIO_CLK_ENABLE();
  /** TIM1 GPIO Configuration
  PA7 or PE8     -----> TIM1_CH1N
  PA8 oe PE9    ------> TIM1_CH1
  */
  GPIO_InitStruct.Pin = CHARGE_LOWSIDE_PIN|CHARGE_HIGHSIDE_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(CHARGE_GPIO_PORT, &GPIO_InitStruct);
  #ifdef BOARD_BLUEPILL
    __HAL_AFIO_REMAP_TIM1_PARTIAL();        // to use PA7/8 it is a partial remap
  #endif
  #ifdef BOARD_YARDFORCE500 
     __HAL_AFIO_REMAP_TIM1_ENABLE();        // to use PE8/9 it is a full remap
  #endif  
}

/*
 * Charge Current
 */
float ADC_ChargeCurrent()
{
    float_t adc_volt;
    uint16_t adc_val;
    ADC_ChannelConfTypeDef sConfig = {0};

    // switch channel
    sConfig.Channel = ADC_CHANNEL_1; // PA1 Charge Current
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    if (HAL_ADC_ConfigChannel(&ADC_Handle, &sConfig) != HAL_OK)
    {
       Error_Handler();
    }
    // do adc conversion
    HAL_ADC_Start(&ADC_Handle);
    HAL_ADC_PollForConversion(&ADC_Handle, 200);
    adc_val = (uint16_t) HAL_ADC_GetValue(&ADC_Handle);
    HAL_ADC_Stop(&ADC_Handle);
    adc_volt= (float)(adc_val/4095.0f)*3.3f;     //PA2 has a 1:16 divider
    return(adc_volt);
}


/*
 * Charge Voltage
 */
float ADC_ChargeVoltage()
{
    float_t adc_volt;
    uint16_t adc_val;
    ADC_ChannelConfTypeDef sConfig = {0};

    // switch channel
    sConfig.Channel = ADC_CHANNEL_2; // PA2 Charge Voltage
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    if (HAL_ADC_ConfigChannel(&ADC_Handle, &sConfig) != HAL_OK)
    {
       Error_Handler();
    }
    // do adc conversion
    HAL_ADC_Start(&ADC_Handle);
    HAL_ADC_PollForConversion(&ADC_Handle, 200);
    adc_val = (uint16_t) HAL_ADC_GetValue(&ADC_Handle);
    HAL_ADC_Stop(&ADC_Handle);
    adc_volt= (float)(adc_val/4095.0f)*3.3f*16;     //PA2 has a 1:16 divider
    return(adc_volt);
}


/*
 * Battery Voltage
 */
float ADC_BatteryVoltage()
{
    float_t adc_volt;
    uint16_t adc_val;
    ADC_ChannelConfTypeDef sConfig = {0};

    // switch channel
    sConfig.Channel = ADC_CHANNEL_3; // PA3 Battery
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
    if (HAL_ADC_ConfigChannel(&ADC_Handle, &sConfig) != HAL_OK)
    {
       Error_Handler();
    }
    // do adc conversion
    HAL_ADC_Start(&ADC_Handle);
    HAL_ADC_PollForConversion(&ADC_Handle, 200);
    adc_val = (uint16_t) HAL_ADC_GetValue(&ADC_Handle);
    HAL_ADC_Stop(&ADC_Handle);
    adc_volt= (float)(adc_val/4095.0f)*3.3f*10;  //PA3 has a 1:10 divider
    return(adc_volt);
}

/*
 * ADC test code to sample PA2 (Charge) and PA3 (Battery) Voltage
 */
void ADC_Test()
{
    float_t adc_volt;
    uint16_t adc_val;
    ADC_ChannelConfTypeDef sConfig = {0};

        // switch channel
        sConfig.Channel = ADC_CHANNEL_3; // PA3 Battery
        sConfig.Rank = ADC_REGULAR_RANK_1;
        sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
        if (HAL_ADC_ConfigChannel(&ADC_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        // do adc conversion
        HAL_ADC_Start(&ADC_Handle);
        HAL_ADC_PollForConversion(&ADC_Handle, 200);
        adc_val = (uint16_t) HAL_ADC_GetValue(&ADC_Handle);
        HAL_ADC_Stop(&ADC_Handle);
        adc_volt= (float)(adc_val/4095.0f)*3.3f*10;  //PA3 has a 1:10 divider
        debug_printf("Battery Voltage: %2.2fV (adc:%d)\r\n", adc_volt, adc_val);

        // switch channel
        sConfig.Channel = ADC_CHANNEL_2; // PA2 Charge
        sConfig.Rank = ADC_REGULAR_RANK_1;
        sConfig.SamplingTime = ADC_SAMPLETIME_28CYCLES_5;
        if (HAL_ADC_ConfigChannel(&ADC_Handle, &sConfig) != HAL_OK)
        {
            Error_Handler();
        }
        // do adc conversion
        HAL_ADC_Start(&ADC_Handle);
        HAL_ADC_PollForConversion(&ADC_Handle, 200);
        adc_val = (uint16_t) HAL_ADC_GetValue(&ADC_Handle);
        HAL_ADC_Stop(&ADC_Handle);
        adc_volt= (float)(adc_val/4095.0f)*3.3f*16;     //PA2 has a 1:16 divider
        debug_printf(" Charge Voltage: %2.2fV (adc:%d)\r\n", adc_volt, adc_val);
        debug_printf("\r\n");
  
}


/*
 * I2C send function
 */
static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *bufp,
                              uint16_t len)
{
  reg |= 0x80;
  HAL_I2C_Mem_Write(handle, LIS3DH_I2C_ADD_L, reg,
                    I2C_MEMADD_SIZE_8BIT, (uint8_t*) bufp, len, 1000);
  return 0;
}

/*
 * I2C receive function
 */
static int32_t platform_read(void *handle, uint8_t reg, uint8_t *bufp,
                             uint16_t len)
{
  /* Read multiple command */
  reg |= 0x80;
  HAL_I2C_Mem_Read(handle, LIS3DH_I2C_ADD_L, reg,
                   I2C_MEMADD_SIZE_8BIT, bufp, len, 1000);
  return 0;
}

/*
 * test code to interface the LIS accerlometer that is onboard the YF500
 */
void I2C_Test(void)
{
    static int16_t data_raw_acceleration[3];
    static int16_t data_raw_temperature;
    static float acceleration_mg[3];
    static float temperature_degC;
    // static uint8_t whoamI;

    stmdev_ctx_t dev_ctx;
    lis3dh_reg_t reg;
    dev_ctx.write_reg = platform_write;
    dev_ctx.read_reg = platform_read;
    dev_ctx.handle = &I2C_Handle;
    HAL_Delay(50);   // wait for bootup
    /* Check device ID */
    lis3dh_device_id_get(&dev_ctx, &reg.byte);    
    if (reg.byte != LIS3DH_ID) {
        while (1) {
            /* manage here device not found */
            debug_printf("Accelerometer not found on I2C addr 0x%x\r\n", LIS3DH_I2C_ADD_L);
        }
    }
    debug_printf("Accelerometer found\r\n");
     /* Enable Block Data Update. */
    lis3dh_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);
    /* Set Output Data Rate to 1Hz. */
    lis3dh_data_rate_set(&dev_ctx, LIS3DH_ODR_100Hz);
    /* Set full scale to 2g. */
    lis3dh_full_scale_set(&dev_ctx, LIS3DH_2g);
    /* Enable temperature sensor. */
    lis3dh_aux_adc_set(&dev_ctx, LIS3DH_AUX_ON_TEMPERATURE);
    /* Set device in continuous mode with 12 bit resol. */
    lis3dh_operating_mode_set(&dev_ctx, LIS3DH_HR_12bit);

    /* Read samples in polling mode (no int) */
    while (1) {
        lis3dh_reg_t reg;
        /* Read output only if new value available */
        lis3dh_xl_data_ready_get(&dev_ctx, &reg.byte);        
        if (reg.byte) {
            /* Read accelerometer data */
            memset(data_raw_acceleration, 0x00, 3 * sizeof(int16_t));
            lis3dh_acceleration_raw_get(&dev_ctx, data_raw_acceleration);
            acceleration_mg[0] =
                lis3dh_from_fs2_hr_to_mg(data_raw_acceleration[0]);
            acceleration_mg[1] =
                lis3dh_from_fs2_hr_to_mg(data_raw_acceleration[1]);
            acceleration_mg[2] =
                lis3dh_from_fs2_hr_to_mg(data_raw_acceleration[2]);

            debug_printf("Acceleration [mg]: X=%4.2f\tY=%4.2f\tZ=%4.2f\r\n", acceleration_mg[0], acceleration_mg[1], acceleration_mg[2]);        
        }

        lis3dh_temp_data_ready_get(&dev_ctx, &reg.byte);

        if (reg.byte) {            
            // Read temperature data 
            memset(&data_raw_temperature, 0x00, sizeof(int16_t));
            lis3dh_temperature_raw_get(&dev_ctx, &data_raw_temperature);
            temperature_degC =
                lis3dh_from_lsb_hr_to_celsius(data_raw_temperature);
            debug_printf("Temperature [degC]:%6.2f\r\n", temperature_degC);
        }
    }
}

/*
 * manaes the charge voltage, and charge, lowbat LED
 * needs to be called frequently
 */
void ChargeController(void)
{        
        float_t charge_voltage;

        charge_voltage =  ADC_ChargeVoltage();            
        // set PWM to approach 29.4V charge voltage         
        if ((charge_voltage < 29.4) && (chargecontrol_pwm_val < 1350))
        {
            chargecontrol_pwm_val++;
        }
        if ((charge_voltage > 29.4) && (chargecontrol_pwm_val > 50))
        {
            chargecontrol_pwm_val--;
        }
        TIM1->CCR1 = chargecontrol_pwm_val;  
}

/*
 * Update the states for the Charge and Low Bat LEDs
 */
void StatusLEDUpdate(void)
{
        float_t charge_voltage, battery_voltage;

        charge_voltage =  ADC_ChargeVoltage();    
        battery_voltage = ADC_BatteryVoltage();        
        if (charge_voltage >= battery_voltage)         // we are charging ...
        {
            // indicate charging by flashing fast if we are plugged in
            PANEL_Set_LED(PANEL_LED_CHARGING, PANEL_LED_FLASH_FAST);
            chargecontrol_is_charging = 1;
        }
        else
        {
            PANEL_Set_LED(PANEL_LED_CHARGING, PANEL_LED_OFF);
            chargecontrol_is_charging = 0;
        }
            
        // show a lowbat warning if battery voltage drops below LOW_BAT_THRESHOLD ? (random guess, needs more testing or a compare to the stock firmware)            
        // if goes below LOW_BAT_THRESHOLD-1 we increase led flash frequency        
        if (battery_voltage <= LOW_BAT_THRESHOLD)
        {
            PANEL_Set_LED(PANEL_LED_BATTERY_LOW, PANEL_LED_FLASH_SLOW); // low
        }
        else if (battery_voltage <= LOW_BAT_THRESHOLD-1.0)
        {
            PANEL_Set_LED(PANEL_LED_BATTERY_LOW, PANEL_LED_FLASH_FAST); // really low
        }
        else
        {
            PANEL_Set_LED(PANEL_LED_BATTERY_LOW, PANEL_LED_OFF); // bat ok
        }                
        debug_printf(" > Chg Voltage: %2.2fV | Bat Voltage %2.2fV\r\n", charge_voltage, battery_voltage);                           
}

/*
 * Send message to DriveMotors PAC
 *
 * <xxx>_speed = 0x - 0xFF
 * <xxx>_dir = 1 = CW, 1 != CCW
 */
void setDriveMotors(uint8_t left_speed, uint8_t right_speed, uint8_t left_dir, uint8_t right_dir)
{
    uint8_t direction = 0x0;            
    uint8_t drivemotors_msg[DRIVEMOTORS_MSG_LEN] =  { 0x55, 0xaa, 0x8, 0x10, 0x80, direction, right_speed, left_speed, 0x0, 0x0, 0x0};
    
    // calc direction bits
    if (left_dir == 1)
    {
        direction |= (0x20 + 0x10);
    }
    else
    {
        direction |= 0x20;
    }
    if (right_dir == 1)
    {
        direction |= (0x40 + 0x80);        
    }
    else
    {
        direction |= 0x80;
    }
    // update direction byte in message
    drivemotors_msg[5] = direction;
    // calc crc
    drivemotors_msg[DRIVEMOTORS_MSG_LEN-1] = crcCalc(drivemotors_msg, DRIVEMOTORS_MSG_LEN-1);
  // msgPrint(drivemotors_msg, DRIVEMOTORS_MSG_LEN);
    // transmit
    HAL_UART_Transmit(&DRIVEMOTORS_USART_Handler, drivemotors_msg, DRIVEMOTORS_MSG_LEN, HAL_MAX_DELAY);
}

/*
 * Send message to Blade PAC
 *
 * <on_off> - no speed settings available
 */
void setBladeMotor(uint8_t on_off)
{
    uint8_t blademotor_on[] =  { 0x55, 0xaa, 0x03, 0x20, 0x80, 0x80, 0x22};
    uint8_t blademotor_off[] = { 0x55, 0xaa, 0x03, 0x20, 0x80, 0x0, 0xa2};

    if (on_off)
    {
        HAL_UART_Transmit(&BLADEMOTOR_USART_Handler, blademotor_on, sizeof(blademotor_on), HAL_MAX_DELAY);    
    }
    else
    {
        HAL_UART_Transmit(&BLADEMOTOR_USART_Handler, blademotor_off, sizeof(blademotor_off), HAL_MAX_DELAY);
    }
        
}

/*
 * print hex bytes
 */
void msgPrint(uint8_t *msg, uint8_t msg_len)
{
     int i;
     debug_printf("msg: ");
     for (i=0;i<msg_len;i++)
     {
        debug_printf(" %02x", msg[i]);
     }            
     debug_printf("\r\n");
}

/*
 * calc crc byte
 */
uint8_t crcCalc(uint8_t *msg, uint8_t msg_len)
{
    uint8_t crc = 0x0;
    uint8_t i;

    for (i=0;i<msg_len;i++)
    {
        crc += msg[i];
    }
    return(crc);
}

/*
 * Debug print via MASTER USART
 */
void vprint(const char *fmt, va_list argp)
{
    char string[200];
    if(0 < vsprintf(string,fmt,argp)) // build string
    {
        HAL_UART_Transmit(&MASTER_USART_Handler, (uint8_t*)string, strlen(string), 0xffffff); // send message via UART
    }
}

/*
 * Debug print
 */
void debug_printf(const char *fmt, ...) 
{
    va_list argp;
    va_start(argp, fmt);
    vprint(fmt, argp);
    va_end(argp);
}