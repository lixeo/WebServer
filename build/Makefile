CXX = /usr/bin/g++-11
CFLAGS = -std=c++14 -O2 -Wall -g 
LDFLAGS = -pthread -lmysqlclient -lcrypt

TARGET = server
OBJS = ../code/log/*.cpp ../code/pool/*.cpp ../code/timer/*.cpp \
       ../code/http/*.cpp ../code/server/*.cpp \
       ../code/buffer/*.cpp ../code/bcrypt/bcrypt.cpp \
	   ../code/main.cpp

all: $(OBJS)
	$(CXX) $(CFLAGS) $(OBJS) -o ../bin/$(TARGET) $(LDFLAGS)

clean:
	rm -rf ../bin/$(TARGET)

