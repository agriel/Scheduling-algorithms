#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<time.h>
#include<sys/time.h>
#include<pthread.h>
#include<unistd.h>
#include <sched.h>
#include"hw6.h"

// when stop == 1, all threads quit voluntarily
static int stop=0;

/* This is an internal struct used by the enforcement system 
	 - there is no access to this from hw5 solution. */
static struct Elevator {
	int seqno, last_action_seqno; // these two are used to enforce control rules
	int floor;	
	int open;
	int passengers;
	int trips;
} elevators[ELEVATORS];

void elevator_check(int elevator) {
	if(elevators[elevator].seqno == elevators[elevator].last_action_seqno) {
		log(0,"VIOLATION: elevator %d make at most one action call per elevator_ready()\n",elevator);
		exit(1);
	}
	if(elevators[elevator].passengers > MAX_CAPACITY || elevators[elevator].passengers < 0) {
		log(0,"VIOLATION: elevator %d over capacity, or negative passenger count %d!\n",elevator,elevators[elevator].passengers);
		exit(1);
	}
	elevators[elevator].last_action_seqno=elevators[elevator].seqno;
}

void elevator_move_direction(int elevator, int direction) {
	elevator_check(elevator);
	log(8,"Moving elevator %d %s from %d\n",elevator,(direction==-1?"down":"up"),elevators[elevator].floor);
	if(elevators[elevator].open) {
		log(0,"VIOLATION: attempted to move elevator %d with door open.\n", elevator);
		exit(1);
	}	
	if(elevators[elevator].floor >= FLOORS || elevators[elevator].floor < 0) {
		log(0,"VIOLATION: attempted to move elevator %d outside of building!\n", elevator);
		exit(1);
	}	
	
	sched_yield();
	usleep(DELAY);
	elevators[elevator].floor+=direction;
}

void elevator_open_door(int elevator) {	
	elevator_check(elevator);
	log(9,"Opening elevator %d at floor %d\n",elevator,elevators[elevator].floor);
	if(elevators[elevator].open) {
		log(0,"VIOLATION: attempted to open elevator %d door when already open.\n", elevator);
		exit(1);
	}	
	elevators[elevator].open=1;
	usleep(10*DELAY);
}

void elevator_close_door(int elevator) {
	elevator_check(elevator);
	log(9,"Closing elevator %d at floor %d\n",elevator,elevators[elevator].floor);
	if(!elevators[elevator].open) {
		log(0,"VIOLATION: attempted to close elevator %d door when already closed.\n", elevator);
		exit(1);
	}	
	sched_yield();
	usleep(10*DELAY);
	elevators[elevator].open=0;
}

static int elevator_count = 0;

void* start_elevator(void *arg) {
	int elevator = elevator_count++;
	struct Elevator *e = &elevators[elevator];
	e->last_action_seqno = 0;
	e->seqno = 1;
	e->passengers = 0;
	e->trips = 0;
	log(6,"Starting elevator %d\n", elevator);

	e->floor = 0;//elevator % (FLOORS-1);
	while(!stop) {
		e->seqno++;
		elevator_ready(elevator,e->floor,elevator_move_direction,elevator_open_door,elevator_close_door);
		sched_yield();
	}
}

/* This is an internal struct used by the enforcement system 
	 - there is no access to this from hw5 solution. */

static struct Passenger {
	int id;
	int from_floor;
	int to_floor;
	int in_elevator;
  int trips;

	int active;
	struct timeval started_at;
	double sum_duration;
	double max_duration;
	enum {WAITING, ENTERED, EXITED} state;
} passengers[PASSENGERS];

void passenger_enter(int passenger, int elevator) {
	if(passengers[passenger].from_floor != elevators[elevator].floor) {
		log(0,"VIOLATION: let passenger %d on on wrong floor %d!=%d.\n", passengers[passenger].id, passengers[passenger].from_floor, elevators[elevator].floor);
		exit(1);
	}	
	if(!elevators[elevator].open) {
		log(0,"VIOLATION: passenger %d walked into a closed door entering elevator %d.\n", passengers[passenger].id, elevator);
		exit(1);
	}	
	if(elevators[elevator].passengers == MAX_CAPACITY) {
		log(0,"VIOLATION: passenger %d attempted to board full elevator %d.\n", passengers[passenger].id, elevator);
		exit(1);
	}	
	if(passengers[passenger].state!=WAITING) {
		log(0,"VIOLATION: passenger %d told to board elevator %d, was not waiting.\n", passengers[passenger].id, elevator);
		exit(1);
	}

	log(6,"Passenger %d got on elevator %d at %d, requested %d\n", passengers[passenger].id, elevator, passengers[passenger].from_floor, elevators[elevator].floor);
	elevators[elevator].passengers++;
	passengers[passenger].in_elevator = elevator;
	passengers[passenger].state = ENTERED;

	sched_yield();
	usleep(DELAY);
}

