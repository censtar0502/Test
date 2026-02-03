################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/dispenser.c \
../Core/Src/dma.c \
../Core/Src/eeprom_at24.c \
../Core/Src/gaskitlink.c \
../Core/Src/gpio.c \
../Core/Src/i2c.c \
../Core/Src/keyboard.c \
../Core/Src/main.c \
../Core/Src/spi.c \
../Core/Src/ssd1309.c \
../Core/Src/stm32h7xx_hal_msp.c \
../Core/Src/stm32h7xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32h7xx.c \
../Core/Src/tim.c \
../Core/Src/ui_manager.c \
../Core/Src/usart.c 

OBJS += \
./Core/Src/dispenser.o \
./Core/Src/dma.o \
./Core/Src/eeprom_at24.o \
./Core/Src/gaskitlink.o \
./Core/Src/gpio.o \
./Core/Src/i2c.o \
./Core/Src/keyboard.o \
./Core/Src/main.o \
./Core/Src/spi.o \
./Core/Src/ssd1309.o \
./Core/Src/stm32h7xx_hal_msp.o \
./Core/Src/stm32h7xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32h7xx.o \
./Core/Src/tim.o \
./Core/Src/ui_manager.o \
./Core/Src/usart.o 

C_DEPS += \
./Core/Src/dispenser.d \
./Core/Src/dma.d \
./Core/Src/eeprom_at24.d \
./Core/Src/gaskitlink.d \
./Core/Src/gpio.d \
./Core/Src/i2c.d \
./Core/Src/keyboard.d \
./Core/Src/main.d \
./Core/Src/spi.d \
./Core/Src/ssd1309.d \
./Core/Src/stm32h7xx_hal_msp.d \
./Core/Src/stm32h7xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32h7xx.d \
./Core/Src/tim.d \
./Core/Src/ui_manager.d \
./Core/Src/usart.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m7 -std=gnu11 -g3 -DDEBUG -DUSE_PWR_LDO_SUPPLY -DUSE_HAL_DRIVER -DSTM32H750xx -c -I../Core/Inc -I../USB_DEVICE/App -I../USB_DEVICE/Target -I../Drivers/STM32H7xx_HAL_Driver/Inc -I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../Drivers/CMSIS/Device/ST/STM32H7xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/dispenser.cyclo ./Core/Src/dispenser.d ./Core/Src/dispenser.o ./Core/Src/dispenser.su ./Core/Src/dma.cyclo ./Core/Src/dma.d ./Core/Src/dma.o ./Core/Src/dma.su ./Core/Src/eeprom_at24.cyclo ./Core/Src/eeprom_at24.d ./Core/Src/eeprom_at24.o ./Core/Src/eeprom_at24.su ./Core/Src/gaskitlink.cyclo ./Core/Src/gaskitlink.d ./Core/Src/gaskitlink.o ./Core/Src/gaskitlink.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/i2c.cyclo ./Core/Src/i2c.d ./Core/Src/i2c.o ./Core/Src/i2c.su ./Core/Src/keyboard.cyclo ./Core/Src/keyboard.d ./Core/Src/keyboard.o ./Core/Src/keyboard.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/spi.cyclo ./Core/Src/spi.d ./Core/Src/spi.o ./Core/Src/spi.su ./Core/Src/ssd1309.cyclo ./Core/Src/ssd1309.d ./Core/Src/ssd1309.o ./Core/Src/ssd1309.su ./Core/Src/stm32h7xx_hal_msp.cyclo ./Core/Src/stm32h7xx_hal_msp.d ./Core/Src/stm32h7xx_hal_msp.o ./Core/Src/stm32h7xx_hal_msp.su ./Core/Src/stm32h7xx_it.cyclo ./Core/Src/stm32h7xx_it.d ./Core/Src/stm32h7xx_it.o ./Core/Src/stm32h7xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32h7xx.cyclo ./Core/Src/system_stm32h7xx.d ./Core/Src/system_stm32h7xx.o ./Core/Src/system_stm32h7xx.su ./Core/Src/tim.cyclo ./Core/Src/tim.d ./Core/Src/tim.o ./Core/Src/tim.su ./Core/Src/ui_manager.cyclo ./Core/Src/ui_manager.d ./Core/Src/ui_manager.o ./Core/Src/ui_manager.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su

.PHONY: clean-Core-2f-Src

