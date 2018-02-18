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
	STATE_PRINT,
	STATE_DONE
};

typedef enum _State State;

struct _threadData { // Données spécifiques à chaque thread
	unsigned int col, row, cpu;
};
typedef struct _threadData threadData;

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

void initPendingMult(multiplyData * mD)
{
	size_t i,j;
	for(i=0;i<mD->maxRows;i++)
	{
		for (j = 0; j < mD->maxCols; j++) {
			if( i < mD->rowsM1 && j < mD->colsM2 ) { mD->pendingMult[i][j] = 1;}
			else { mD->pendingMult[i][j] = 0 ;}
		}
	}
}

int nbPendingMult(multiplyData * mD)
{
	size_t i,j;
	int nb = 0;
	for(i=0;i<mD->rowsM1;i++)
	{
		for (j = 0; j < mD->colsM2; j++) {
			nb += mD->pendingMult[i][j];
		}
	}
	return nb;
}

/****** GLOBAL DATA ******/
int processors;
multiplyData multData;

/****** FUNCTIONS ******/

void * multiply(void * data){
	threadData thD;
	size_t iter;
	unsigned int i, j;
	thD = *(threadData *)data;

	cpu_set_t multiplySet;
	CPU_ZERO(&multiplySet);
	CPU_SET(thD.cpu, &multiplySet);
	if(sched_setaffinity(0, sizeof(cpu_set_t), &multiplySet) == -1){
		perror("sched_setaffinity");
	}
	fprintf(stderr,"Begin multiply(%d,%d) on CPU %d\n",thD.row, thD.col,sched_getcpu());
	for(iter=0;iter<multData.nbIterations;iter++)  /* n'ont pas eu lieu              */
	{

		/*=>Attendre l'autorisation de multiplication POUR UNE NOUVELLE ITERATION...*/
		pthread_mutex_lock(&multData.mutex);
		while(multData.state != STATE_MULT || multData.pendingMult[thD.row][thD.col] == 0){
			pthread_cond_wait(&multData.cond, &multData.mutex);
			if(multData.state == STATE_DONE){
				printf("State_Done\n");
				return NULL;
			}
		}
		pthread_mutex_unlock(&multData.mutex);
		fprintf(stderr,"--> mult(%d,%d)\n",thD.row, thD.col); /* La multiplication peut commencer */

		multData.m3[thD.row][thD.col] = multData.m1[thD.row][0]*multData.m2[0][thD.col];

		/*=>Effectuer la multiplication a l'index du thread courant... */
		for (i = 1; i < multData.rowsM1; i++) {
			for (j = 1; j < multData.colsM2; j++) {
				multData.m3[thD.row][thD.col] += multData.m1[thD.row][j]*multData.m2[i][thD.col];
			}
		}

		fprintf(stderr,"<-- mult(%d,%d) : %.3g\n",           /* Affichage du */
				thD.row,thD.col,multData.m3[thD.row][thD.col]);/* calcul sur   */
		/* l'index      */
		/*=>Marquer la fin de la multiplication en cours... */
		multData.pendingMult[thD.row][thD.col] = 0;
		/*=>Si c'est la derniere... */
		if(nbPendingMult(&multData) == 0)
		{
			/*=>Autoriser le demarrage de l'addition... */
			multData.state = STATE_PRINT;
			pthread_cond_broadcast(&multData.cond);
		}
	}
	fprintf(stderr,"Quit mult(%d,%d)\n",thD.col, thD.row);
	return NULL;
}

