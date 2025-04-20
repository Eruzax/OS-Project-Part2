#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <stdbool.h>

typedef enum {ARRIVE, READY, RUNNING, PREEMPTION, ENQUEUE, WAITING, TERMINATED} State;

typedef struct {
    char* pid;
    int arrivalTime;
    int endTime;
    int numBursts;
    int burstsLeft;
    int* cpuBursts;
    int* ioBursts;
    int* remainingBursts;
    State state;
    int tau;
    // For writing to simout
    int readyTime;
    int wait;
    int startTime;
    int turnaround;
    int cs;
    // For RR
    int preemptions;
    int oneTS;
} Process;

// Process: Process associated with the event
// Time: timestamp when a process finishes its state
// State: state of the process
typedef struct {
    Process* process;
    int time;
    State state;
} Event;

// Create a new event
Event* createEvent(Process* p, int time, State s){
    Event* newEvent = malloc(sizeof(Event*));
    newEvent->process = p;
    newEvent->time = time;
    newEvent->state = s;
    return newEvent;
}

typedef struct {
    Event** events;
    int size;
    int capacity;
} EventQueue;

// Initialize the event queue
void initEventQueue(EventQueue* q, int capacity) {
    q->events = calloc(capacity, sizeof(Event));
    q->size = 0;
    q->capacity = capacity;
}

// Helper function to assign a priority based on the event state.
int getEventPriority(int state) {
    if (state == RUNNING || state == ENQUEUE || state == PREEMPTION) { // CPU burst completion
        return 0;
    } else if (state == READY) {                // Process starts using the CPU
        return 1;
    } else if (state == WAITING) {              // I/O burst completions
        return 2;
    } else if (state == ARRIVE) {               // New process arrivals
        return 3;
    } else {                                    // Other events
        return 4;
    }
}
// Insert an event into the event queue for FCFS
void insertEventFCFS(EventQueue* q, Event* event) {
    if (q->size >= q->capacity) {
        fprintf(stderr, "ERROR: EventQueue is full. Cannot insert event.\n");
        return;
    }
    
    int insertIndex = q->size;  // Default to inserting at the end

    // Find the correct position to insert while keeping order
    for (int i = 0; i < q->size; i++) {
        if (q->events[i]->time > event->time) {
            insertIndex = i;
            break;
        } 
        else if (event->time == q->events[i]->time) {
            int eventPriority = getEventPriority(event->state);
            int curPriority = getEventPriority(q->events[i]->state);
            if (eventPriority < curPriority) {
                // Event has higher priority (lower numerical value).
                insertIndex = i;
                break;
            } else if (eventPriority < curPriority){
                if (strcmp(event->process->pid, q->events[i]->process->pid) < 0) {
                    insertIndex = i;
                    break;
                }
            }
        }
    }

    // Shift all events **after insertIndex** one position to the right
    for (int j = q->size; j > insertIndex; j--) {
        q->events[j] = q->events[j - 1];
    }

    // Insert the new event at the correct position
    q->events[insertIndex] = event;
    q->size++;
}


// Remove the element at the top of event queue
Event* popEvent(EventQueue* q) {
    if (q->size == 0) return NULL;
    Event* event = q->events[0];
    q->size--;
    for (int i = 0; i < q->size; i++) {
        q->events[i] = q->events[i + 1];
    }
    return event;
}


// Free all events in the EventQueue and then the array itself.
void freeEventQueue(EventQueue* q) {
    if (q == NULL) return;
    // Free each individual event
    for (int i = 0; i < q->size; i++) {
        if (q->events[i]){
            free(q->events[i]);
        }
    }
    // Free the events array
    free(q->events);
    q->events = NULL;
    q->size = 0;
    q->capacity = 0;
}

// For debugging
const char* stateToString(State s) {
    switch (s) {
        case ARRIVE:      return "ARRIVE";
        case READY:       return "READY";
        case RUNNING:     return "RUNNING";
        case PREEMPTION:  return "PREEMPTION";
        case WAITING:     return "WAITING";
        case TERMINATED:  return "TERMINATED";
        default:          return "UNKNOWN";
    }
}

void printEventQueue(EventQueue* q) {
    if (q->size == 0) {
        printf("[Q empty]\n");
        return;
    }
    
    printf("Event Queue: ");
    for (int i = 0; i < q->size; i++) {
        Event* e = q->events[i];
        printf("[Time: %d, Process: %s, State: %s]", e->time, e->process->pid, stateToString(e->state));
    }
    printf("\n");
}


// Queue DS
typedef struct {
    Process **procs;
    int size;
    int capacity;
} Queue;

// Initialize queue
void initQueue(Queue *q, int capacity) {
    q->procs = calloc(capacity + 1, sizeof(Process *));
    if (q->procs == NULL) {
        fprintf(stderr, "ERROR: Memory allocation failed for queue\n");
        return;
    }
    q->size = 0;
    q->capacity = capacity;
}

// Add to queue
void enqueue(Queue *q, Process *p, int time) {
    if (q->size >= q->capacity) {
        int cap = q->capacity + 2;
        q->procs = realloc(q->procs, cap * sizeof(Process*));
        if (!q->procs){
            fprintf(stderr, "ERROR: Queue is full, cannot enqueue %s at time %dms\n", p->pid, time);
            return;
        }
        q->capacity++;
    }

    *(q->procs + q->size) = p;
    q->size++;
}

Process* dequeue(Queue *q) {
    if (q->size == 0) return NULL;
    Process *p = *(q->procs);
    // Shift elements left
    Process **cur = q->procs;
    for (int i = 0; i < q->size - 1; i++) {
        *(cur + i) = *(cur + i + 1);
    }
    q->size--;
    return p;
}

void printQueue(Queue *q) {
    if (q->size == 0) {
        printf(" empty");
    } else {
        Process **cur = q->procs;
        for (int i = 0; i < q->size; i++) {
            printf(" %s", (*(cur + i))->pid);
        }
    }
}


// Get the time that the cpu is free based on 
int getTimeOfLastEvent(EventQueue* eq, int time, int tslice){
    if (eq->size == 0){
        return time;
    }

    for (int i = eq->size-1; i >= 0; i--){
        if (eq->events[i]->state != WAITING && eq->events[i]->state != ARRIVE){
            int burstRem = eq->events[i]->process->remainingBursts[eq->events[i]->process->numBursts - eq->events[i]->process->burstsLeft];
            if (burstRem <= tslice){
                return eq->events[i]->time + burstRem;
            } else {
                return eq->events[i]->time + tslice;
            }
        }
    }
    return time;
}

