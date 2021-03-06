################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
C:/CredaCash/depends/blake2/blake2b.c \
C:/CredaCash/depends/blake2/blake2s.c 

OBJS += \
./src/blake2/blake2b.o \
./src/blake2/blake2s.o 

C_DEPS += \
./src/blake2/blake2b.d \
./src/blake2/blake2s.d 


# Each subdirectory must supply rules for building sources it contributes
src/blake2/blake2b.o: C:/CredaCash/depends/blake2/blake2b.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -IC:/CredaCash/depends -IC:/CredaCash/depends/boost -IC:/CredaCash/depends/skein -IC:/CredaCash/depends/blake2 -IC:/CredaCash/depends/sqlite -IC:/CredaCash/depends/keccak -IC:/CredaCash/depends/ed25519 -IC:/CredaCash/source/3rdparty/src/sqlite -IC:/CredaCash/source/3rdparty/src/ed25519 -IC:/CredaCash/source/cccommon/src -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -c -fmessage-length=0 -Wstrict-overflow=4 -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

src/blake2/blake2s.o: C:/CredaCash/depends/blake2/blake2s.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	gcc -D_DEBUG=1 -D_HAVE_SQLITE_CONFIG_H=1 -DED25519_REFHASH=1 -DED25519_CUSTOMRANDOM=1 -IC:/CredaCash/depends -IC:/CredaCash/depends/boost -IC:/CredaCash/depends/skein -IC:/CredaCash/depends/blake2 -IC:/CredaCash/depends/sqlite -IC:/CredaCash/depends/keccak -IC:/CredaCash/depends/ed25519 -IC:/CredaCash/source/3rdparty/src/sqlite -IC:/CredaCash/source/3rdparty/src/ed25519 -IC:/CredaCash/source/cccommon/src -O0 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -Wall -c -fmessage-length=0 -Wstrict-overflow=4 -isystem C:/CredaCash/depends/boost -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


