#gcc -Wall -o cisterna sscistpi.c -lwiringPi
#gcc -Wall -o build/cisterna src/cisterna.c -lwiringPi -lm

# g++ -std=c++11 -o build/cisterna src/cisterna.c -lpistache -lpthread -lwiringPi -lm
#g++ -std=c++11 -o build/cisterna src/cisterna.c -lpistache -lpthread -lwiringPi -lm -Ofast


g++ -std=c++11 -o build/cisterna cisterna.c -lpistache -lpthread -lwiringPi -lm -Ofast
