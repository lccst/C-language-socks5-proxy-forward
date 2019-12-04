all:
	make clean
	gcc proxy.c common.c -o lcc_proxy.out -lpthread -g
	gcc forward.c common.c -o lcc_forward.out -lpthread -g 

clean:
	rm -rf *.out