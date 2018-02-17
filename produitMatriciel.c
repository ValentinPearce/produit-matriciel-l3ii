#define _GNU_SOURCE

#include <fcntl.h>

#include <pthread.h>

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <unistd.h>

/****** TYPES ******/
enum _State {
	STATE_WAIT,
	STATE_MULT,
	STATE_PRINT
};

typedef enum _State State;

struct _multiplyData {
	State state;
	pthread_cond_t cond;
	pthread_mutex_t mutex;
	size_t nbIterations;
	double ** m1;
	double ** m2;
	double ** m3;
	int ** pendingMult;
	unsigned int rowsM1, rowsM2, colsM1, colsM2, maxRows, maxCols;	
	int quantityScanned;
};

typedef struct _multiplyData multiplyData;

/****** GLOBAL DATA ******/
int processors;
multiplyData multData;

/****** FUNCTIONS ******/
void multiply(void * data){
	size_t index;
	size_t iter;

	index = *(int *)data;

	cpu_set_t multiplySet;
	CPU_ZERO(&multiplySet);
	CPU_SET(index%processors, &multiplySet);

}

int main(int argc, const char *argv[])
{
	size_t i, iter;
	pthread_t *multTh;
	char * mmappedFile;
	struct stat statbuf;
	int err;
	long pageSize = sysconf(_SC_PAGESIZE);

	if (argc != 2) {
		printf("Usage : %s <filename>\n", argv[0]);
		return 1;
	}

	/* CPU Settings */	
	processors = sysconf(_SC_NPROCESSORS_ONLN);
	printf("Number of processors : %d\n", processors);
	cpu_set_t mainCpuSet;
	CPU_ZERO(&mainCpuSet);

	/* Set mainCpuSet */
	CPU_SET(0, &mainCpuSet);
	if(sched_setaffinity(0, sizeof(cpu_set_t), &mainCpuSet) == -1){
		perror("sched_setaffinity");
	}
	printf("Main thread CPU : %d\n",sched_getcpu());

	/* Lecture du fichier pour déterminer le nombre de threads à créer */
	int fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("open");
		return 1;
	}
	lstat(argv[1], &statbuf);
	mmappedFile = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	mprotect(mmappedFile, (size_t) pageSize, PROT_READ);
	char * mmappedFileCursor = mmappedFile; // Curseur servant à la lecture depuis un point avancé du fichier
	double dummy; // Variable servant à récupérer des valeurs non utilisées après récupération.
	int displacement;
	unsigned int rowCursor = 0;
	unsigned int colCursor = 0;
	/****** Récupération du nombre d'itérations ******/

	/****** Initialisation des maximums ******/
	printf("Fetching maximums\n");
	multData.maxRows = 0;
	multData.maxCols = 0;
	for (iter = 0; iter < multData.nbIterations; iter++) {
		sscanf(mmappedFile, "%u%u%u%u%n", &multData.rowsM1, &multData.colsM1, &multData.rowsM2, &multData.colsM2, &displacement);
		mmappedFileCursor += displacement;	

		printf("Entering loop\n");
		if(multData.rowsM1 > multData.maxRows) { multData.maxRows = multData.rowsM1;}
		if(multData.colsM1 > multData.maxCols) { multData.maxCols = multData.colsM1;}
		if(multData.rowsM2 > multData.maxRows) { multData.maxRows = multData.rowsM2;}
		if(multData.colsM2 > multData.maxCols) { multData.maxCols = multData.colsM2;}
		printf("Ignoring first matrix");
		for (rowCursor = 0; rowCursor < multData.rowsM1; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM1; colCursor++) {
				sscanf(mmappedFileCursor, "%lf%n", &dummy, &displacement);
				mmappedFileCursor += displacement;	
			}
		}
		printf("Ignoring second matrix");
		for (rowCursor = 0; rowCursor < multData.rowsM2; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM2; colCursor++) {
				sscanf(mmappedFileCursor, "%lf%n", &dummy, &displacement);
				mmappedFileCursor += displacement;	
			}
		}
	}

	mmappedFileCursor = mmappedFile; // Réinitialisation du pointeur au début du fichier

	/* Initialisations (multiplyData, tableaux) */

	printf("Creating arrays\n");
	multData.state = STATE_WAIT;
	multData.m1 = malloc(multData.maxRows*sizeof(double *));
	multData.m2 = malloc(multData.maxRows*sizeof(double *));
	multData.m3 = malloc(multData.maxRows*sizeof(double *));
	multData.pendingMult = malloc(multData.maxRows*sizeof(int *));
	for (rowCursor = 0; rowCursor < multData.maxRows; rowCursor++) {
		multData.m1[rowCursor] = malloc(multData.maxCols*sizeof(double));
		multData.m2[rowCursor] = malloc(multData.maxCols*sizeof(double));
		multData.m3[rowCursor] = malloc(multData.maxCols*sizeof(double));
		multData.pendingMult[rowCursor] = malloc(multData.maxCols*sizeof(int));
	}



	err = close(fd);
	if (err == -1) {
		perror("close");
		return 1;
	}
	return 0;
}
