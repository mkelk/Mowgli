
#ifndef __BOARD_H
#define __BOARD_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * at the moment this really only works for BOARD_YARDFORCE500
 * the bluepill config is broken because it hasnt been used in a while - will be fixed later or removed ;-)
 *
 */

/********************************************************************************
* YARDFORCE 500 MAINBOARD
********************************************************************************/
#ifdef BOARD_YARDFORCE500    
    #warning "Compiling for YardForce 500 (GForce) board"
    

    #define PANEL_TYPE_YARDFORCE_500_CLASSIC    1
    // #define PANEL_TYPE_YARDFORCE_900_ECO   1

    #define LOW_BAT_THRESHOLD   23.5

    // we use J18 (Red 9 pin connector as Master Serial Port)
    #define MASTER_J18 1

    // enable Drive and Blade Motor UARTS
    #define DRIVEMOTORS_USART_ENABLED 1
    #define BLADEMOTOR_USART_ENABLED 1
    #define PANEL_USART_ENABLED 1

    // our IMU hangs of a bigbanged I2C bus on J18
    #define SOFT_I2C_ENABLED 1

    #define LED_PIN GPIO_PIN_2
    #define LED_GPIO_PORT GPIOB
    #define LED_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()

    /* 24V Supply */
    #define TF4_PIN GPIO_PIN_5
    #define TF4_GPIO_PORT GPIOC
    #define TF4_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

    /* Blade Motor nRESET - (HIGH for no RESET) */
    #define PAC5223RESET_PIN GPIO_PIN_14
    #define PAC5223RESET_GPIO_PORT GPIOE
    #define PAC5223RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()

    /* Drive Motors - HC366 OE Pins (LOW to enable) */
    #define PAC5210RESET_PIN GPIO_PIN_15
    #define PAC5210RESET_GPIO_PORT GPIOE
    #define PAC5210RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()

    /* Charge Control Pins - HighSide/LowSide MosFET */    
    #define CHARGE_LOWSIDE_PIN GPIO_PIN_8
    #define CHARGE_HIGHSIDE_PIN GPIO_PIN_9
    #define CHARGE_GPIO_PORT GPIOE
    #define CHARGE_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE();
    
    /* either J6 or J18 can be the master USART port */
#ifdef MASTER_J6    
    /* USART1 (J6 Pin 1 (TX) Pin 2 (RX)) */    
    #define MASTER_USART_INSTANCE USART1
    #define MASTER_USART_RX_PIN GPIO_PIN_10
    #define MASTER_USART_RX_PORT GPIOA
    #define MASTER_USART_TX_PIN GPIO_PIN_9
    #define MASTER_USART_TX_PORT GPIOA
    #define MASTER_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
    #define MASTER_USART_USART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
    #define MASTER_USART_IRQ USART1_IRQn
#endif
#ifdef MASTER_J18
    /* UART4 (J18 Pin 7 (TX) Pin 8 (RX)) */
    #define MASTER_USART_INSTANCE UART4
    #define MASTER_USART_RX_PIN GPIO_PIN_11
    #define MASTER_USART_RX_PORT GPIOC
    #define MASTER_USART_TX_PIN GPIO_PIN_10
    #define MASTER_USART_TX_PORT GPIOC
    #define MASTER_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
    #define MASTER_USART_USART_CLK_ENABLE() __HAL_RCC_UART4_CLK_ENABLE()
    #define MASTER_USART_IRQ UART4_IRQn