void passenger_exit(int passenger, int elevator) {
	if(passengers[passenger].to_floor != elevators[elevator].floor) {
		log(0,"VIOLATION: let passenger %d off on wrong floor %d!=%d.\n", passengers[passenger].id, passengers[passenger].to_floor, elevators[elevator].floor);
		exit(1);
	}	
	if(!elevators[elevator].open) {
		log(0,"VIOLATION: passenger %d walked into a closed door leaving elevator %d.\n", passengers[passenger].id, elevator);
		exit(1);
	}
	if(passengers[passenger].state!=ENTERED) {
		log(0,"VIOLATION: passenger %d told to board elevator %d, was not waiting.\n", passengers[passenger].id, elevator);
		exit(1);
	}
	
	log(6,"Passenger %d got off elevator %d at %d, requested %d\n", passengers[passenger].id, elevator, passengers[passenger].to_floor, elevators[elevator].floor);
	elevators[elevator].passengers--;
	elevators[elevator].trips++;
	passengers[passenger].in_elevator = -1;
	passengers[passenger].state = EXITED;
	passengers[passenger].trips++;

	struct timeval after;
	gettimeofday(&after,0);
	double duration = after.tv_sec - passengers[passenger].started_at.tv_sec + (after.tv_usec - passengers[passenger].started_at.tv_usec)/1000000.0;
	passengers[passenger].sum_duration += duration;
	if(duration > passengers[passenger].max_duration)
		passengers[passenger].max_duration = duration;
	
	sched_yield();
	usleep(DELAY);
}

pthread_mutex_t plock;
static int passenger_count = 0;

void* start_passenger(void *arg) {
	pthread_mutex_lock(&plock);
	int passenger=passenger_count++;
	int wait = passenger_count*10;
	pthread_mutex_unlock(&plock);

	usleep(wait*DELAY);	 

	struct Passenger *p = &passengers[passenger];
	log(6,"Starting passenger %d\n", passenger);
	p->from_floor = random()%FLOORS;
	p->in_elevator = -1;
	p->trips = 0;
	p->active = 1;
	p->sum_duration = 0;
	p->max_duration = 0;
	p->id = passenger;
	


	while(!stop) {

		if(p->from_floor!=0 && (random()%2))
			p->to_floor = 0;
		else
			p->to_floor = 1 + random() % (FLOORS-1);

		log(6,"Passenger %d requesting %d->%d\n",
						passenger,p->from_floor,p->to_floor);

		struct timeval before;
		gettimeofday(&before,0);
		passengers[passenger].state=WAITING;
		gettimeofday(&passengers[passenger].started_at,0);

		passenger_request(passenger, p->from_floor, p->to_floor, passenger_enter, passenger_exit);
		struct timeval after;
		gettimeofday(&after,0);
		int ms = (after.tv_sec - before.tv_sec)*1000 + (after.tv_usec - before.tv_usec)/1000;
		log(1,"Passenger %d trip duration %d ms, %d slots\n",passenger,ms,ms*1000/DELAY);
		p->from_floor = p->to_floor;
		//		usleep(DELAY); // sleep for some time
		usleep(wait*DELAY);	 
	}
}

void* draw_state(void *ptr) {
	while(!stop) {
		printf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
		for(int floor=FLOORS-1;floor>=0;floor--) {
      printf("%d\t",floor);
			for(int el=0;el<ELEVATORS;el++) {

				if(elevators[el].floor==floor) 	printf(" %c ",(elevators[el].open?'O':'_'));
				else
					printf(" %c ",elevators[el].floor>floor?'|':' ');
			}
 			printf("    ");
			int align = 5+5*ELEVATORS-ELEVATORS*ELEVATORS/4;
			for(int p=0;p<PASSENGERS;p++)
				if((passengers[p].state==ENTERED && elevators[passengers[p].in_elevator].floor==floor)) {
					align-=7;
					printf("%02d->%02d ",p,passengers[p].to_floor);
				}
			while(align-->0) printf(" ");
			printf((floor%2?" / ":" \\ "));
			for(int p=0;p<PASSENGERS;p++)
				if(passengers[p].active && (passengers[p].from_floor==floor && passengers[p].state==WAITING)) {
					printf("%d->%d ",p,passengers[p].to_floor);
				}
			printf("\n");
		}
		usleep(DELAY);
	}
}

int main(int argc, char** argv) {
	pthread_mutex_init(&plock,0);
	scheduler_init();

	pthread_t passenger_t[PASSENGERS];
	for(int i=0;i<PASSENGERS;i++)  {
		pthread_create(&passenger_t[i],NULL,start_passenger,NULL);	
	}
	//	usleep(100000);

	pthread_t elevator_t[ELEVATORS];
	for(int i=0;i<ELEVATORS;i++) {
		pthread_create(&elevator_t[i],NULL,start_elevator,NULL);	
	}
	
#ifndef NODISPLAY
	pthread_t draw_t;
	pthread_create(&draw_t,NULL,draw_state,NULL);
#endif 

	sleep(DURATION); // run for some time, then quit
	stop = 1;
	
	pthread_join(draw_t,0);
	
	fflush(stderr);
	fflush(stdout);
	printf("\nRESULTS\n---------------\n");
	int trips=0;
	double max_duration=0;
	double sum_duration=0;
	for(int i=0;i<PASSENGERS;i++)  {
		trips+=passengers[i].trips;
		sum_duration+=passengers[i].sum_duration;
		if(max_duration<passengers[i].max_duration)
			max_duration=passengers[i].max_duration;
		printf("Passenger %d, trips: %d, max duration: %.2f, mean duration: %.2f\n",i,passengers[i].trips,passengers[i].max_duration,passengers[i].sum_duration/passengers[i].trips);
	}
	printf("Total trips %d, overall max duration %.2f, overall mean %.2f\n",trips,max_duration,sum_duration/trips); 
}

