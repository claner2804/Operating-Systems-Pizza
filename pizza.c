#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

#define COUNTER_MAX_CAPACITY 30 // Maximale Kapazität der Pizzatheke
#define SUPPLIER_TRIGGER_THRESHOLD 20 // Lieferant wird informiert, wenn 2/3 der Theke gefüllt sind
#define PIZZAIOLO_COUNT 6 // Anzahl der Pizzaiolos (Threads)
#define SIMULATION_DURATION 40 // Timer zum automatischen shutdown

typedef struct{
    int countMargherita;
    int countMarinara;
    pthread_mutex_t mutex; // Mutex zum Schutz des Thekenbestands
} counter_t;

// Condition Variables und ihre zugehörigen Mutexes für Synchronisation
pthread_cond_t supplier_cond;
pthread_mutex_t supplier_mutex;

pthread_cond_t pizzaiolo_cond;
pthread_mutex_t pizzaiolo_mutex;

counter_t counter;

volatile sig_atomic_t shutdownFlag = 0; // Flag zum sauberen Beenden des Programms
volatile sig_atomic_t supplierInformed = 0; // Flag, damit der Lieferant nur einmal informiert wird

// Funktion für die Pizzaiolos (Threads)
void* bakePizza(void* arg){
    while (!shutdownFlag){
        int pizzaType = rand() % 2; // 0 = Margherita, 1 = Marinara

        // Kritischer Bereich beginnt - Schutz des Thekenbestands
        pthread_mutex_lock(&counter.mutex);
        // Pizza backen, wenn noch Platz auf der Theke ist
        if ((counter.countMargherita + counter.countMarinara) < COUNTER_MAX_CAPACITY){
            if (pizzaType == 0) {
                printf("Margherita Pizza wird auf die Theke gelegt\n");
                counter.countMargherita += 1;
            } else {
                printf("Marinara Pizza wird auf die Theke gelegt\n");
                counter.countMarinara += 1;
            }
            // Lieferant informieren, wenn 2/3 der Theke gefüllt sind
            if ((counter.countMargherita + counter.countMarinara) >= SUPPLIER_TRIGGER_THRESHOLD){
                pthread_mutex_lock(&supplier_mutex);
                if (supplierInformed == 0) {
                    printf("Lieferant wird informiert\n");
                    pthread_cond_signal(&supplier_cond); // Lieferant wecken
                    supplierInformed = 1;
                }
                pthread_mutex_unlock(&supplier_mutex);
            }
            pthread_mutex_unlock(&counter.mutex); // Kritischer Bereich endet
            sleep(1); // Simuliert Zeit zum Backen einer Pizza
        } else {
            pthread_mutex_unlock(&counter.mutex); // Kritischer Bereich endet
            // Warte, bis Platz auf der Theke frei wird
            printf("Warte bis Platz auf der Theke frei wird\n");
            pthread_mutex_lock(&pizzaiolo_mutex);
            while ((counter.countMargherita + counter.countMarinara) >= COUNTER_MAX_CAPACITY) {
                pthread_cond_wait(&pizzaiolo_cond, &pizzaiolo_mutex); // Warten, bis Platz frei wird
            }
            pthread_mutex_unlock(&pizzaiolo_mutex);
            printf("Warte bis Platz auf der Theke frei wird: Aufgeweckt\n");
        }
    }
    printf("Pizzaiolo beendet sich\n");
    return NULL;
}

// Funktion für den Lieferanten (Thread)
void* deliverPizza(void* arg){
    while (!shutdownFlag){
        // Lieferant wartet auf Signal zum Abholen der Pizzen
        printf("Lieferant wartet auf Anruf\n");
        pthread_mutex_lock(&supplier_mutex);
        pthread_cond_wait(&supplier_cond, &supplier_mutex); // Warten, bis er benachrichtigt wird
        pthread_mutex_unlock(&supplier_mutex);
        printf("Lieferant hat Anruf bekommen\n");

        if (!shutdownFlag){
            // Simuliert das Abholen der Pizzen (zwischen 1-4 Sekunden)
            sleep((rand() % 4) + 1);
            // Theke leeren
            printf("Lieferant hat die Theke geleert\n");
            pthread_mutex_lock(&counter.mutex);
            counter.countMargherita = 0;
            counter.countMarinara = 0;
            pthread_mutex_unlock(&counter.mutex);
            supplierInformed = 0; // Lieferant kann erneut benachrichtigt werden
            // Alle Pizzaiolos wecken, da die Theke nun leer ist
            pthread_mutex_lock(&pizzaiolo_mutex);
            pthread_cond_broadcast(&pizzaiolo_cond); // Weckt alle Pizzaiolos
            pthread_mutex_unlock(&pizzaiolo_mutex);
        }
    }
    printf("Lieferant beendet sich\n");
    return NULL;
}

// Signalfunktion für Qualitätskontrolle - Entfernt zufällig einige Pizzen aus der Theke
void qualityCheck(int signum){
    printf("Qualitätskontrolle");
    int countMargheritaToRemove = (rand() % (counter.countMargherita + 1));
    int countMarinaraToRemove = (rand() % (counter.countMarinara + 1));

    pthread_mutex_lock(&counter.mutex);
    counter.countMargherita -= countMargheritaToRemove;
    counter.countMarinara -= countMarinaraToRemove;
    pthread_mutex_unlock(&counter.mutex);

    printf("Qualitätskontrolle: %d Margherita und %d Marinara wurden entfernt, da sie kalt waren\n", countMargheritaToRemove, countMarinaraToRemove);

    // Pizzaiolos wecken, um weiter Pizzen zu backen
    pthread_mutex_lock(&pizzaiolo_mutex);
    pthread_cond_broadcast(&pizzaiolo_cond); // Weckt alle Pizzaiolos
    pthread_mutex_unlock(&pizzaiolo_mutex);
    alarm(5); // Setzt den Alarm für die nächste Qualitätskontrolle
}