// First Come First Serve
int FCFS(Process** processes, int n, int tcs) {
    // Reset all processes
    for (int i = 0; i < n; i++) {
        (*(processes+i))->state = ARRIVE;
        (*(processes+i))->burstsLeft = (*(processes+i))->numBursts;
        for (int j = 0; j < (*(processes+i))->numBursts; j++){
            (*(processes+i))->remainingBursts[j] = (*(processes+i))->cpuBursts[j];
        }
        // For writing to simout
        (*(processes+i))->readyTime = 0;
        (*(processes+i))->wait = 0;
        (*(processes+i))->startTime = 0;
        (*(processes+i))->turnaround = 0;
        (*(processes+i))->cs = 0;
    }


    // Start Simultaion
    Queue q;
    initQueue(&q, n);
    EventQueue eq;
    initEventQueue(&eq, n);
    printf("time 0ms: Simulator started for FCFS [Q empty]\n");

    // Arrivals
    for (int i = 0; i < n; i++){
        Event* newEvent = createEvent(*(processes+i), (*(processes+i))->arrivalTime, ARRIVE);
        insertEventFCFS(&eq, newEvent);
    }

    int time = 0;
    int terminatedCount = 0;
    int cpuFreeAt = 0;
    int cpuIdle = -1;
    while (terminatedCount < n) {
        // Handle Events
        Event* e = popEvent(&eq);
        time = e->time;

        // Arrival
        if (e->state == ARRIVE){
            // Print and add to queue
            enqueue(&q, e->process, time);
            // Print
            if (time <= 10000){
                printf("time %dms: Process %s arrived; added to ready queue [Q", time, e->process->pid);
                printQueue(&q);
                printf("]\n");
            }

            // Create a CPU burst event
            // CPU is free
            if (cpuIdle == -1 && time >= cpuFreeAt){
                Event* newEvent = createEvent(e->process, time + tcs/2, READY);
                insertEventFCFS(&eq, newEvent);
                cpuFreeAt = time + e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft] + tcs/2;
                dequeue(&q);
            }
            // CPU is not free
            else{
                Event* newEvent = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventFCFS(&eq, newEvent);
                cpuFreeAt += e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft] + tcs;
            }

            // For writing to simout
            e->process->startTime = time + tcs/2;   // Turnaround time
            e->process->readyTime = time;           // Wait time
        }
        // Start CPU Burst
        else if (e->state == READY){
            e->process->cs++;                                           // Context Switch
            e->process->wait += time - e->process->readyTime - tcs/2;   // Wait time
            cpuIdle = 0;

            if (e->process->pid == q.procs[0]->pid){
                dequeue(&q);
            }
            int burstTime = *(e->process->cpuBursts + (e->process->numBursts - e->process->burstsLeft));
            // Print
            if (time <= 10000){
                printf("time %dms: Process %s started using the CPU for %dms burst [Q", time, e->process->pid, burstTime);
                printQueue(&q);
                printf("]\n");
            }

            e->process->burstsLeft--;
            if (e->process->burstsLeft == 0){
                Event* endCpu = createEvent(e->process, time + burstTime, TERMINATED);
                insertEventFCFS(&eq, endCpu);
            } else{
                Event* endCpu = createEvent(e->process, time + burstTime, RUNNING);
                insertEventFCFS(&eq, endCpu);
            }
            // updated the time when the CPU is free
            if (cpuFreeAt < time + burstTime){
                cpuFreeAt = time + burstTime;
            }
        }
        // CPU Burst complete
        else if(e->state == RUNNING){
            e->process->turnaround += time + (tcs/2) - e->process->startTime;   // Turnaround time

            // CPU Burst complete
            cpuIdle = -1;

            // Print
            if (time <= 10000){
                if (e->process->burstsLeft == 1){
                    printf("time %dms: Process %s completed a CPU burst; %d burst to go [Q", time, e->process->pid, e->process->burstsLeft);
                } else{
                    printf("time %dms: Process %s completed a CPU burst; %d bursts to go [Q", time, e->process->pid, e->process->burstsLeft);
                }
                printQueue(&q);
                printf("]\n");
            }

            // IO Burst start
            int ioCompTime = time + *(e->process->ioBursts+(e->process->numBursts - e->process->burstsLeft - 1)) + tcs/2;
            // Print
            if (time <= 10000){
                printf("time %dms: Process %s switching out of CPU; blocking on I/O until time %dms [Q", time, e->process->pid, ioCompTime);
                printQueue(&q);
                printf("]\n");
            }
            Event* ioBurst = createEvent(e->process, ioCompTime, WAITING);
            insertEventFCFS(&eq, ioBurst);
        }
        // IO End
        else if (e->state == WAITING){
            if (cpuIdle == -1 && eq.events[0]->time - tcs/2 <= time){
                dequeue(&q);
            }
            enqueue(&q, e->process, time);

            // Print
            if (time <= 10000){
                printf("time %dms: Process %s completed I/O; added to ready queue [Q", time, e->process->pid);
                printQueue(&q);
                printf("]\n");
            }

            // Get the burst time of the last process ready to run
            if (q.size == 1 && cpuIdle == -1){
                cpuFreeAt = time;
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs/2, READY);
                insertEventFCFS(&eq, cpuBurst);
            } else if (q.size > 1){
                int lastProcBurst = time;
                for (int i = eq.size; i > 0; i--){
                    if (eq.events[i]->state == READY){
                        lastProcBurst = eq.events[i]->time + eq.events[i]->process->cpuBursts[eq.events[i]->process->numBursts - eq.events[i]->process->burstsLeft];
                        break;
                    }
                }
                
                // Creates an event while considering CPU Bursts times in the queue
                Event* cpuBurst = createEvent(e->process, lastProcBurst + tcs, READY);
                insertEventFCFS(&eq, cpuBurst);
                int burstTime = e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft];
                cpuFreeAt = lastProcBurst + burstTime + tcs;
            } else {
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventFCFS(&eq, cpuBurst);
            }

            // For writing to simout
            e->process->startTime = time;   // Turnaround time
            e->process->readyTime = time;   // Wait time
        }
        // Termination
        else if (e->state == TERMINATED){
            e->process->turnaround += time + tcs - e->process->startTime; // Turnaround time
            cpuIdle = -1;
            printf("time %dms: Process %s terminated [Q", time, e->process->pid);
            printQueue(&q);
            printf("]\n");
            terminatedCount++;
        }
        free(e);
    }
    time += tcs/2;
    printf("time %dms: Simulator ended for FCFS [Q empty]\n\n", time);
    freeEventQueue(&eq);
    free(q.procs);
    return time;
}


//----------------------------------------------------------------------------------------------------------------------------

// Inserts a process into the ready queue in sorted order (by tau, then PID) returns true if process is inserted in the middle of the queue returns false if the process is insert at the end of queue 
bool enqueueSJF(Queue* q, Process* p) {
    if (q->size >= q->capacity) {
        fprintf(stderr, "ERROR: Queue is full, cannot enqueue %s\n", p->pid);
        return false;
    }
    int insertIndex = q->size;
    for (int i = 0; i < q->size; i++) {
        if (p->tau < q->procs[i]->tau ||
           (p->tau == q->procs[i]->tau && strcmp(p->pid, q->procs[i]->pid) < 0)) {
            insertIndex = i;
            break;
        }
    }
    // Shift elements rightward to make space for the new process
    for (int j = q->size; j > insertIndex; j--) {
        q->procs[j] = q->procs[j-1];
    }
    q->procs[insertIndex] = p;
    q->size++;
    return (insertIndex != q->size - 1);
}

// Makes sure that all processes in the event queue are in correct order by their elapsed times
void updateProcessTiming(EventQueue *eq) {
    int size = eq->size;
    int newSize;
    int pass = 0;
    bool swapped = true; 
    int i;
    
    while (swapped) {
        swapped = false;
        newSize = size - pass;
        i = 0;
        while (i < newSize - 1) {
            if (eq->events[i]->time > eq->events[i+1]->time) {
                Event *temp = eq->events[i];
                eq->events[i] = eq->events[i+1];
                eq->events[i+1] = temp;
                swapped = true;
            }
            i++;
        }
        pass++;
    }
}

void updateProcessOrder(EventQueue* eq, Event* event, int insertIndex, int tcs) {
	event->time = eq->events[insertIndex]->time; 
	int prevProcTime = event->time; // a4
	int burstTime = event->process->cpuBursts[event->process->numBursts - event->process->burstsLeft]; 
	
	for (int i = insertIndex; i < eq->size; i++) {
        if (eq->events[i]->state == READY) {
			eq->events[i]->time = prevProcTime + burstTime + tcs;
        	prevProcTime = eq->events[i]->time; 
        	burstTime = eq->events[i]->process->cpuBursts[eq->events[i]->process->numBursts - eq->events[i]->process->burstsLeft];
        }       
		
    }    
    
}

// For events with state READY, if they have the same event time,
// we compare their process's tau values (and use PID as a tie-breaker).
bool insertEventSJF(EventQueue* eq, Event* event, int tcs) {
    if (eq->size >= eq->capacity) {
        fprintf(stderr, "ERROR: EventQueue is full. Cannot insert event.\n");
        return false;
    }
    bool insertMid = false;
    int insertIndex = eq->size; // default: insert at end
    for (int i = 0; i < eq->size; i++) {
        if ((event->process->tau < eq->events[i]->process->tau ||
           (event->process->tau == eq->events[i]->process->tau &&
            strcmp(event->process->pid, eq->events[i]->process->pid) < 0)) && (event->state == READY && eq->events[i]->state == READY)) 
		{
			insertMid = true;
        	insertIndex = i;
        	
        	break;
	   	} 
		else if (eq->events[i]->time > event->time) {
		    insertIndex = i;
		    break;
    	}	
	}
	
	if (insertMid) {
    	updateProcessOrder(eq, event, insertIndex, tcs);
    	updateProcessTiming(eq);
    }
    
    // Shift existing events to make room
    for (int j = eq->size; j > insertIndex; j--) {
        eq->events[j] = eq->events[j - 1];
    }
    eq->events[insertIndex] = event;
    eq->size++;
    return insertMid;
}


