TARGET := NesNspire
GCC := nspire-gcc
CXX := nspire-g++
GENZEHN := genzehn
MAKE_PRG := make-prg

OBJS := \
	src/ndless/InfoNES_System_Ndless.o \
	src/InfoNES.o \
	src/InfoNES_Mapper.o \
	src/InfoNES_pAPU.o \
	src/K6502.o

CFLAGS := -O3 -marm -Isrc -Wall
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS := -Wl,--gc-sections

all: $(TARGET).tns

$(TARGET).tns: $(TARGET).elf
	$(GENZEHN) --input $^ --output $(TARGET).zehn --name $(TARGET)
	$(MAKE_PRG) $(TARGET).zehn $@

$(TARGET).elf: $(OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(GCC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).zehn $(TARGET).tns
