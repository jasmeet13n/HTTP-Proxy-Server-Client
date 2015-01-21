client.o: proxy.o
	g++ -Wall client.cpp -o client
proxy.o: 
	g++ -Wall proxy.cpp -o proxy
clean:
	rm -rf client
	rm -rf proxy
	rm -rf client.o
	rm -rf proxy.o
	rm -rf a.out