// Shortest Job First
int SJF(Process** processes, int n, int tcs, double alpha, double lambda) {
    // Reset all processes
    for (int i = 0; i < n; i++) {
        (*(processes+i))->state = ARRIVE;
        (*(processes+i))->burstsLeft = (*(processes+i))->numBursts;
        (*(processes+i))->tau = (int)ceil(1.0 / lambda);
        for (int j = 0; j < (*(processes+i))->numBursts; j++){
            (*(processes+i))->remainingBursts[j] = (*(processes+i))->cpuBursts[j];
        }
        // For writing to simout
        (*(processes+i))->readyTime = 0;
        (*(processes+i))->wait = 0;
        (*(processes+i))->startTime = 0;
        (*(processes+i))->turnaround = 0;
        (*(processes+i))->cs = 0;
    }

    Queue q;
    initQueue(&q, n);
    EventQueue eq;
    initEventQueue(&eq, n);
    printf("time 0ms: Simulator started for SJF [Q empty]\n");

    // Schedule initial arrivals
    for (int i = 0; i < n; i++){
        Event* newEvent = createEvent(processes[i], processes[i]->arrivalTime, ARRIVE);
        insertEventSJF(&eq, newEvent, tcs);
    }
    int time = 0;
    int terminatedCount = 0;
    int cpuFreeAt = 0;
    int cpuIdle = -1;
    
    while (terminatedCount < n) {
    	// Handle Events
        Event* e = popEvent(&eq);
        time = e->time;
		
        if (e->state == ARRIVE) {
            // Add process to the ready queue using SJF ordering
            enqueueSJF(&q, e->process);
            
            // Print
            if (time <= 10000){
                printf("time %dms: Process %s (tau %dms) arrived; added to ready queue [Q", time, e->process->pid, e->process->tau);
                printQueue(&q);
                printf("]\n");
            }
            
            if (cpuIdle == -1 && time >= cpuFreeAt) {
                Event* newEvent = createEvent(e->process, time + tcs/2, READY);
                insertEventSJF(&eq, newEvent, tcs);
                cpuFreeAt = time + e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft] + tcs/2;
                dequeue(&q);           
            } else {
                Event* newEvent = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventSJF(&eq, newEvent, tcs);
                cpuFreeAt += e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft] + tcs;                
            }
            
            // For writing to simout
            e->process->startTime = time + tcs/2;   // Turnaround time
            e->process->readyTime = time;           // Wait time
        }
        else if (e->state == READY) {
        	e->process->cs++;                                           // Context Switch
            e->process->wait += time - e->process->readyTime - tcs/2;   // Wait time
        
            cpuIdle = 0;
            if (q.size > 0 && e->process->pid == q.procs[0]->pid) {
                dequeue(&q);
            }
            int burstTime = *(e->process->cpuBursts + (e->process->numBursts - e->process->burstsLeft));
            
            // Print
            if (time <= 10000){
                printf("time %dms: Process %s (tau %dms) started using the CPU for %dms burst [Q", 
                    time, e->process->pid, e->process->tau, burstTime);
                printQueue(&q);
                printf("]\n");
            }

            e->process->burstsLeft--;
            if (e->process->burstsLeft == 0) {
                Event* endCpu = createEvent(e->process, time + burstTime, TERMINATED);
                insertEventSJF(&eq, endCpu, tcs);
            } else {
                Event* endCpu = createEvent(e->process, time + burstTime, RUNNING);
                insertEventSJF(&eq, endCpu, tcs);
            }
            if (cpuFreeAt < time + burstTime) {
                cpuFreeAt = time + burstTime;
            }
        }
        else if (e->state == RUNNING) {
        	// Turnaround time
        	e->process->turnaround += time + (tcs/2) - e->process->startTime;   
        
        	// CPU Burst complete
            cpuIdle = -1;
            
            // Print
            if (time <= 10000){
                if (e->process->burstsLeft == 1){
                    printf("time %dms: Process %s (tau %dms) completed a CPU burst; %d burst to go [Q", 
                        time, e->process->pid, e->process->tau, e->process->burstsLeft);
                } else {
                    printf("time %dms: Process %s (tau %dms) completed a CPU burst; %d bursts to go [Q", 
                        time, e->process->pid, e->process->tau, e->process->burstsLeft);
                }
                printQueue(&q);
                printf("]\n");
            }

            // Recalculate tau after the CPU burst
            int oldTau = e->process->tau;
            int completedBurst = e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft - 1];
            int newTau = (int)ceil(alpha * completedBurst + (1 - alpha) * oldTau);
            
            // Print
            if (time <= 10000){
                printf("time %dms: Recalculated tau for process %s: old tau %dms ==> new tau %dms [Q", 
                    time, e->process->pid, oldTau, newTau);
                printQueue(&q);
                printf("]\n");
            }
			e->process->tau = newTau;
			// IO Burst start
            int ioCompTime = time + e->process->ioBursts[e->process->numBursts - e->process->burstsLeft - 1] + tcs/2;
            
            // Print
            if (time <= 10000){
                printf("time %dms: Process %s switching out of CPU; blocking on I/O until time %dms [Q", 
                    time, e->process->pid, ioCompTime);
                printQueue(&q);
                printf("]\n");
            }

            Event* ioBurst = createEvent(e->process, ioCompTime, WAITING);
            insertEventSJF(&eq, ioBurst, tcs);
        }
        else if (e->state == WAITING) {
            if (cpuIdle == -1 && e->process->pid == q.procs[0]->pid) {
                dequeue(&q);
            }
            enqueueSJF(&q, e->process);
			
			// Print
            if (time <= 10000){
                printf("time %dms: Process %s (tau %dms) completed I/O; added to ready queue [Q", 
                    time, e->process->pid, e->process->tau);
                printQueue(&q);
                printf("]\n");
            }
			
            if (q.size == 1 && cpuIdle == -1) {
                cpuFreeAt = time;
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs/2, READY);
                insertEventSJF(&eq, cpuBurst, tcs);
            } else if (q.size > 1) {
                int lastProcBurst = time;
                int i;
                for (i = eq.size - 1; i >= 0; i--) {
                    if (eq.events[i]->state == READY) {
                        lastProcBurst = eq.events[i]->time + eq.events[i]->process->cpuBursts[eq.events[i]->process->numBursts - eq.events[i]->process->burstsLeft];
                        break;
                    } 
                }
                Event* cpuBurst = createEvent(e->process, lastProcBurst + tcs, READY);
                int burstTime = e->process->cpuBursts[e->process->numBursts - e->process->burstsLeft];
                cpuFreeAt = lastProcBurst + burstTime + tcs;
                insertEventSJF(&eq, cpuBurst, tcs);             
            } else {
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventSJF(&eq, cpuBurst, tcs);
            }
            
            // For writing to simout
            e->process->startTime = time;   // Turnaround time
            e->process->readyTime = time;   // Wait time
        }
        else if (e->state == TERMINATED) {
        	e->process->turnaround += time + tcs - e->process->startTime; // Turnaround time
            cpuIdle = -1;
            printf("time %dms: Process %s terminated [Q", time, e->process->pid);
            printQueue(&q);
            printf("]\n");
            terminatedCount++;
        }
        free(e);
    }
    time += tcs/2;
    printf("time %dms: Simulator ended for SJF [Q empty]\n\n", time);
    free(q.procs);
    return time;
}

//----------------------------------------------------------------------------------------------------------------------------