#endif

    #ifdef DRIVEMOTORS_USART_ENABLED
        /* drive motors PAC 5210 (USART2) */
        #define DRIVEMOTORS_USART_INSTANCE USART2
        
        #define DRIVEMOTORS_USART_RX_PIN GPIO_PIN_6
        #define DRIVEMOTORS_USART_RX_PORT GPIOD

        #define DRIVEMOTORS_USART_TX_PIN GPIO_PIN_5
        #define DRIVEMOTORS_USART_TX_PORT GPIOD

        #define DRIVEMOTORS_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOD_CLK_ENABLE()
        #define DRIVEMOTORS_USART_USART_CLK_ENABLE() __HAL_RCC_USART2_CLK_ENABLE()

        #define DRIVEMOTORS_USART_IRQ USART2_IRQn
        #define DRIVEMOTORS_MSG_LEN 12
    #endif 

    #ifdef BLADEMOTOR_USART_ENABLED
        /* blade motor PAC 5223 (USART3) */
        #define BLADEMOTOR_USART_INSTANCE USART3
        
        #define BLADEMOTOR_USART_RX_PIN GPIO_PIN_11
        #define BLADEMOTOR_USART_RX_PORT GPIOB

        #define BLADEMOTOR_USART_TX_PIN GPIO_PIN_10
        #define BLADEMOTOR_USART_TX_PORT GPIOB

        #define BLADEMOTOR_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
        #define BLADEMOTOR_USART_USART_CLK_ENABLE() __HAL_RCC_USART3_CLK_ENABLE()
    #endif 

    #ifdef PANEL_USART_ENABLED    
        #define PANEL_USART_INSTANCE USART1

        #define PANEL_USART_RX_PIN GPIO_PIN_10
        #define PANEL_USART_RX_PORT GPIOA

        #define PANEL_USART_TX_PIN GPIO_PIN_9
        #define PANEL_USART_TX_PORT GPIOA

        #define PANEL_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
        #define PANEL_USART_USART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
        #define PANEL_USART_IRQ USART1_IRQn
    #endif

    // J18 has the SPI3 pins, as we dont use SPI3, we recycle them for I2C Bitbanging (for our Pololu ALtIMU-10v5)
    #ifdef SOFT_I2C_ENABLED
        #define SOFT_I2C_SCL_PIN GPIO_PIN_3
        #define SOFT_I2C_SCL_PORT GPIOB
        #define SOFT_I2C_SDA_PIN GPIO_PIN_4
        #define SOFT_I2C_SDA_PORT GPIOB

        #define SOFT_I2C_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE();
    #endif

#endif // BOARD_YARDFORCE500


/********************************************************************************
* BLUE PILL DEVELOPMENT BOARD (USED FOR SIMULATION)
********************************************************************************/
#ifdef BOARD_BLUEPILL

    #warning "Compiling for BLUEPILL dev board"    
    #define LED_PIN GPIO_PIN_13
    #define LED_GPIO_PORT GPIOC
    #define LED_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

    /* 24V Supply */
    #define TF4_PIN GPIO_PIN_5
    #define TF4_GPIO_PORT GPIOC
    #define TF4_GPIO_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()

    /* Blade Motor */
    #define PAC5223RESET_PIN GPIO_PIN_14
    #define PAC5223RESET_GPIO_PORT GPIOE
    #define PAC5223RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOE_CLK_ENABLE()

    /* Drive Motors */
    #define PAC5210RESET_PIN GPIO_PIN_12
    #define PAC5210RESET_GPIO_PORT GPIOB
    #define PAC5210RESET_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
    
    /* Charge Control Pins - HighSide/LowSide MosFET */    
    #define CHARGE_LOWSIDE_PIN GPIO_PIN_7
    #define CHARGE_HIGHSIDE_PIN GPIO_PIN_8
    #define CHARGE_GPIO_PORT GPIOA
    #define CHARGE_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE();

    /* upstream controller / debug */
    #define MASTER_USART_INSTANCE USART1
    #define MASTER_USART_RX_PIN GPIO_PIN_10
    #define MASTER_USART_RX_PORT GPIOA
    #define MASTER_USART_TX_PIN GPIO_PIN_9
    #define MASTER_USART_TX_PORT GPIOA
    #define MASTER_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
    #define MASTER_USART_USART_CLK_ENABLE() __HAL_RCC_USART1_CLK_ENABLE()
    #define MASTER_USART_IRQ USART1_IRQn

    #ifdef DRIVEMOTORS_USART_ENABLED
        /* drive motors (USART2) */
        #define DRIVEMOTORS_USART_INSTANCE USART2

        #define DRIVEMOTORS_USART_RX_PIN GPIO_PIN_3
        #define DRIVEMOTORS_USART_RX_PORT GPIOA

        #define DRIVEMOTORS_USART_TX_PIN GPIO_PIN_2
        #define DRIVEMOTORS_USART_TX_PORT GPIOA

        #define DRIVEMOTORS_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOA_CLK_ENABLE()
        #define DRIVEMOTORS_USART_USART_CLK_ENABLE() __HAL_RCC_USART2_CLK_ENABLE()
    #endif 

    #ifdef BLADEMOTOR_USART_ENABLED
        /* blade motor PAC 5223 (USART3) */
        #define BLADEMOTOR_USART_INSTANCE USART3
        
        #define BLADEMOTOR_USART_RX_PIN GPIO_PIN_11
        #define BLADEMOTOR_USART_RX_PORT GPIOB

        #define BLADEMOTOR_USART_TX_PIN GPIO_PIN_10
        #define BLADEMOTOR_USART_TX_PORT GPIOB

        #define BLADEMOTOR_USART_GPIO_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
        #define BLADEMOTOR_USART_USART_CLK_ENABLE() __HAL_RCC_USART3_CLK_ENABLE()
    #endif 
#endif // BOARD_BLUEPILL

#ifdef __cplusplus
}
#endif

#endif /* __BOARD_H */