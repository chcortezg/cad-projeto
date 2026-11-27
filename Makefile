preprocess: compilar roadnet livejournal

compilar:
	gcc -O2 -fopenmp datasets/preprocessador.c -o datasets/preprocessador

roadnet: compilar
	./datasets/preprocessador datasets/roadNet-CA.txt/roadNet-CA.txt datasets/roadNet-CA.csr.bin

livejournal: compilar
	./datasets/preprocessador datasets/soc-LiveJournal1.txt/soc-LiveJournal1.txt datasets/soc-LiveJournal1.csr.bin
	./datasets/preprocessador datasets/soc-LiveJournal1.txt/soc-LiveJournal1.txt datasets/soc-LiveJournal1-dirigido.csr.bin dirigido

orkut: compilar
	./datasets/preprocessador datasets/com-orkut.ungraph.txt/com-orkut.ungraph.txt datasets/com-orkut.ungraph.csr.bin

.PHONY: preprocess compilar roadnet livejournal

sequencial-roadnet:
	gcc -O2 -std=c11 ./src/sequencial.c -o ./outputs/sequencial
	./outputs/sequencial ../processador-dataset/datasets/roadNet-CA.csr.bin 0

sequencial-livejournal:
	gcc -std=c11 ./src/sequencial.c -o ./outputs/sequencial
	./outputs/sequencial ../processador-dataset/datasets/soc-LiveJournal1.csr.bin

sequencial-livejournal-dir:
	gcc -std=c11 ./src/sequencial.c -o ./outputs/sequencial
	./outputs/sequencial ../processador-dataset/datasets/soc-LiveJournal1.csr.bin

sequencial-orkut:
	gcc -std=c11 ./src/sequencial.c -o ./outputs/sequencial
	./outputs/sequencial ../processador-dataset/datasets/com-orkut.ungraph.csr.bin