int main(int argc, const char *argv[])
{
	size_t iter;
	threadData *thData;
	pthread_t *multTh;
	char * mmappedFile;
	struct stat statbuf;
	int err;
	long pageSize = sysconf(_SC_PAGESIZE);

	if (argc != 2) {
		printf("Usage : %s <filename>\n", argv[0]);
		return 1;
	}
	
	FILE * results;
	results = fopen("results", "w");
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
	multData.maxRows = 0;
	multData.maxCols = 0;

	sscanf(mmappedFile, "%u%n", &multData.nbIterations, &displacement);
	mmappedFileCursor += displacement;	
	for (iter = 0; iter < multData.nbIterations; iter++) {
		sscanf(mmappedFileCursor, "%u%u%u%u%n", &multData.rowsM1, &multData.colsM1, &multData.rowsM2, &multData.colsM2, &displacement);
		mmappedFileCursor += displacement;	

		if(multData.rowsM1 > multData.maxRows) { multData.maxRows = multData.rowsM1;}
		if(multData.colsM1 > multData.maxCols) { multData.maxCols = multData.colsM1;}
		if(multData.rowsM2 > multData.maxRows) { multData.maxRows = multData.rowsM2;}
		if(multData.colsM2 > multData.maxCols) { multData.maxCols = multData.colsM2;}
		for (rowCursor = 0; rowCursor < multData.rowsM1; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM1; colCursor++) {
				sscanf(mmappedFileCursor, "%lf%n", &dummy, &displacement);
				mmappedFileCursor += displacement;	
			}
		}
		for (rowCursor = 0; rowCursor < multData.rowsM2; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM2; colCursor++) {
				sscanf(mmappedFileCursor, "%lf%n", &dummy, &displacement);
				mmappedFileCursor += displacement;	
			}
		}
	}

	mmappedFileCursor = mmappedFile; // Réinitialisation du pointeur au début du fichier

	/* Initialisations (multiplyData, tableaux) */

	multData.state = STATE_WAIT;

	printf("Creating arrays\n");
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

	/*=>initialiser multData.mutex ... */
	if(pthread_mutex_init(&multData.mutex, NULL) != 0){
		perror("pthread_mutex_init()\n");
	}
	/*=>initialiser multData.cond ...  */
	if (pthread_cond_init(&multData.cond, NULL)) {
		perror("pthread_cond_init()\n");
	}

	/* Initialisation des threads */
	multTh = malloc(multData.maxCols*multData.maxRows*sizeof(pthread_t));
	thData = malloc(multData.maxCols*multData.maxRows*sizeof(threadData));
	for (rowCursor = 0; rowCursor < multData.maxRows; rowCursor++) {
		for (colCursor = 0; colCursor < multData.maxCols; colCursor++) {
			thData[rowCursor*multData.maxCols+colCursor].row = rowCursor;
			thData[rowCursor*multData.maxCols+colCursor].col = colCursor;
			thData[rowCursor*multData.maxCols+colCursor].cpu = (rowCursor*multData.maxCols+colCursor)%processors;
			pthread_create(&multTh[(rowCursor*multData.maxCols+colCursor)], NULL, multiply, &thData[(rowCursor*multData.maxCols+colCursor)]);
		}
	}
	sscanf(mmappedFile, "%u%n", &multData.nbIterations, &displacement);
	mmappedFileCursor += displacement;	
	fprintf(results, "%d\n", multData.nbIterations);
	for (iter = 0; iter < multData.nbIterations; iter++) {
		sscanf(mmappedFileCursor, "%u%u%u%u%n", &multData.rowsM1, &multData.colsM1, &multData.rowsM2, &multData.colsM2, &displacement);
		mmappedFileCursor += displacement;	

		for (rowCursor = 0; rowCursor < multData.rowsM1; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM1; colCursor++) {
				sscanf(mmappedFileCursor, "%lf%n", &multData.m1[rowCursor][colCursor], &displacement);
				mmappedFileCursor += displacement;	
			}
		}
		for (rowCursor = 0; rowCursor < multData.rowsM2; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM2; colCursor++) {
				sscanf(mmappedFileCursor, "%lf%n", &multData.m2[rowCursor][colCursor], &displacement);
				mmappedFileCursor += displacement;	
			}
		}
		/*=>Autoriser le demarrage des multiplications pour une nouvelle iteration..*/
		initPendingMult(&multData);
		multData.state = STATE_MULT;
		pthread_cond_broadcast(&multData.cond);

		/*=>Attendre l'autorisation d'affichage...*/
		pthread_mutex_lock(&multData.mutex);
		while(multData.state != STATE_PRINT){
			pthread_cond_wait(&multData.cond, &multData.mutex);
		}

		/*=>Afficher le resultat de l'iteration courante...*/
		printf("ITERATION %d, RESULT : \n", iter);
		fprintf(results, "%d %d \n", multData.rowsM1, multData.colsM2);
		for (rowCursor = 0; rowCursor < multData.rowsM1; rowCursor++) {
			for (colCursor = 0; colCursor < multData.colsM2; colCursor++) {
				printf("%f ", multData.m3[rowCursor][colCursor]);
				fprintf(results, "%f ", multData.m3[rowCursor][colCursor]);
			}
			printf("\n");
			fprintf(results, "\n");
		}
		pthread_mutex_unlock(&multData.mutex);
	}
	/* Avertissement aux threads non terminés de la fin de l'execution */
	multData.state = STATE_DONE;
	pthread_cond_broadcast(&multData.cond);

	fclose(results);

	/* Fermeture des threads */
	printf("Closing threads\n");
	for (rowCursor = 0; rowCursor < multData.maxRows*multData.maxCols; rowCursor++) {
		pthread_join(multTh[rowCursor], NULL);
		printf("Closed thread n°%d\n", rowCursor);
	}
	printf("Threads closed\n");

	/*=> detruire multData.cond ... */
	pthread_cond_destroy(&multData.cond);
	/*=> detruire multData.mutex ... */
	pthread_mutex_destroy(&multData.mutex);
	/*** Libération de la mémoire ***/
	munmap(mmappedFile, statbuf.st_size);
	for (rowCursor = 0; rowCursor < multData.maxRows; rowCursor++) {
	free(multData.m1[rowCursor]);
	free(multData.m2[rowCursor]);
	free(multData.m3[rowCursor]);
	free(multData.pendingMult[rowCursor]);
	}
	free(multData.m1);
	free(multData.m2);
	free(multData.m3);
	free(multData.pendingMult);
	free(thData);
	free(multTh);
	err = close(fd);
	if (err == -1) {
		perror("close");
		return 1;
	}
	
	return 0;
}
