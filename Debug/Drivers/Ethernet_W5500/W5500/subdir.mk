################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (12.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Drivers/Ethernet_W5500/W5500/w5500.c 

OBJS += \
./Drivers/Ethernet_W5500/W5500/w5500.o 

C_DEPS += \
./Drivers/Ethernet_W5500/W5500/w5500.d 


# Each subdirectory must supply rules for building sources it contributes
Drivers/Ethernet_W5500/W5500/%.o Drivers/Ethernet_W5500/W5500/%.su Drivers/Ethernet_W5500/W5500/%.cyclo: ../Drivers/Ethernet_W5500/W5500/%.c Drivers/Ethernet_W5500/W5500/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xE -c -I../Core/Inc -I"C:/Users/Acer/OneDrive/Documents/STM32/work/w5500_stm32rtc_http/Drivers/Ethernet_W5500" -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Drivers-2f-Ethernet_W5500-2f-W5500

clean-Drivers-2f-Ethernet_W5500-2f-W5500:
	-$(RM) ./Drivers/Ethernet_W5500/W5500/w5500.cyclo ./Drivers/Ethernet_W5500/W5500/w5500.d ./Drivers/Ethernet_W5500/W5500/w5500.o ./Drivers/Ethernet_W5500/W5500/w5500.su

.PHONY: clean-Drivers-2f-Ethernet_W5500-2f-W5500