int SRT(Process** processes, int n, int tcs, double alpha, double lambda){
    // Reset all processes
    for (int i = 0; i < n; i++) {
        (*(processes+i))->state = ARRIVE;
        (*(processes+i))->burstsLeft = (*(processes+i))->numBursts;
        (*(processes+i))->tau = (int)ceil(1.0 / lambda);
        for (int j = 0; j < (*(processes+i))->numBursts; j++){
            (*(processes+i))->remainingBursts[j] = (*(processes+i))->cpuBursts[j];
        }
        // For writing to simout
        (*(processes+i))->readyTime = 0;
        (*(processes+i))->wait = 0;
        (*(processes+i))->startTime = 0;
        (*(processes+i))->turnaround = 0;
        (*(processes+i))->cs = 0;
    }

    Queue q;
    initQueue(&q, n);
    EventQueue eq;
    initEventQueue(&eq, n);
    printf("time 0ms: Simulator started for SRT [Q empty]\n");

    // Schedule initial arrivals
    for (int i = 0; i < n; i++){
        Event* newEvent = createEvent(processes[i], processes[i]->arrivalTime, ARRIVE);
        insertEventSJF(&eq, newEvent, tcs);
    }
	
    int time = 0;
    // int terminatedCount = 0;
    // int cpuFreeAt = 0;
    // int cpuIdle = -1;

    time += tcs/2;
    printf("time %dms: Simulator ended for SRT [Q empty]\n\n", time);
    freeEventQueue(&eq);
    free(q.procs);
    return time;
}

// Round Robin
int RR(Process** processes, int n, int tcs, int tslice){
    // Reset all processes
    for (int i = 0; i < n; i++) {
        (*(processes+i))->state = ARRIVE;
        (*(processes+i))->burstsLeft = (*(processes+i))->numBursts;
        for (int j = 0; j < (*(processes+i))->numBursts; j++){
            (*(processes+i))->remainingBursts[j] = (*(processes+i))->cpuBursts[j];
        }
        // For writing to simout
        (*(processes+i))->readyTime = 0;
        (*(processes+i))->wait = 0;
        (*(processes+i))->startTime = 0;
        (*(processes+i))->turnaround = 0;
        (*(processes+i))->cs = 0;
        (*(processes+i))->preemptions = 0;
        (*(processes+i))->oneTS = 0;
    }


    // Start Simultaion
    Queue q;
    initQueue(&q, n);
    EventQueue eq;
    initEventQueue(&eq, 4*n);
    printf("time 0ms: Simulator started for RR [Q empty]\n");

    // Arrivals
    for (int i = 0; i < n; i++){
        Event* newEvent = createEvent(*(processes+i), (*(processes+i))->arrivalTime, ARRIVE);
        insertEventFCFS(&eq, newEvent);
    }

    // Start Simulation
    int time = 0;
    int terminatedCount = 0;
    int cpuFreeAt = 0;
    int cpuIdle = -1;
    while (terminatedCount < n) {
        // Handle Events
        Event* e = popEvent(&eq);
        time = e->time;

        // Arrival
        if (e->state == ARRIVE){
            // Print and add to queue
            enqueue(&q, e->process, time);

            // Print
            if (time <= 10000){
                printf("time %dms: Process %s arrived; added to ready queue [Q", time, e->process->pid);
                printQueue(&q);
                printf("]\n");
            }
            
            // CPU is free
            int burstTime = e->process->remainingBursts[e->process->numBursts - e->process->burstsLeft];
            if (cpuIdle == -1 && time >= cpuFreeAt){
                Event* newEvent = createEvent(e->process, time + tcs/2, READY);
                insertEventFCFS(&eq, newEvent);
                if (tslice >= burstTime){
                    cpuFreeAt = time + burstTime + tcs/2;
                } 
                // Burst finishes after time slice
                else{
                    cpuFreeAt = time + tslice + tcs/2;
                }
                dequeue(&q);
            }
            // CPU is not free
            else {
                Event* newEvent = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventFCFS(&eq, newEvent);
                if (tslice >= burstTime){
                    cpuFreeAt += burstTime + tcs;
                } 
                // Burst finishes before timeslice
                else {
                    cpuFreeAt += tslice + tcs;
                }
            }

            // For writing to simout
            e->process->readyTime = time;           // Wait time
        }
        // Start CPU Burst
        else if (e->state == READY){
            e->process->cs++;                                           // Context Switch
            e->process->wait += time - e->process->readyTime - tcs/2;   // Wait time
            
            cpuIdle = 0;
            if (e->process->pid == q.procs[0]->pid){
                dequeue(&q);
            }

            int burstTime = *(e->process->remainingBursts + (e->process->numBursts - e->process->burstsLeft));
            int fullBurst = *(e->process->cpuBursts + (e->process->numBursts - e->process->burstsLeft));

            // Print
            if (time <= 10000){
                if (burstTime != fullBurst){
                    printf("time %dms: Process %s started using the CPU for remaining %dms of %dms burst [Q", time, e->process->pid, burstTime, fullBurst);
                    printQueue(&q);
                    printf("]\n");
                } else {
                    printf("time %dms: Process %s started using the CPU for %dms burst [Q", time, e->process->pid, fullBurst);
                    printQueue(&q);
                    printf("]\n");
                }
            }

            // Update bursts
            int *burstRem = e->process->remainingBursts + (e->process->numBursts - e->process->burstsLeft);
            *burstRem -= tslice;

            // Burst finishes its remaining time
            if (*burstRem <= 0 || e->process->burstsLeft == 0){
                if (e->process->burstsLeft == 0){
                    Event* endCpu = createEvent(e->process, time + burstTime, TERMINATED);
                    insertEventFCFS(&eq, endCpu);
                } else{
                    Event* endCpu = createEvent(e->process, time + burstTime, RUNNING);
                    insertEventFCFS(&eq, endCpu);
                }
            } 
            // Burst is not fully complete
            else {
                Event* endCpu = createEvent(e->process, time + tslice, PREEMPTION);
                insertEventFCFS(&eq, endCpu);
            }

            // For writing to simout
            if (fullBurst <= tslice){
                e->process->oneTS++;
            }
        } 
        // Preemption
        else if (e->state == PREEMPTION) {
            // No preemption
            int *burstRem = e->process->remainingBursts + (e->process->numBursts - e->process->burstsLeft);
            if (q.size == 0){
                // Print
                if (time <= 10000){
                    printf("time %dms: Time slice expired; no preemption because ready queue is empty [Q", time);
                    printQueue(&q);
                    printf("]\n"); 
                }

                // Last time slice before finishing
                if (*burstRem <= tslice){
                    Event* continueBurst = createEvent(e->process, time + *burstRem, RUNNING);
                    insertEventFCFS(&eq, continueBurst);
                    cpuFreeAt += *burstRem;
                } 
                // Still more cpu bursts
                else {
                    *burstRem -= tslice;
                    Event* continueBurst = createEvent(e->process, time + tslice, PREEMPTION);
                    insertEventFCFS(&eq, continueBurst);
                    cpuFreeAt += tslice;
                }
            } else {
                // Print
                if (time <= 10000){
                    printf("time %dms: Time slice expired; preempting process %s with %dms remaining [Q", time, e->process->pid, *burstRem);
                    printQueue(&q);
                    printf("]\n");
                }
                Event* enqueue = createEvent(e->process, time + tcs/2, ENQUEUE);
                insertEventFCFS(&eq, enqueue);

                // For writing to simout
                e->process->preemptions++;              // Preemptions
            }
        } 
        // Add to queue after process is preempted
        else if (e->state == ENQUEUE){
            // For writing to simout
            e->process->readyTime = time;   // Wait time

            enqueue(&q, e->process, time);
            // Creates an event while considering CPU Bursts times in the queue
            Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs, READY);
            insertEventFCFS(&eq, cpuBurst);
            // Get the burst time of the last process ready to run
            int lastProcBurst = getTimeOfLastEvent(&eq, time, tslice);
            cpuFreeAt = lastProcBurst;
        }
        // CPU Burst complete
        else if(e->state == RUNNING){
            // CPU Burst complete
            cpuIdle = -1;
            e->process->burstsLeft--;
            if (e->process->burstsLeft == 0){
                Event* termination = createEvent(e->process, time, TERMINATED);
                insertEventFCFS(&eq, termination);
            } else {
                // Print
                if (time <= 10000){
                    if (e->process->burstsLeft == 1){
                        printf("time %dms: Process %s completed a CPU burst; %d burst to go [Q", time, e->process->pid, e->process->burstsLeft);
                    } else{
                        printf("time %dms: Process %s completed a CPU burst; %d bursts to go [Q", time, e->process->pid, e->process->burstsLeft);
                    }
                    printQueue(&q);
                    printf("]\n");
                }
    
                // IO Burst start
                int ioCompTime = time + *(e->process->ioBursts+(e->process->numBursts - e->process->burstsLeft - 1)) + tcs/2;

                // Print
                if (time <= 10000){
                    printf("time %dms: Process %s switching out of CPU; blocking on I/O until time %dms [Q", time, e->process->pid, ioCompTime);
                    printQueue(&q);
                    printf("]\n");
                }
                Event* ioBurst = createEvent(e->process, ioCompTime, WAITING);
                insertEventFCFS(&eq, ioBurst);
            }
        } 
        // IO End
        else if (e->state == WAITING){
            // Dequeue next process if it runs before the next event 
            if ((eq.events[0]->time - time) < (tcs/2) && eq.events[0]->state == READY){
                dequeue(&q);
            }

            // I/O Burst complete
            enqueue(&q, e->process, time);

            // Print
            if (time <= 10000){
                printf("time %dms: Process %s completed I/O; added to ready queue [Q", time, e->process->pid);
                printQueue(&q);
                printf("]\n");
            }

            // Same process is at the head of the queue
            if (q.size == 1 && cpuIdle == -1){
                cpuFreeAt = time;
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs/2, READY);
                insertEventFCFS(&eq, cpuBurst);
                cpuIdle = 0;
                dequeue(&q);
                int lastProcBurst = getTimeOfLastEvent(&eq, time, tslice);
                cpuFreeAt = lastProcBurst;
            } 
            // Different process from the current process running
            else {
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventFCFS(&eq, cpuBurst);
                int lastProcBurst = getTimeOfLastEvent(&eq, time, tslice);
                cpuFreeAt = lastProcBurst;
            }

            // For writing to simout
            e->process->readyTime = time;          // Wait time
        }
        // Termination
        else if (e->state == TERMINATED){
            e->process->turnaround = time + (tcs/2) - e->process->arrivalTime; // Turnaround time
            cpuIdle = -1;
            e->process->burstsLeft--;
            printf("time %dms: Process %s terminated [Q", time, e->process->pid);
            printQueue(&q);
            printf("]\n");
            terminatedCount++;
        }
        free(e);
    }

    time += tcs/2;
    printf("time %dms: Simulator ended for RR [Q empty]\n", time);
    freeEventQueue(&eq);
    free(q.procs);
    return time;
}

