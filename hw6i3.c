#include "hw6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
// The following is just some header information for our queue

struct Queue* queue(struct Queue*, double, int);
int dequeue(struct Queue*);
struct Queue* new_queue();
void delete_queue(struct Queue*);
void print_queue(struct Queue*);
double getWeight(int);
// The following are the structers necessary for the queue implementation

struct Node {
	double elem;
	int pass;
	struct Node* next;
};

struct Queue {
	struct Node* head;
};

struct timeval time_val;
double time_ret;
// End queue data structures

struct Elevator {
	pthread_mutex_t lock;	
	pthread_barrier_t barrier;

	int floor;
	int direction;
	int next;
	int occupancy;
	enum {ELEVATOR_ARRIVED, ELEVATOR_OPEN, ELEVATOR_CLOSED} state;	
} elevators[ELEVATORS];

// global lock over all passengers and passenger_count
pthread_mutex_t passenger_lock=PTHREAD_MUTEX_INITIALIZER;
int passenger_count=0; 

//Holds the requests for each passenger.
int pass_request[ELEVATORS][PASSENGERS];
int current_passenger;
int elevator_floor_tracker[ELEVATORS];
struct Queue *elevator_queue;


struct Passenger {
	int elevator;	
	int from_floor;
	int to_floor;	
	int finished;
    //adding this new piece
	double end_time;	

	pthread_mutex_t wakeup_lock;
	pthread_cond_t wakeup;
} passengers[PASSENGERS];

void scheduler_init() {	
	memset(passengers,0,sizeof(passengers));
	for(int i=0;i<PASSENGERS;i++) {
		pthread_cond_init(&passengers[i].wakeup,0);
		pthread_mutex_init(&passengers[i].wakeup_lock,0);
	}
	
	for(int i=0;i<ELEVATORS;i++) {
		elevators[i].floor=0;		
		elevators[i].direction=1;
		elevators[i].next=-1;
		elevators[i].occupancy=0;
		elevators[i].state=ELEVATOR_ARRIVED;
		pthread_mutex_init(&elevators[i].lock,0);
		pthread_barrier_init(&elevators[i].barrier,0,2);
	}
    elevator_queue = new_queue();
}

void passenger_request(int passenger, int from_floor, int to_floor, 
											 void (*enter_elevator)(int, int), 
											 void(*exit_elevator)(int, int))
{	
	// add passenger to passenger manifest
	pthread_mutex_lock(&passenger_lock);
	if(passenger >= passenger_count)
		passenger_count=passenger+1;
	passengers[passenger].elevator=-1;
	passengers[passenger].from_floor=from_floor;
	passengers[passenger].to_floor=to_floor;
	passengers[passenger].finished=0;


	//This is where we queue the passenger into the elevator queue
	queue(elevator_queue, passengers[passenger].end_time, passenger);
    

	pthread_mutex_unlock(&passenger_lock);

	// wait for elevator assignment
	pthread_mutex_lock(&passengers[passenger].wakeup_lock);
	pthread_cond_wait(&passengers[passenger].wakeup, &passengers[passenger].wakeup_lock);	
	pthread_mutex_unlock(&passengers[passenger].wakeup_lock);

	int elevator=passengers[passenger].elevator;
	log(3,"Passenger was %d assigned to elevator %d\n",passenger,elevator);

	int waiting=1;
	// get passenger into elevator once it's at origin floor
	while(waiting) {
		log(3,"Passenger %d awaiting elevator %d floor arrival at %d, occupancy %d.\n",passenger,elevator,from_floor,elevators[elevator].occupancy);
		pthread_barrier_wait(&elevators[elevator].barrier);

		log(3,"Passenger %d alerted to elevator %d floor arrival at %d, occupancy %d.\n",passenger,elevator,from_floor,elevators[elevator].occupancy);
		pthread_mutex_lock(&elevators[elevator].lock);
		log(3,"Passenger %d alerted to elevator %d floor arrival at %d, occupancy %d (got lock).\n",passenger,elevator,from_floor,elevators[elevator].occupancy);

		if(elevators[elevator].floor == from_floor && 
			 elevators[elevator].state == ELEVATOR_OPEN && 
			 elevators[elevator].occupancy < MAX_CAPACITY) {
			enter_elevator(passenger, elevator);
			elevators[elevator].occupancy++;
			elevators[elevator].next = to_floor;
			//Picked up by elevator

			waiting=0;
		}

		pthread_mutex_unlock(&elevators[elevator].lock);
		
		pthread_barrier_wait(&elevators[elevator].barrier);
	}
	
	int riding=1;
	// get passenger out of elevator once it's at destination floor
	while(riding) {
		pthread_barrier_wait(&elevators[elevator].barrier);
		pthread_mutex_lock(&elevators[elevator].lock);
		log(3,"Passenger %d alerted to elevator %d floor delivery at %d, state %d.\n",passenger,elevator,to_floor,elevators[elevator].state);

		if(elevators[elevator].floor == to_floor && elevators[elevator].state == ELEVATOR_OPEN) {
			
			//Update the last time used...
			
			//passenger[passengers].end_time=tim.tv_usec;
 		    gettimeofday(&time_val, 0);           
			//passenger[passengers].end_time=(double) time_val.tv_sec + (double) 1e-6 * time_val.tv_usec + getWeight(passenger); 
            passenger[passengers].end_time=getWeight(passenger);
			log(3, "Passenger %f has time\n", passenger[passengers].end_time);
			exit_elevator(passenger, elevator);
			elevators[elevator].next=-1;
			// now schedule next trip
			elevators[elevator].occupancy--;
			riding=0;
		}

		pthread_mutex_unlock(&elevators[elevator].lock);

		pthread_barrier_wait(&elevators[elevator].barrier);
	}

}

