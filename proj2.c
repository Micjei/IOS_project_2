/*
*   IOS - 2. projekt (synchronizace)
*   author: Michael Babušík
*   xlogin: xbabus01
*   datum odevzdání 30.4.2023
*/

#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <semaphore.h>

typedef struct{
    int* customer_ids;
    int posta_open;
    int* clerk_ids;
    int customers;
    int* queue1;
    int* queue2;
    int* queue3;
    int front1;
    int front2;
    int front3;
    int rear1;
    int rear2;
    int rear3;
    int A;
    int a;
    int b;
    int c;
} shared_memory;

sem_t *line_three = NULL;
sem_t *line_one = NULL;
sem_t *customer = NULL;
sem_t *id_mutex = NULL;
sem_t *line_two = NULL;
sem_t *s_print = NULL;
sem_t *clerk = NULL;

shared_memory *shrm = NULL;

FILE *pfile = NULL;

//Vytváření sdílené paměti
void create_mem(int CUSTOMER_MAX_ID, int CLERK_MAX_ID){
    shrm = mmap(NULL, sizeof(shared_memory), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if(shrm == MAP_FAILED){
		fprintf(stderr, "ERROR: vytvareni sdilene pameti!\n");
		exit(1);
	}

    shrm->c = 0;
    shrm->b = 0;
    shrm->a = 0;
    shrm->A = 1;
    shrm->rear3 = -1;
    shrm->rear2 = -1;
    shrm->rear1 = -1;
    shrm->front3 = -1;
    shrm->front2 = -1;
    shrm->front1 = -1;
    shrm->customers = 0;
    shrm->posta_open = 1;
    shrm->queue1 = mmap(NULL, CUSTOMER_MAX_ID * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); //pole zákazníků pro 1. řadu
    shrm->queue2 = mmap(NULL, CUSTOMER_MAX_ID * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); //pole zákazníků pro 2. řadu
    shrm->queue3 = mmap(NULL, CUSTOMER_MAX_ID * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); //pole zákazníků pro 3. řadu
    shrm->clerk_ids = mmap(NULL, CLERK_MAX_ID * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); // pole id zákazníků
    shrm->customer_ids = mmap(NULL, CUSTOMER_MAX_ID * sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); // pole id úředníků

    if(shrm->customer_ids == MAP_FAILED || shrm->clerk_ids == MAP_FAILED || shrm->queue1 == MAP_FAILED || shrm->queue2 == MAP_FAILED || shrm->queue3 == MAP_FAILED) {
        fprintf(stderr, "ERROR: vytvareni sdilene pameti!\n");
        exit(1);
    }
    memset(shrm->customer_ids, 0, CUSTOMER_MAX_ID * sizeof(int)); // inicializace na 0 
    memset(shrm->clerk_ids, 0, CLERK_MAX_ID * sizeof(int)); // inicializace na 0
}

// mazání sdílené paměti
void destroy_mem(){
    if(munmap(shrm, sizeof(shared_memory))== -1){
        fprintf(stderr, "ERROR: mazani sdilene pameti!\n");
        exit(1);
    }
}

// zavírání semaforů
void close_sem(sem_t **sem){
    if (sem_close(*sem) == -1){
		fprintf(stderr, "ERROR: zavirani semaforu!\n");
		exit(1);
	}
}

//unlink semaforů
void unlink_sem(char *name){
    if (sem_unlink(name) == -1){
		fprintf(stderr, "ERROR: unlink semaforu!\n");
		exit(1);
	}
}

//otevírání semaforů
void open_sem(sem_t **sem, char *name, int value){
    *sem = sem_open(name, O_CREAT | O_EXCL, 0666, value);
    if (*sem == SEM_FAILED){
        fprintf(stderr, "ERROR: otevirani semaforu!\n");
        exit(1);
    }
}

//Vytáření semaforů
void create_sem() {
    open_sem(&s_print, "/xbabus01.ios.proj2.s_print", 1); // semafor pro print
    open_sem(&customer, "/xbabus01.ios.proj2.customer", 1); // semafor pro úředníka, kdy kontroluje otevřenost pošty
    open_sem(&clerk, "/xbabus01.ios.proj2.clerk", 1); // semafor pro úředníka do whilu
    open_sem(&id_mutex, "/xbabus01.ios.proj2.id_mutex", 1); // semafor pro generování id
    open_sem(&line_one, "/xbabus01.ios.proj2.line_one", 0); // semafor pro 1. řadu
    open_sem(&line_two, "/xbabus01.ios.proj2.line_two", 0); // semafor pro 2. řadu
    open_sem(&line_three, "/xbabus01.ios.proj2.line_three", 0); // semafor pro 2. řadu
}

//ničení semaforů
void destroy_sem() {
    close_sem(&s_print);
    unlink_sem("/xbabus01.ios.proj2.s_print");
    close_sem(&customer);
    unlink_sem("/xbabus01.ios.proj2.customer");
    close_sem(&clerk);
    unlink_sem("/xbabus01.ios.proj2.clerk");
    close_sem(&id_mutex);
    unlink_sem("/xbabus01.ios.proj2.id_mutex");
    close_sem(&line_one);
    unlink_sem("/xbabus01.ios.proj2.line_one");
    close_sem(&line_two);
    unlink_sem("/xbabus01.ios.proj2.line_two");
    close_sem(&line_three);
    unlink_sem("/xbabus01.ios.proj2.line_three");
}

//náhodné uspávání v intervalu
void random_sleep(int wait_time) {
    if (wait_time > 0) {
        srand(time(NULL));
        usleep((rand() % wait_time) * 1000);
    } 
    else usleep(0);
}

//náhodný čas do zavření pošty
void posta_close(int wait_time) {
    if (wait_time > 0) {
        srand(time(NULL));
        usleep(((rand() % (wait_time/2)) + (wait_time/2)) * 1000);
    } 
    else usleep(0);
    sem_wait(s_print);
    fprintf(pfile, "%d: closing\n", shrm->A);
    fflush(pfile);
    shrm->posta_open = 0;    
    shrm->A++;
    sem_post(s_print);
}

//kontrola, jestli jsou argumenty pouze čísla
void check_number(char *argument) {
    while (*argument != '\0') {
        if (!isdigit(*argument)){
            fprintf(stderr, "ERROR: Zadejte prosim jenom cisla\n");
            exit(1);
        }
        argument++;
    }
}

//kontrola argumentů
int check_arguments(int argc, char *argv[]){
    if (argc != 6){
        fprintf(stderr, "ERROR: Chybny pocet argumentu\n");
        exit(1);
    }

    check_number(argv[1]);
    check_number(argv[2]);
    check_number(argv[3]);
    check_number(argv[4]);
    check_number(argv[5]);

    if (atoi(argv[3]) < 0 || atoi(argv[3]) > 10000 || atoi(argv[4]) < 0 || atoi(argv[4]) > 100 || atoi(argv[5]) < 0 || atoi(argv[5]) > 10000) {
        fprintf(stderr, "ERROR: Chybne hodnoty argumentu\n");
        exit(1);
    }
    return 0;
}

//generování ID pro zákazníky a úředníky
int generate_id(int MAX_ID, int* used_ids) {
    int id;
    do {
        id = rand() % MAX_ID; // generujeme náhodné číslo v daném rozmezí (bez nuly)
        id++; // inkrementujeme výsledek o jedna
    } while (used_ids[id] == 1); // dokud toto číslo již není použité, opakujeme
    used_ids[id] = 1; // označíme, že jsme toto číslo použili
    return id;
}

//zákazník proces
void create_customers(int num_customers, int wait_time){
    //generovani id
    sem_wait(id_mutex);
    int Cus_id = generate_id(num_customers, shrm->customer_ids);
    sem_post(id_mutex);

    //vytisknutí started
    sem_wait(s_print);
    fprintf(pfile, "%d: Z %d: started\n", shrm->A, Cus_id);
    fflush(pfile);
    shrm->A++;
    sem_post(s_print);

    //čekám náhodný čas v intervalu <0,TZ>
    random_sleep(wait_time);

    //vybírám random číslo od 1 do 3 a podle toho se zařadám do určité fronty
    time_t t;
    srand((int)time(&t) % getpid());
    int X = (rand()%3)+1;

    //kontrola otevrenosti posty
    sem_wait(customer);
    sem_wait(s_print);
    if (shrm->posta_open == 0){
        sem_post(s_print);

        sem_wait(s_print);
        fprintf(pfile, "%d: Z %d: going home\n", shrm->A, Cus_id);
        fflush(pfile);
        shrm->A++;
        sem_post(s_print);
    
        sem_post(customer);
        exit(0);
    }
    //výtisk, do jaké fronty jdu
    fprintf(pfile, "%d: Z %d: entering office for a service %d\n", shrm->A, Cus_id, X);
    fflush(pfile);
    shrm->A++;
    sem_post(customer);
    sem_post(s_print);

    //zařazuju se do fronty podle čísla 1-3
    if (X == 1){
        if (shrm->front1 == -1) { // Pokud je fronta prázdná
            shrm->front1 = 0;
        }
        //konec fronty ++ a zařadit id zákazníka na první volné místo a zařadím ho do počtu zákazníků ve frontě
        shrm->rear1++;
        shrm->queue1[shrm->rear1] = Cus_id;
        shrm->customers++;

        //čekám na zavolání úředníkem
        sem_wait(line_one);

        //výtisk, zákazník je ve frontě, nový řádek
        sem_wait(s_print);
        fprintf(pfile, "%d: Z %d: called by office worker\n", shrm->A, Cus_id);
        fflush(pfile);
        shrm->A++;
        sem_post(s_print);

        //čekaní na dokončení tasku na úředníka
        sem_wait(line_one);
    }
    else if (X == 2){
        if (shrm->front2 == -1) { // Pokud je fronta prázdná
            shrm->front2 = 0;
        }
        //konec fronty ++ a zařadit id zákazníka na první volné místo a zařadím ho do počtu zákazníků ve frontě
        shrm->rear2++;
        shrm->queue2[shrm->rear2] = Cus_id;
        shrm->customers++;

        //čekám na zavolání úředníkem
        sem_wait(line_two);

        //výtisk, zákazník je ve frontě, nový řádek
        sem_wait(s_print);
        fprintf(pfile, "%d: Z %d: called by office worker\n", shrm->A, Cus_id);
        fflush(pfile);
        shrm->A++;
        sem_post(s_print);

        //čekaní na dokončení tasku na úředníka
        sem_wait(line_two);
    }
    else{
        if (shrm->front3 == -1) { // pokud je fronta prázdná
            shrm->front3 = 0;
        }
        //konec fronty ++ a zařadit id zákazníka na první volné místo a zařadím ho do počtu zákazníků ve frontě
        shrm->rear3++;
        shrm->queue3[shrm->rear3] = Cus_id;
        shrm->customers++;

        //čekám na zavolání úřerníkem
        sem_wait(line_three);
        
        //výtisk, zákazník je ve frontě, nový řádek
        sem_wait(s_print);
        fprintf(pfile, "%d: Z %d: called by office worker\n", shrm->A, Cus_id);
        fflush(pfile);
        shrm->A++;
        sem_post(s_print);

        //čekaní na dokončení tasku na úředníka
        sem_wait(line_three);
    }
    //random sleep od 0 do 10
    random_sleep(10);

    //žádost hotová, jdu domů
    sem_wait(s_print);
    fprintf(pfile, "%d: Z %d: going home\n", shrm->A, Cus_id);
    fflush(pfile);
    shrm->A++;
    sem_post(s_print);
}

//úředník proces
void create_clerks(int num_clerks, int wait_time){
    //generovani id 
    sem_wait(id_mutex);
    int clerk_id = generate_id(num_clerks, shrm->clerk_ids);
    sem_post(id_mutex);   

    //vytisknutí started
    sem_wait(s_print);
    fprintf(pfile, "%d: U %d: started\n", shrm->A, clerk_id);
    fflush(pfile);
    shrm->A++;
    sem_post(s_print);

    //while cyklus
    while(shrm->posta_open == 1){
        while(shrm->customers != 0){
            //cekam na toho před sebou, než si vybere frontu
            sem_wait(clerk);
            if (shrm->customers == 0 && shrm->posta_open == 0){
                sem_wait(s_print);
                fprintf(pfile, "%d: U %d: going home\n", shrm->A, clerk_id);
                fflush(pfile);
                shrm->A++;
                sem_post(s_print);
                
                sem_post(clerk);
                exit(0);
            }
            //random číslo od 1-3
            time_t t;
            srand((int)time(&t) % getpid());
            int o = rand() % 3 + 1;

            //chci obloužit frontu 1, a zákazníci ještě ve frontách jsou
            if (o == 1 && shrm->customers != 0){
                //fronta je neprázdná?
                if (shrm->queue1[shrm->a] != 0){
                    //posunu se v poli
                    shrm->a++;

                    //zavolám si zákazníka
                    sem_post(line_one);
                    
                    //obsluhuju zakaznika
                    sem_wait(s_print);
                    fprintf(pfile, "%d: U %d: serving a servise of type 1\n", shrm->A, clerk_id);
                    fflush(pfile);
                    shrm->A++;
                    sem_post(s_print);
                    
                    //trvá, než obsloužím
                    random_sleep(10);

                    //skončil jsem
                    sem_post(line_one);
                    
                    sem_wait(s_print);
                    fprintf(pfile, "%d: U %d: servise finished\n", shrm->A, clerk_id);
                    fflush(pfile);
                    shrm->A++;
                    sem_post(s_print);

                    //odeberu zákazníka z fronty
                    shrm->customers--;
                }
            }
            //chci obloužit frontu 2, a zákazníci ještě ve frontách jsou
            else if (o == 2 && shrm->customers != 0){
                //fronta je neprázdná?
                if (shrm->queue2[shrm->b] != 0){
                    //posunu se v poli
                    shrm->b++;

                    //zavolám si zákazníka
                    sem_post(line_two);

                    //obsluhuju zakaznika
                    sem_wait(s_print);
                    fprintf(pfile, "%d: U %d: serving a servise of type 2\n", shrm->A, clerk_id);
                    fflush(pfile);
                    shrm->A++;
                    sem_post(s_print);

                    //trvá, než obsloužím
                    random_sleep(10);

                    //skončil jsem
                    sem_post(line_two);

                    sem_wait(s_print);
                    fprintf(pfile, "%d: U %d: servise finished\n", shrm->A, clerk_id);
                    fflush(pfile);
                    shrm->A++;
                    sem_post(s_print);
                
                    //odeberu zákazníka z fronty
                    shrm->customers--;
                }
            }
            //chci obloužit frontu 3, a zákazníci ještě ve frontách jsou
            else if (o == 3 && shrm->customers != 0){
                //fronta je neprázdná?
                if (shrm->queue3[shrm->c] != 0){
                    shrm->c++;

                    //zavolám si zákazníka
                    sem_post(line_three);

                    //obsluhuju zakaznika
                    sem_wait(s_print);
                    fprintf(pfile, "%d: U %d: serving a servise of type 3\n", shrm->A, clerk_id);
                    fflush(pfile);
                    shrm->A++;
                    sem_post(s_print);

                    //trvá, než obsloužím
                    random_sleep(10);

                    //skončil jsem
                    sem_post(line_three);

                    sem_wait(s_print);
                    fprintf(pfile, "%d: U %d: servise finished\n", shrm->A, clerk_id);
                    fflush(pfile);
                    shrm->A++;
                    sem_post(s_print);
                
                    //odeberu zákazníka z fronty
                    shrm->customers--;
                }
            }
            else{
                //zákazníci nejsou ve frontě, vezmu si pauzu
                sem_wait(s_print);
                fprintf(pfile, "%d: U %d: taking break\n", shrm->A, clerk_id);
                fflush(pfile);
                shrm->A++;
                sem_post(s_print);
                
                //trvání pauzy
                random_sleep(wait_time);

                //skoncila mi pauza
                sem_wait(s_print);
                fprintf(pfile, "%d: U %d: break finished\n", shrm->A, clerk_id);
                fflush(pfile);
                shrm->A++;
                sem_post(s_print);
            }
            //pustim toho za sebou vybrat frontu
            sem_post(clerk);
        }
    }
    //posta je zavrena, zamestnanci jdou domů
    sem_wait(s_print);
    fprintf(pfile, "%d: U %d: going home\n", shrm->A, clerk_id);
    fflush(pfile);
    shrm->A++;
    sem_post(s_print);
}

//hlavní proces
int main(int argc, char *argv[]){
    //otevírání výstupního souboru
    pfile = fopen("proj2.out","w");
    if (pfile == NULL){
        fprintf(stderr, "ERROR: otevíraní výstupního souboru");
    }
    pid_t pid_customerproc, pid_clerksproc;

    check_arguments(argc, argv);
    int NZ = atoi(argv[1]); // počet zákazníků
    int NU = atoi(argv[2]); // počet úředníků
    int TZ = atoi(argv[3]); // maximální čas v ms, po který zákazník po svém vytvoření čeká, než vejde na poštu (eventuálně odchází s nepořízenou)
    int TU = atoi(argv[4]); // maximální délka přestávky úředníka v ms
    int F = atoi(argv[5]); // maximální čas v ms, po kterém je uzavřena pošta pro nově příchozí

    /*******************************************/
    create_mem(NZ, NU);
    create_sem();
    
    for(int z = 0; z < NZ; z++){
        pid_customerproc = fork();
        if(pid_customerproc == 0){
            create_customers(NZ, TZ);
            exit(0);
        }
    }
    for (int u = 0; u < NU; u++){
        pid_clerksproc = fork();
        if(pid_clerksproc == 0){
            create_clerks(NU, TU);
            exit(0);
        }
    }

    posta_close(F);

    while(wait(NULL) > 0);
    
    destroy_sem();
    destroy_mem();
    /*******************************************/

    fclose(pfile);
    exit(0);
}
