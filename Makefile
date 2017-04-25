TARGET := fixNES
DEBUG   = 0

OBJECTS :=
OBJECTS += main.o
OBJECTS += apu.o
OBJECTS += audio.o
OBJECTS += audio_fds.o
OBJECTS += audio_mmc5.o
OBJECTS += audio_vrc6.o
OBJECTS += audio_vrc7.o
OBJECTS += alhelpers.o
OBJECTS += cpu.o
OBJECTS += ppu.o
OBJECTS += mem.o
OBJECTS += input.o
OBJECTS += mapper.o
OBJECTS += mapperList.o
OBJECTS += fm2play.o
OBJECTS += vrc_irq.o
OBJECTS += mapper/fds.o
OBJECTS += mapper/m1.o
OBJECTS += mapper/m10.o
OBJECTS += mapper/m13.o
OBJECTS += mapper/m15.o
OBJECTS += mapper/m4.o
OBJECTS += mapper/m48.o
OBJECTS += mapper/m4add.o
OBJECTS += mapper/m7.o
OBJECTS += mapper/m9.o
OBJECTS += mapper/nsf.o
OBJECTS += mapper/p16c4.o
OBJECTS += mapper/p16c8.o
OBJECTS += mapper/p32c4.o
OBJECTS += mapper/p32c8.o
OBJECTS += mapper/p8c8.o
OBJECTS += mapper/vrc1.o
OBJECTS += mapper/vrc2_4.o
OBJECTS += mapper/vrc6.o
OBJECTS += mapper/vrc7.o


FLAGS    += -Wall -Wextra -msse -mfpmath=sse -ffast-math
FLAGS    += -Werror=implicit-function-declaration
DEFINES  += -DFREEGLUT_STATIC
INCLUDES += -I.

ifeq ($(DEBUG),1)
FLAGS += -O0 -g
else
FLAGS   += -O3
LDFLAGS += -s
endif

CFLAGS += $(FLAGS) $(DEFINES) $(INCLUDES)

LDFLAGS += $(CFLAGS) -lglut -lopenal -lGL -lGLU -lm

all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(TARGET) $(OBJECTS)


.PHONY: clean test