// NEW ROUND ROBIN
// Add to queue
void alt_enqueue(Queue *q, Process *p) {
    if (q->size >= q->capacity) {
        q->capacity += 2;
        q->procs = realloc(q->procs, q->capacity * sizeof(Process*));
        if (q->procs == NULL) {
            perror("ERROR: Failed to realloc queue");
            exit(1);
        }
    }
    
    if (q->size == 0) {
        enqueue(q, p, 0);
        return;
    }
    
    // Shift everything right
    for (int i = q->size; i > 1; i--) {
        q->procs[i] = q->procs[i-1];
    }
    q->procs[1] = p;
    q->size++;
}

// Sort the Eventqueue by time
void sortEventQueue(EventQueue* eq) {
    for (int i = 1; i < eq->size; i++) {
        Event* key = eq->events[i];
        int j = i - 1;

        // Shift events with greater time to the right
        while (j >= 0 && eq->events[j]->time > key->time) {
            eq->events[j + 1] = eq->events[j];
            j--;
        }

        // Insert the key at the correct position
        eq->events[j + 1] = key;
    }
}

// Change event times
void updateTimes(EventQueue *eq, Process* p, int tslice, int tcs){
    int index = 0;
    for (int i = 0; i < eq->size; i++){
        if (eq->events[i]->state == READY){
            index = i;
            break;
        }
    }

    Process* curr = p;
    Process* temp = NULL;
    int prevIdx = index;
    for (int i = index+1; i < eq->size; i++){
        if (eq->events[i]->state == READY){
            // Change the process
            temp = eq->events[i]->process;
            eq->events[i]->process = curr;

            // Update time
            int *burstRem = eq->events[prevIdx]->process->remainingBursts + (eq->events[prevIdx]->process->numBursts - eq->events[prevIdx]->process->burstsLeft);
            if (*burstRem >= tslice){
                eq->events[i]->time = eq->events[prevIdx]->time + tslice + tcs;
            } else {
                eq->events[i]->time = eq->events[prevIdx]->time + *burstRem + tcs;
            }

            // Update
            curr = temp;
            prevIdx = i;
        }
    }
    Event* cpuBurst = createEvent(curr, getTimeOfLastEvent(eq, eq->events[prevIdx]->time, tslice) + tcs, READY);
    insertEventFCFS(eq, cpuBurst);

    // Resort by times
    sortEventQueue(eq);
}


