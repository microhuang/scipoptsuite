OFLAGS          +=      -mtune=native  # -malign-double -mcpu=pentium4
CFLAGS		+=	-m64
CXXFLAGS	+=	-m64
LDFLAGS		+=      -m64

ifeq ($(SHARED),true)
LIBBUILDFLAGS	+=     	-m64
endif
