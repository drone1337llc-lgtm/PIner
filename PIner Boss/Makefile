CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

OBJS = main.o i2c_master.o stratum_client.o mining_coordinator.o sha256_utils.o terminal_ui.o uart_master.o web_server.o

all: pi_miner

pi_miner: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) -pthread -lssl -lcrypto

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

i2c_master.o: i2c_master.cpp i2c_master.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

stratum_client.o: stratum_client.cpp stratum_client.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

mining_coordinator.o: mining_coordinator.cpp mining_coordinator.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

sha256_utils.o: sha256_utils.cpp sha256_utils.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

terminal_ui.o: terminal_ui.cpp terminal_ui.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

uart_master.o: uart_master.cpp uart_master.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

web_server.o: web_server.cpp web_server.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) pi_miner

.PHONY: all clean