// Round Robin
int alt_RR(Process** processes, int n, int tcs, int tslice){
    // Reset all processes
    for (int i = 0; i < n; i++) {
        (*(processes+i))->state = ARRIVE;
        (*(processes+i))->burstsLeft = (*(processes+i))->numBursts;
        for (int j = 0; j < (*(processes+i))->numBursts; j++){
            (*(processes+i))->remainingBursts[j] = (*(processes+i))->cpuBursts[j];
        }
        // For writing to simout
        (*(processes+i))->readyTime = 0;
        (*(processes+i))->wait = 0;
        (*(processes+i))->startTime = 0;
        (*(processes+i))->turnaround = 0;
        (*(processes+i))->cs = 0;
        (*(processes+i))->preemptions = 0;
        (*(processes+i))->oneTS = 0;
    }


    // Start Simultaion
    Queue q;
    initQueue(&q, n);
    EventQueue eq;
    initEventQueue(&eq, 4*n);
    printf("time 0ms: Simulator started for RR [Q empty]\n");

    // Arrivals
    for (int i = 0; i < n; i++){
        Event* newEvent = createEvent(*(processes+i), (*(processes+i))->arrivalTime, ARRIVE);
        insertEventFCFS(&eq, newEvent);
    }

    // Start Simulation
    int time = 0;
    int terminatedCount = 0;
    int cpuFreeAt = 0;
    int cpuIdle = -1;
    while (terminatedCount < n) {
        // Handle Events
        Event* e = popEvent(&eq);
        time = e->time;

        // Arrival
        if (e->state == ARRIVE){
            // Print and add to queue
            enqueue(&q, e->process, time);

            // Print
            if (time <= 10000){
                printf("time %dms: Process %s arrived; added to ready queue [Q", time, e->process->pid);
                printQueue(&q);
                printf("]\n");
            }
            
            // CPU is free
            int burstTime = e->process->remainingBursts[e->process->numBursts - e->process->burstsLeft];
            if (cpuIdle == -1 && time >= cpuFreeAt){
                Event* newEvent = createEvent(e->process, time + tcs/2, READY);
                insertEventFCFS(&eq, newEvent);
                if (tslice >= burstTime){
                    cpuFreeAt = time + burstTime + tcs/2;
                } 
                // Burst finishes after time slice
                else{
                    cpuFreeAt = time + tslice + tcs/2;
                }
                dequeue(&q);
            }
            // CPU is not free
            else {
                Event* newEvent = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventFCFS(&eq, newEvent);
                if (tslice >= burstTime){
                    cpuFreeAt += burstTime + tcs;
                } 
                // Burst finishes before timeslice
                else {
                    cpuFreeAt += tslice + tcs;
                }
            }

            // For writing to simout
            e->process->readyTime = time;           // Wait time
        }
        // Start CPU Burst
        else if (e->state == READY){
            e->process->cs++;                                           // Context Switch
            e->process->wait += time - e->process->readyTime - tcs/2;   // Wait time
            
            cpuIdle = 0;
            if (e->process->pid == q.procs[0]->pid){
                dequeue(&q);
            }

            int burstTime = *(e->process->remainingBursts + (e->process->numBursts - e->process->burstsLeft));
            int fullBurst = *(e->process->cpuBursts + (e->process->numBursts - e->process->burstsLeft));

            // Print
            if (time <= 10000){
                if (burstTime != fullBurst){
                    printf("time %dms: Process %s started using the CPU for remaining %dms of %dms burst [Q", time, e->process->pid, burstTime, fullBurst);
                    printQueue(&q);
                    printf("]\n");
                } else {
                    printf("time %dms: Process %s started using the CPU for %dms burst [Q", time, e->process->pid, fullBurst);
                    printQueue(&q);
                    printf("]\n");
                }
            }

            // Update bursts
            int *burstRem = e->process->remainingBursts + (e->process->numBursts - e->process->burstsLeft);
            *burstRem -= tslice;

            // Burst finishes its remaining time
            if (*burstRem <= 0 || e->process->burstsLeft == 0){
                if (e->process->burstsLeft == 0){
                    Event* endCpu = createEvent(e->process, time + burstTime, TERMINATED);
                    insertEventFCFS(&eq, endCpu);
                } else{
                    Event* endCpu = createEvent(e->process, time + burstTime, RUNNING);
                    insertEventFCFS(&eq, endCpu);
                }
            } 
            // Burst is not fully complete
            else {
                Event* endCpu = createEvent(e->process, time + tslice, PREEMPTION);
                insertEventFCFS(&eq, endCpu);
            }

            // For writing to simout
            if (fullBurst <= tslice){
                e->process->oneTS++;
            }
        } 
        // Preemption
        else if (e->state == PREEMPTION) {
            // No preemption
            int *burstRem = e->process->remainingBursts + (e->process->numBursts - e->process->burstsLeft);
            if (q.size == 0){
                // Print
                if (time <= 10000){
                    printf("time %dms: Time slice expired; no preemption because ready queue is empty [Q", time);
                    printQueue(&q);
                    printf("]\n"); 
                }

                // Last time slice before finishing
                if (*burstRem <= tslice){
                    Event* continueBurst = createEvent(e->process, time + *burstRem, RUNNING);
                    insertEventFCFS(&eq, continueBurst);
                    cpuFreeAt += *burstRem;
                } 
                // Still more cpu bursts
                else {
                    *burstRem -= tslice;
                    Event* continueBurst = createEvent(e->process, time + tslice, PREEMPTION);
                    insertEventFCFS(&eq, continueBurst);
                    cpuFreeAt += tslice;
                }
            } else {
                // Print
                if (time <= 10000){
                    printf("time %dms: Time slice expired; preempting process %s with %dms remaining [Q", time, e->process->pid, *burstRem);
                    printQueue(&q);
                    printf("]\n");
                }
                Event* enqueue = createEvent(e->process, time + tcs/2, ENQUEUE);
                insertEventFCFS(&eq, enqueue);

                // For writing to simout
                e->process->preemptions++;              // Preemptions
            }
        } 
        // Add to queue after process is preempted
        else if (e->state == ENQUEUE){
            // For writing to simout
            e->process->readyTime = time;   // Wait time

            alt_enqueue(&q, e->process);

            // Creates an event while considering CPU Bursts times in the queue
            updateTimes(&eq, e->process, tslice, tcs);

            // Get the burst time of the last process ready to run
            int lastProcBurst = getTimeOfLastEvent(&eq, time, tslice);
            cpuFreeAt = lastProcBurst;
        }
        // CPU Burst complete
        else if(e->state == RUNNING){
            // CPU Burst complete
            cpuIdle = -1;
            e->process->burstsLeft--;
            if (e->process->burstsLeft == 0){
                Event* termination = createEvent(e->process, time, TERMINATED);
                insertEventFCFS(&eq, termination);
            } else {
                // Print
                if (time <= 10000){
                    if (e->process->burstsLeft == 1){
                        printf("time %dms: Process %s completed a CPU burst; %d burst to go [Q", time, e->process->pid, e->process->burstsLeft);
                    } else{
                        printf("time %dms: Process %s completed a CPU burst; %d bursts to go [Q", time, e->process->pid, e->process->burstsLeft);
                    }
                    printQueue(&q);
                    printf("]\n");
                }
    
                // IO Burst start
                int ioCompTime = time + *(e->process->ioBursts+(e->process->numBursts - e->process->burstsLeft - 1)) + tcs/2;

                // Print
                if (time <= 10000){
                    printf("time %dms: Process %s switching out of CPU; blocking on I/O until time %dms [Q", time, e->process->pid, ioCompTime);
                    printQueue(&q);
                    printf("]\n");
                }
                Event* ioBurst = createEvent(e->process, ioCompTime, WAITING);
                insertEventFCFS(&eq, ioBurst);
            }
        } 
        // IO End
        else if (e->state == WAITING){
            // Dequeue next process if it runs before the next event 
            if ((eq.events[0]->time - time) < (tcs/2) && eq.events[0]->state == READY){
                dequeue(&q);
            }

            // I/O Burst complete
            enqueue(&q, e->process, time);

            // Print
            if (time <= 10000){
                printf("time %dms: Process %s completed I/O; added to ready queue [Q", time, e->process->pid);
                printQueue(&q);
                printf("]\n");
            }

            // Same process is at the head of the queue
            if (q.size == 1 && cpuIdle == -1){
                cpuFreeAt = time;
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs/2, READY);
                insertEventFCFS(&eq, cpuBurst);
                cpuIdle = 0;
                dequeue(&q);
                int lastProcBurst = getTimeOfLastEvent(&eq, time, tslice);
                cpuFreeAt = lastProcBurst;
            } 
            // Different process from the current process running
            else {
                Event* cpuBurst = createEvent(e->process, cpuFreeAt + tcs, READY);
                insertEventFCFS(&eq, cpuBurst);
                int lastProcBurst = getTimeOfLastEvent(&eq, time, tslice);
                cpuFreeAt = lastProcBurst;
            }

            // For writing to simout
            e->process->readyTime = time;          // Wait time
        }
        // Termination
        else if (e->state == TERMINATED){
            e->process->turnaround = time + (tcs/2) - e->process->arrivalTime; // Turnaround time
            cpuIdle = -1;
            e->process->burstsLeft--;
            printf("time %dms: Process %s terminated [Q", time, e->process->pid);
            printQueue(&q);
            printf("]\n");
            terminatedCount++;
        }
        free(e);
    }

    time += tcs/2;
    printf("time %dms: Simulator ended for RR [Q empty]\n", time);
    freeEventQueue(&eq);
    free(q.procs);
    return time;
}



double nextExp(double lambda, double upperBound){
    double r = drand48();
    double x = -log(r) / lambda;
    while (x > upperBound){
        r = drand48();
        x = -log(r) / lambda;
    }
    return x;
}

// helper for rounding to write to simout
double ceil3(double value) {
    return ceil(value * 1000) / 1000;
}

