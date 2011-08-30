#include "hw6.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

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
struct Passenger {
	int elevator;	
	int from_floor;
	int to_floor;	
	int finished;
	
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
			for(int i=0;i<passenger_count;i++) {								
				// search for closest passenger
				int distance = abs(passengers[i].from_floor-at_floor);
				if(!passengers[i].finished &&
					 distance < closest_distance) {
					closest_distance = distance;
					closest_passenger = i;
				}
			}
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
