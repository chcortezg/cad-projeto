preprocess: compilar roadnet livejournal

compilar:
	gcc -O2 -fopenmp datasets/preprocessador.c -o datasets/preprocessador

roadnet: compilar
	./datasets/preprocessador datasets/roadNet-CA.txt/roadNet-CA.txt datasets/roadNet-CA.csr.bin

livejournal: compilar
	./datasets/preprocessador datasets/soc-LiveJournal1.txt/soc-LiveJournal1.txt datasets/soc-LiveJournal1.csr.bin
	./datasets/preprocessador datasets/soc-LiveJournal1.txt/soc-LiveJournal1.txt datasets/soc-LiveJournal1-dirigido.csr.bin dirigido

.PHONY: preprocess compilar roadnet livejournal