int main(int argc, char** argv){
    if (argc < 9){
        perror("ERROR: Invalid argument(s)");
        return EXIT_FAILURE;
    }

    // Get parameters
    int n = atoi(*(argv+1));
    int ncpu = atoi(*(argv+2));
    int seed = atoi(*(argv+3));
    double lambda = atof(*(argv+4));
    if (lambda <=0){
        perror("ERROR: Lambda <= 0");
        return EXIT_FAILURE;
    }
    int upperBound = atoi(*(argv+5));
    int tcs = atoi(*(argv+6));
    if (tcs < 0){
        perror("ERROR: Negative context switch");
        return EXIT_FAILURE;
    }
    double alpha = atof(*(argv+7));
    if (alpha > 1 || alpha < 0){
        perror("ERROR: Alpha is not in the range 0 to 1");
        return EXIT_FAILURE;
    }
    int tslice = atoi(*(argv+8));
    if (tslice < 0){
        perror("ERROR: Negative timeslice");
        return EXIT_FAILURE;
    }
    if (ncpu == 1){
        printf("<<< -- process set (n=%d) with %d CPU-bound process\n", n, ncpu);
    } else {
        printf("<<< -- process set (n=%d) with %d CPU-bound processes\n", n, ncpu);
    }
    printf("<<< -- seed=%d; lambda=%.6f; bound=%d\n\n", seed,lambda, upperBound);

    // Simulation Calcs
    srand48(seed);
    Process **processes = calloc(n, sizeof(Process*));
    char *letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int letterCount = -1;
    for (int i = 0; i < n; i++) {
        // Create processes
        *(processes+i) = calloc(1, sizeof(Process));
        if (i % 10 == 0){
            letterCount++;
        }
        (*(processes+i))->pid = calloc(4, sizeof(char));
        sprintf((*(processes + i))->pid, "%c%d", *(letters+letterCount), (i % 10));

        // Get arrival times
        double arrivalExp = nextExp(lambda, upperBound);
        int arrivalTime = (int)floor(arrivalExp);
        int numBursts = (int)ceil(drand48() * 32);
        (*(processes+i))->arrivalTime = arrivalExp;
        (*(processes+i))->endTime = INT_MAX;
        (*(processes+i))->numBursts = numBursts;
        (*(processes+i))->cpuBursts = calloc(numBursts + 1, sizeof(int));
        (*(processes+i))->ioBursts = calloc(numBursts, sizeof(int));
        // For SJF/SRT
        (*(processes+i))->tau = (int)ceil(1.0 / lambda);
        // For RR
        (*(processes+i))->remainingBursts = calloc(numBursts + 1, sizeof(int));

        // Print Process Info
        if (i < ncpu){
            if (numBursts == 1){
                printf("CPU-bound process %s: arrival time %dms; %d CPU burst:\n", (*(processes+i))->pid, arrivalTime, numBursts);
            } else {
                printf("CPU-bound process %s: arrival time %dms; %d CPU bursts:\n", (*(processes+i))->pid, arrivalTime, numBursts);
            }
        } else{
            if (numBursts == 1){
                printf("I/O-bound process %s: arrival time %dms; %d CPU burst:\n",(*(processes+i))->pid, arrivalTime, numBursts);
            } else {
                printf("I/O-bound process %s: arrival time %dms; %d CPU bursts:\n",(*(processes+i))->pid, arrivalTime, numBursts);
            }
        }
        
        // Simulate CPU Bursts
        for (int j = 0; j < numBursts; j++) {
            int cpuBurst = (int)ceil(nextExp(lambda, upperBound));
            if (j < numBursts - 1) {
                int ioBurst = (int)ceil(nextExp(lambda, upperBound));
                // Check for CPU-bound Process
                if (i < ncpu){
                    cpuBurst *= 4;
                } else{
                    ioBurst *= 8;
                }
                *((*(processes+i))->ioBursts+j) = ioBurst;
                printf("==> CPU burst %dms ==> I/O burst %dms\n", cpuBurst, ioBurst);
            } else {
                if (i < ncpu){
                    cpuBurst *= 4;
                }
                printf("==> CPU burst %dms\n\n", cpuBurst);
            }
            *((*(processes+i))->cpuBursts+j) = cpuBurst;
        }
    }

    printf("<<< PROJECT SIMULATIONS\n");
    if (alpha == -1){
        printf("<<< -- t_cs=%dms; alpha=<n/a>; t_slice=%dms", tcs, tslice);
    } else {
        printf("<<< -- t_cs=%dms; alpha=%.2f; t_slice=%dms", tcs, alpha, tslice);
    }

    if (*(argv+9)){
        if (strcasecmp(*(argv+9), "RR_ALT") == 0){
            printf("; RR_ALT");
        }
    }
    printf("\n");

    // Write to file
    // Open the output file for writing.
    FILE *fp = fopen("simout.txt", "w");
    if (fp == NULL) {
        perror("Error opening file");
        return 1;
    }
    
    // Write general simulation statistics
    fprintf(fp, "-- number of processes: %d\n", n);
    fprintf(fp, "-- number of CPU-bound processes: %d\n", ncpu);
    fprintf(fp, "-- number of I/O-bound processes: %d\n", n-ncpu);
    
    // calculate overall stats
    double cpuBoundBurst = 0.0;
    double ioBoundBurst = 0.0;
    int numCpuBurst = 0;
    int numIoBurst = 0;
    // CPU burst calc
    for (int i = 0; i < n; i++){
        Process *p = processes[i];
        for (int j = 0; j < p->numBursts; j++){
            if (i < ncpu) {
                cpuBoundBurst += p->cpuBursts[j];
                numCpuBurst++;
            } else {
                ioBoundBurst += p->cpuBursts[j];
                numIoBurst++;
            }
        }
    }

    double cpuIOBurst = 0.0;
    double ioIOBurst = 0.0;
    int numCpuIOBurst = 0;
    int numIoIOBurst = 0;
    // IO Bursts calc
    for (int i = 0; i < n; i++){
        Process *p = processes[i];
        for (int j = 0; j < p->numBursts - 1; j++){
            if (i < ncpu) {
                cpuIOBurst += p->ioBursts[j];
                numCpuIOBurst++;
            } else {
                ioIOBurst += p->ioBursts[j];
                numIoIOBurst++;
            }
        }
    }

    fprintf(fp, "-- CPU-bound average CPU burst time: %.3f ms\n", ceil3(cpuBoundBurst/numCpuBurst) );
    fprintf(fp, "-- I/O-bound average CPU burst time: %.3f ms\n", ceil3(ioBoundBurst/numIoBurst) );
    fprintf(fp, "-- overall average CPU burst time: %.3f ms\n",  ceil3( (cpuBoundBurst + ioBoundBurst)/(numCpuBurst+numIoBurst)) );
    fprintf(fp, "-- CPU-bound average I/O burst time: %.3f ms\n", ceil3(cpuIOBurst/numCpuIOBurst) );
    fprintf(fp, "-- I/O-bound average I/O burst time: %.3f ms\n", ceil3(ioIOBurst/numIoIOBurst) );
    fprintf(fp, "-- overall average I/O burst time: %.3f ms\n\n", ceil3( (cpuIOBurst + ioIOBurst)/(numCpuIOBurst+numIoIOBurst)) );
    

    // FCFS
    int fcfsTime = FCFS(processes, n, tcs);
    // Write FCFS 
    fprintf(fp, "Algorithm FCFS\n");
    fprintf(fp, "-- CPU utilization: %.3f%%\n", ceil3((cpuBoundBurst + ioBoundBurst)/fcfsTime * 100));

    // calc wait time
    double cpuWait = 0.0;
    double ioWait = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            cpuWait+= processes[i]->wait;
        } else {
            ioWait += processes[i]->wait;
        }
    }

    fprintf(fp, "-- CPU-bound average wait time: %.3f ms\n", ceil3(cpuWait / numCpuBurst));
    fprintf(fp, "-- I/O-bound average wait time: %.3f ms\n", ceil3(ioWait / numIoBurst));
    fprintf(fp, "-- overall average wait time: %.3f ms\n", ceil3( (cpuWait + ioWait) / (numCpuBurst+numIoBurst) ));
    
    // calc turnaround time
    double fcfsCpuTR = 0.0;
    double fcfsIoTR = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            fcfsCpuTR += processes[i]->turnaround;
        } else {
            fcfsIoTR += processes[i]->turnaround;
        }
    }
    fprintf(fp, "-- CPU-bound average turnaround time: %.3f ms\n", ceil3(fcfsCpuTR / numCpuBurst));
    fprintf(fp, "-- I/O-bound average turnaround time: %.3f ms\n", ceil3(fcfsIoTR / numIoBurst));
    fprintf(fp, "-- overall average turnaround time: %.3f ms\n", ceil3( (fcfsCpuTR + fcfsIoTR) / (numCpuBurst+numIoBurst) ));
    
    // calc context switches
    int fcfsCpuCs = 0;
    int fcfsIoCs = 0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            fcfsCpuCs  += processes[i]->cs;
        } else {
            fcfsIoCs += processes[i]->cs;
        }
    }
    fprintf(fp, "-- CPU-bound number of context switches: %d\n", fcfsCpuCs);
    fprintf(fp, "-- I/O-bound number of context switches: %d\n", fcfsIoCs);
    fprintf(fp, "-- overall number of context switches: %d\n", fcfsCpuCs + fcfsIoCs);
    fprintf(fp, "-- CPU-bound number of preemptions: 0\n");
    fprintf(fp, "-- I/O-bound number of preemptions: 0\n");
    fprintf(fp, "-- overall number of preemptions: 0\n\n");

    // SJF
    int sjfTime = SJF(processes, n, tcs, alpha, lambda);

    // Write SJF 
    fprintf(fp, "Algorithm SJF\n");
    
    fprintf(fp, "-- CPU utilization: %.3f%%\n", ceil3((cpuBoundBurst + ioBoundBurst) / sjfTime * 100));

    // calc wait time 
    cpuWait = 0.0;
    ioWait = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            cpuWait += processes[i]->wait;
        } else {
            ioWait += processes[i]->wait;
        }
    }
    fprintf(fp, "-- CPU-bound average wait time: %.3f ms\n", ceil3(cpuWait / numCpuBurst));
    fprintf(fp, "-- I/O-bound average wait time: %.3f ms\n", ceil3(ioWait / numIoBurst));
    fprintf(fp, "-- overall average wait time: %.3f ms\n", ceil3((cpuWait + ioWait) / (numCpuBurst + numIoBurst)));

    // calc turnaround time
    double sjfCpuTR = 0.0;
    double sjfIoTR = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            sjfCpuTR += processes[i]->turnaround;
        } else {
            sjfIoTR += processes[i]->turnaround;
        }
    }
    fprintf(fp, "-- CPU-bound average turnaround time: %.3f ms\n", ceil3(sjfCpuTR / numCpuBurst));
    fprintf(fp, "-- I/O-bound average turnaround time: %.3f ms\n", ceil3(sjfIoTR / numIoBurst));
    fprintf(fp, "-- overall average turnaround time: %.3f ms\n", ceil3((sjfCpuTR + sjfIoTR) / (numCpuBurst + numIoBurst)));

    // calc context switches for 
    int sjfCpuCs = 0;
    int sjfIoCs = 0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            sjfCpuCs += processes[i]->cs;
        } else {
            sjfIoCs += processes[i]->cs;
        }
    }
    fprintf(fp, "-- CPU-bound number of context switches: %d\n", sjfCpuCs);
    fprintf(fp, "-- I/O-bound number of context switches: %d\n", sjfIoCs);
    fprintf(fp, "-- overall number of context switches: %d\n", sjfCpuCs + sjfIoCs);

    fprintf(fp, "-- CPU-bound number of preemptions: 0\n");
    fprintf(fp, "-- I/O-bound number of preemptions: 0\n");
    fprintf(fp, "-- overall number of preemptions: 0\n\n");

    // SRT
    int srtTime = SRT(processes, n, tcs, alpha, lambda);
    // Write SRT
    fprintf(fp, "Algorithm SRT\n");
    fprintf(fp, "-- CPU utilization: %.3f%%\n", ceil3((cpuBoundBurst + ioBoundBurst)/srtTime * 100));

    // calc wait time 
    cpuWait = 0.0;
    ioWait = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            cpuWait += processes[i]->wait;
        } else {
            ioWait += processes[i]->wait;
        }
    }
    fprintf(fp, "-- CPU-bound average wait time: %.3f ms\n", ceil3(cpuWait / numCpuBurst));
    fprintf(fp, "-- I/O-bound average wait time: %.3f ms\n", ceil3(ioWait / numIoBurst));
    fprintf(fp, "-- overall average wait time: %.3f ms\n", ceil3((cpuWait + ioWait) / (numCpuBurst + numIoBurst)));

    // calc turnaround time
    double srtCpuTR = 0.0;
    double srtIoTR = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            srtCpuTR += processes[i]->turnaround;
        } else {
            srtIoTR += processes[i]->turnaround;
        }
    }
    fprintf(fp, "-- CPU-bound average turnaround time: %.3f ms\n", ceil3(srtCpuTR / numCpuBurst));
    fprintf(fp, "-- I/O-bound average turnaround time: %.3f ms\n", ceil3(srtIoTR / numIoBurst));
    fprintf(fp, "-- overall average turnaround time: %.3f ms\n", ceil3((srtCpuTR + srtIoTR) / (numCpuBurst + numIoBurst)));

    // calc context switches 
    int srtCpuCs = 0;
    int srtIoCs = 0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            srtCpuCs += processes[i]->cs;
        } else {
            srtIoCs += processes[i]->cs;
        }
    }
    fprintf(fp, "-- CPU-bound number of context switches: %d\n", srtCpuCs);
    fprintf(fp, "-- I/O-bound number of context switches: %d\n", srtIoCs);
    fprintf(fp, "-- overall number of context switches: %d\n", srtCpuCs + srtIoCs);

    fprintf(fp, "-- CPU-bound number of preemptions: 0\n");
    fprintf(fp, "-- I/O-bound number of preemptions: 0\n");
    fprintf(fp, "-- overall number of preemptions: 0\n\n");
	
    // RR
    int rrTime = 0;

    // NEW optional 9th arg
    if (argc == 10){
        if (strcmp(*(argv+9),"RR_ALT") == 0){
            rrTime = alt_RR(processes, n, tcs, tslice);
        }
    } else {
       rrTime = RR(processes, n, tcs, tslice);
    }
    
    // Write RR
    fprintf(fp, "Algorithm RR\n");
    fprintf(fp, "-- CPU utilization: %.3f%%\n", ceil3((cpuBoundBurst + ioBoundBurst)/rrTime * 100));

    // calc wait time
    double rrCpuWait = 0.0;
    double rrIoWait = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            rrCpuWait+= processes[i]->wait;
        } else {
            rrIoWait += processes[i]->wait;
        }
    }
    fprintf(fp, "-- CPU-bound average wait time: %.3f ms\n", ceil3(rrCpuWait / numCpuBurst));
    fprintf(fp, "-- I/O-bound average wait time: %.3f ms\n", ceil3(rrIoWait / numIoBurst));
    fprintf(fp, "-- overall average wait time: %.3f ms\n", ceil3( (rrCpuWait + rrIoWait) / (numCpuBurst+numIoBurst) ));

    // calc turnaround time
    double rrCpuTR = 0.0;
    double rrIoTR = 0.0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            rrCpuTR += processes[i]->turnaround;
        } else {
            rrIoTR += processes[i]->turnaround;
        }
    }
    fprintf(fp, "-- CPU-bound average turnaround time: %.3f ms\n", ceil3((rrCpuTR- cpuIOBurst)/ numCpuBurst));
    fprintf(fp, "-- I/O-bound average turnaround time: %.3f ms\n", ceil3((rrIoTR- ioIOBurst) / numIoBurst));
    fprintf(fp, "-- overall average turnaround time: %.3f ms\n", ceil3( ((rrCpuTR- cpuIOBurst) + (rrIoTR- ioIOBurst)) / (numCpuBurst+numIoBurst) ));

    // calc context switches
    int RRCpuCs = 0;
    int RRIoCs = 0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            RRCpuCs += processes[i]->cs;
        } else {
            RRIoCs += processes[i]->cs;
        }
    }
    fprintf(fp, "-- CPU-bound number of context switches: %d\n", RRCpuCs);
    fprintf(fp, "-- I/O-bound number of context switches: %d\n", RRIoCs);
    fprintf(fp, "-- overall number of context switches: %d\n", RRCpuCs + RRIoCs);

    // calc preemptions
    int cpuPre = 0;
    int ioPre = 0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            cpuPre += processes[i]->preemptions;
        } else {
            ioPre += processes[i]->preemptions;
        }
    }
    fprintf(fp, "-- CPU-bound number of preemptions: %d\n", cpuPre);
    fprintf(fp, "-- I/O-bound number of preemptions: %d\n", ioPre);
    fprintf(fp, "-- overall number of preemptions: %d\n", cpuPre + ioPre);
    
    // Additional RR algorithm stats
    int cpuOneTS = 0;
    int ioOneTS = 0;
    for (int i = 0; i < n; i++){
        if (i < ncpu) {
            cpuOneTS += processes[i]->oneTS;
        } else {
            ioOneTS += processes[i]->oneTS;
        }
    }
    fprintf(fp, "-- CPU-bound percentage of CPU bursts completed within one time slice: %.3f%%\n", ceil3(100.0 * cpuOneTS/numCpuBurst) );
    fprintf(fp, "-- I/O-bound percentage of CPU bursts completed within one time slice: %.3f%%\n", ceil3(100.0 * ioOneTS/numIoBurst) );
    fprintf(fp, "-- overall percentage of CPU bursts completed within one time slice: %.3f%%\n", ceil3( (100.0 *(cpuOneTS + ioOneTS)/(numCpuBurst + numIoBurst)) ));
    fclose(fp);

    // Clean up
    for (int i = 0; i < n; i++){
        free((*(processes+i))->pid);
        free((*(processes+i))->cpuBursts);
        free((*(processes+i))->ioBursts);
        free((*(processes+i))->remainingBursts);
        free(*(processes+i));
    }
    free(processes);

}