void elevator_ready(int elevator, int at_floor, 
										void(*move_direction)(int, int), 
										void(*door_open)(int), void(*door_close)(int)) {
	pthread_mutex_lock(&elevators[elevator].lock);

	if(elevators[elevator].state == ELEVATOR_ARRIVED && elevators[elevator].next == at_floor) {
		door_open(elevator);
		elevators[elevator].state=ELEVATOR_OPEN;		
		pthread_mutex_unlock(&elevators[elevator].lock);

		log(3,"Elevator %d signaling arrival.\n",elevator);
		pthread_barrier_wait(&elevators[elevator].barrier);
		// gives the passenger time to board
		pthread_barrier_wait(&elevators[elevator].barrier);

		return;
	}
	else if(elevators[elevator].state == ELEVATOR_OPEN) {
		door_close(elevator);
		elevators[elevator].state=ELEVATOR_CLOSED;
	}
	else {
		// find an unserved passenger
		if(elevators[elevator].next<0) {
			log(7,"Elevator %d acquiring passenger scheduling lock.\n",elevator);
			pthread_mutex_lock(&passenger_lock);
			int closest_passenger = -1;
			int closest_distance = 1000;
			print_queue(elevator_queue);	
			// Pick the next passenger
			closest_passenger=dequeue(elevator_queue);
						
			print_queue(elevator_queue);
			if(closest_passenger!=-1) {
				passengers[closest_passenger].finished=1;
				passengers[closest_passenger].elevator=elevator;
				elevators[elevator].next=passengers[closest_passenger].from_floor;

				log(3,"Elevator %d assigned to passenger %d, next floor %d.\n",elevator,closest_passenger,elevators[elevator].next);
				pthread_cond_signal(&passengers[closest_passenger].wakeup);
			}

			pthread_mutex_unlock(&passenger_lock);
		}

		// if we've got a passenger, go straight to destination
		if(elevators[elevator].next>=0) {
			if(at_floor < elevators[elevator].next) {
				move_direction(elevator,1);
				elevators[elevator].floor++;
			}
			else if(at_floor > elevators[elevator].next) {
				move_direction(elevator,-1);
				elevators[elevator].floor--;
			}
		}
		elevators[elevator].state=ELEVATOR_ARRIVED;
	}
	pthread_mutex_unlock(&elevators[elevator].lock);
}

// These are some basic functions for the queue...

void delete_queue(struct Queue* q) {
	struct Node* node;
	while (q->head) {
		node=q->head->next;
		free(q->head);
	}
	free(q);
}
//
void print_queue(struct Queue* q) {
	struct Node* n = q->head;
	log(3,"---------------------------------\n", 0);
	while (n) {
		log(3, "Contents are, pass: %i, time: %f\n", n->pass, n->elem); 
		n=n->next;
	}
	log(3,"---------------------------------\n", 0);

}

double getWeight(int passenger) {
    //So weight is 0, 1, 2, ..., 49
    double weight = (double)passenger;
    /*if (passenger < 10 ) {
        weight = weight + 4;
    }
    else if (passenger < 20 && passenger >= 10) {
        weight = weight + 3;
    }
    else if (passenger < 30 && passenger >= 20) {
        weight = weight + 2;
    
    }
    else if (passenger < 40 && passenger >= 30) {
        weight = weight + 1;
    
    }//If any higher, there is no penalty
    
    weight = weight * ((double)PASSENGERS)/(double)(passenger+1) ;
    */
    return weight*-1.00;
}


//*/
struct Queue* new_queue() {
	struct Queue* q = (struct Queue*) malloc(sizeof(struct Queue));
    q->head=0;
	return q;	
}


int dequeue(struct Queue* q) {
	if (!q->head) {
        return -1 ;      
	}
    struct Node *temp = q->head; 
    int pass = q->head->pass;
    q->head=q->head->next;
    free(temp);
    return pass;
}

/*
*   new.. The queue part of the priority queue...
*/
struct Queue* queue(struct Queue* q, double elem, int pass) {
	struct Node *n = (struct Node*) malloc(sizeof(struct Node));
	struct Node *prev, *current;
    n->elem=elem;
    n->pass=pass;
    if (q->head==0) {
        q->head=n;
    }
    else {
        if (q->head->elem > elem) {
            n->next=q->head;
            q->head=n;
        }
        else {
            prev=q->head;
            current=q->head->next;
            while (current && current->elem < elem ) {
                prev=prev->next;
                current=current->next;
            }
            prev->next=n;
            n->next=current;
        }
    }
   
	return q;
}
