
preprocess:
	cd datasets
	gcc -O2 -fopenmp preprocessador.c -o preprocessador
	./preprocessador com-orkut.ungraph.txt com-orkut.csr.bin