// Signalfunktion für einen sauberen Shutdown des Systems
void graceful_shutdown(int signum){
    printf("Graceful shutdown\n");
    shutdownFlag = 1;
    // Alle Pizzaiolos wecken, damit sie sich beenden können
    pthread_mutex_lock(&pizzaiolo_mutex);
    pthread_cond_broadcast(&pizzaiolo_cond);
    pthread_mutex_unlock(&pizzaiolo_mutex);
    // Lieferanten wecken, damit auch dieser sich beenden kann
    pthread_mutex_lock(&supplier_mutex);
    pthread_cond_broadcast(&supplier_cond);
    pthread_mutex_unlock(&supplier_mutex);
}

// Threadfunktion, die das Programm nach einer festgelegten Zeit automatisch beendet
void* auto_shutdown(void* arg){
    int timer = SIMULATION_DURATION;
    while (timer > 0){
        sleep(1); // Wartet die vorgegebene Zeit
        timer--;
        if (shutdownFlag)
            return NULL;
    }
    graceful_shutdown(0);
    return NULL;
}


int main(void){
    // Zufallsgenerator initialisieren
    srand(time(NULL));

    // Initialisierung der Mutexes
    if (pthread_mutex_init(&counter.mutex, NULL) != 0) {
        perror("Fehler: 'counter' Mutex konnte nicht initialisiert werden\n");
        exit(1);
    }
    if (pthread_mutex_init(&supplier_mutex, NULL) != 0) {
        fprintf(stderr, "Fehler: 'supplier' Mutex konnte nicht initialisiert werden\n");
        exit(1);
    }
    if (pthread_mutex_init(&pizzaiolo_mutex, NULL) != 0) {
        perror("Fehler: 'pizzaiolo' Mutex konnte nicht initialisiert werden\n");
        exit(1);
    }

    // Initialisierung der Condition Variables
    if (pthread_cond_init(&supplier_cond, NULL) != 0) {
        perror("Fehler: 'supplier' Condition konnte nicht initialisiert werden\n");
        exit(1);
    }
    if (pthread_cond_init(&pizzaiolo_cond, NULL) != 0) {
        perror("Fehler: 'pizzaiolo' Condition konnte nicht initialisiert werden\n");
        exit(1);
    }

    // Registriere Signal Handler für den Alarm (qualityCheck)
    struct sigaction signal;
    signal.sa_handler = qualityCheck;
    sigemptyset(&signal.sa_mask);

    if (sigaction(SIGALRM, &signal, NULL) != 0){
        perror("Fehler beim registrieren des signal handler\n");
        exit(1);
    }

    // Registriere Signal Handler für den Graceful Shutdown
    struct sigaction shutdownSignal;
    shutdownSignal.sa_handler = graceful_shutdown;
    sigemptyset(&shutdownSignal.sa_mask);
    sigaddset(&shutdownSignal.sa_mask, SIGTERM);
    sigaddset(&shutdownSignal.sa_mask, SIGINT);
    sigaddset(&shutdownSignal.sa_mask, SIGQUIT);
    shutdownSignal.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &shutdownSignal, NULL) != 0){
        perror("Fehler beim registrieren des signal handler\n");
        exit(1);
    }
    if (sigaction(SIGINT, &shutdownSignal, NULL) != 0){
        perror("Fehler beim registrieren des signal handler\n");
        exit(1);
    }
    if (sigaction(SIGQUIT, &shutdownSignal, NULL) != 0){
        perror("Fehler beim registrieren des signal handler\n");
        exit(1);
    }

    // Pizzaiolo-Threads erstellen
    pthread_t pizzaioloThreads[PIZZAIOLO_COUNT];
    for (int i = 0; i < PIZZAIOLO_COUNT; i++){
        if (pthread_create(&pizzaioloThreads[i], NULL, bakePizza, NULL) != 0) {
            perror("Fehler: Pizzaiolo Thread konnte nicht erstellt werden\n");
            exit(1);
        }
    }

    // Lieferanten-Thread erstellen
    pthread_t supplierThread;
    if (pthread_create(&supplierThread, NULL, deliverPizza, NULL) != 0) {
        perror("Fehler: Lieferant Thread konnte nicht erstellt werden\n");
        exit(1);
    }

    // Shutdown-Thread erstellen
    pthread_t shutdownThread;
    if (pthread_create(&shutdownThread, NULL, auto_shutdown, NULL) != 0) {
        perror("Fehler: Shutdown Thread konnte nicht erstellt werden\n");
        exit(1);
    }

    alarm(5); // Startet die regelmäßige Qualitätskontrolle alle 5 Sekunden

    // Warten bis alle Pizzaiolo-Threads beendet sind
    for (int i = 0; i < PIZZAIOLO_COUNT; i++){
        pthread_join(pizzaioloThreads[i], NULL);
    }

    // Warten bis der Lieferanten-Thread beendet ist
    pthread_join(supplierThread, NULL);

    // Warten bis der Shutdown-Thread beendet ist
    pthread_join(shutdownThread, NULL);

    // Ressourcen freigeben
    pthread_mutex_destroy(&counter.mutex);
    pthread_mutex_destroy(&supplier_mutex);
    pthread_mutex_destroy(&pizzaiolo_mutex);
    pthread_cond_destroy(&supplier_cond);
    pthread_cond_destroy(&pizzaiolo_cond);

    counter.countMargherita = 0;
    counter.countMarinara = 0;

    return 0;
}