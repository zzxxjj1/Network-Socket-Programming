all: serverM.cpp serverA.cpp serverB.cpp client.cpp
	g++ -o serverM serverM.cpp
	g++ -o serverA serverA.cpp
	g++ -o serverB serverB.cpp
	g++ -o client client.cpp

clean:
	rm -f serverM serverA serverB client