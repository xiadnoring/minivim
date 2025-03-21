CC = gcc
CC_ARGS_DRV = -fno-stack-protector -ffreestanding -O0 -g -fPIC -static -nostdlib -m32
CC_ARGS = -fno-stack-protector -ffreestanding -O0 -g -fPIC -static -nostdlib -m32
# CC_ARGS_DRV = -fno-stack-protector -ffreestanding -O0 -g -fPIC -static
# CC_ARGS = -fno-stack-protector -ffreestanding -O0 -g -fPIC -static
CC_INC  = -I "../../usr/include/"
CC_LIB = \
../../usr/lib/libc.o
# CC_INC =
# CC_LIB =
all:
#	$(CC) $(CC_ARGS) $(CC_INC) -Wno-builtin-declaration-mismatch -o main main.c ${CC_LIB} -DMINIVIM_LINUX
	$(CC) $(CC_ARGS) $(CC_INC) -Wno-builtin-declaration-mismatch -o main main.c ${CC_LIB}