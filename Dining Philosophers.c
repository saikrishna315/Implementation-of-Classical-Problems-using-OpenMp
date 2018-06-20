#include <mpi.h>
#include <omp.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>	
#include <unistd.h>	
#define NUM_PHILOSOPHERS 5
typedef struct {
	char data[10];
} ForkResource; 
enum message_type {
	MSG_REQ,
	MSG_FORK,
};enum philosopher_status {
	EATING,
	HUNGRY,
	THINKING,
};
static const int resources[][2] = { {0, 1}, {1, 2}, {2, 3}, {3, 4}, {0, 4} };
#define NUM_RESOURCES (sizeof(resources) / sizeof(resources[0]))
static const int min_eat_ms[NUM_PHILOSOPHERS]   = {180, 180, 180, 180, 180};
static const int max_eat_ms[NUM_PHILOSOPHERS]   = {360, 360, 360, 360, 360};
static const int min_think_ms[NUM_PHILOSOPHERS] = {180, 180, 180, 180, 180};
static const int max_think_ms[NUM_PHILOSOPHERS] = {360, 360, 360, 360, 360};
static int simulation_time = 5;
static int my_rank;
static int pids[NUM_PHILOSOPHERS];
static double eating_time   = 0.0;
static double thinking_time = 0.0;
static double hungry_time   = 0.0;
static int times_eaten      = 0;
static unsigned seed;
static void philosopher_proc(void);
static void* philosopher_main_thread_proc(void* ignore);
static void* philosopher_helper_thread_proc(void* ignore);
static void philosopher_main_thread_cycle(void);
static void philosopher_helper_thread_cycle(void);
static void think(void);
static void eat(ForkResource resources[], int num_resources);
static void sleep_rand_r(int min_ms, int max_ms, unsigned *seed);
static void millisleep(int ms);
static void sigalrm_handler(int sig);
static void sigusr1_handler(int sig);
static void usage(const char *program_name);
static void print_no_mpi_threads_msg(void);

int main(int argc, char **argv)
{
	int provided;
	int num_processes;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_size(MPI_COMM_WORLD, &num_processes);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
	if (provided != MPI_THREAD_MULTIPLE) {
		if (my_rank == 0)
			print_no_mpi_threads_msg();
		MPI_Finalize();
		exit(1);
	}
	if (num_processes != NUM_PHILOSOPHERS) {
		if (my_rank == 0)
			usage(argv[0]);
		MPI_Finalize();
		exit(1);
	}

	if (argc > 1) {
		simulation_time = atoi(argv[1]);
		if (simulation_time < 1) {
			if (my_rank == 0) {
				fprintf(stderr, "Error: Simulation time of %d "
					"seconds is not long enough\n", 
					simulation_time);
			}
			MPI_Finalize();
			exit(1);
		}
	}
	char processor_name[MPI_MAX_PROCESSOR_NAME];
	int processor_name_len;
	MPI_Get_processor_name(processor_name, &processor_name_len);
	printf("Philosopher %d running on processor %s: beginning %d second "
		"simulation\n", my_rank, processor_name, simulation_time);
		struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	if (my_rank == 0) {
		act.sa_handler = sigalrm_handler;
		sigaddset(&act.sa_mask, SIGALRM);
		sigaction(SIGALRM, &act, NULL);

		alarm(simulation_time);
	} else {
		
		act.sa_handler = sigusr1_handler;
		sigaddset(&act.sa_mask, SIGUSR1);
		sigaction(SIGUSR1, &act, NULL);
	}
	int pid = getpid();
	MPI_Gather(&pid, 1, MPI_INT, pids, 1, MPI_INT, 0, MPI_COMM_WORLD);
	time_t t = time(NULL);
	seed = t + my_rank;
	philosopher_proc();
	return 0;
}
static enum philosopher_status status;
static int num_neighbors;
static int* neighbors;
static bool* has_fork;
static bool* dirty;
static bool *req;
static int *in_reqs_data;
static MPI_Request *in_reqs;
static MPI_Request *out_reqs;
static ForkResource *forks_data;
static MPI_Request *in_forks;
static MPI_Request *out_forks;
static int num_forks;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#define ALLOC_PER_NEIGHBOR(ptr) \
	ptr = malloc(num_neighbors * sizeof(*ptr))
static pthread_t helper_thread;
void philosopher_proc()
{
int i;
	for ( i = 0; i < NUM_RESOURCES; i++) {
		if (resources[i][0] == my_rank || resources[i][1] == my_rank) {
			num_neighbors++;
		}
	}
	ALLOC_PER_NEIGHBOR(neighbors);
	ALLOC_PER_NEIGHBOR(has_fork);
	ALLOC_PER_NEIGHBOR(dirty);
	ALLOC_PER_NEIGHBOR(req);
	ALLOC_PER_NEIGHBOR(in_reqs);
	ALLOC_PER_NEIGHBOR(out_reqs);
	ALLOC_PER_NEIGHBOR(in_reqs_data);
	ALLOC_PER_NEIGHBOR(forks_data);
	ALLOC_PER_NEIGHBOR(in_forks);
	ALLOC_PER_NEIGHBOR(out_forks);
	int j = 0;
	num_forks = 0;
	for ( i = 0; i < NUM_RESOURCES; i++) {
		if (resources[i][0] == my_rank) {
			neighbors[j] = resources[i][1];
			has_fork[j]  = true;
			dirty[j]     = true;
			req[j]       = false;
				num_forks++;
			j++;
		} else if (resources[i][1] == my_rank) {
			neighbors[j] = resources[i][0];
			has_fork[j]  = false;
			dirty[j]     = false;
			req[j]       = true;
			j++;
		}
	}
	for ( i = 0; i < num_neighbors; i++) {
		MPI_Irecv(&forks_data[i], sizeof(ForkResource), MPI_CHAR, 
				neighbors[i], neighbors[i], 
				MPI_COMM_WORLD, &in_forks[i]);

		MPI_Irecv(&in_reqs_data[i], 1, MPI_INT, neighbors[i], 
				neighbors[i] + NUM_PHILOSOPHERS, 
				MPI_COMM_WORLD, &in_reqs[i]);
	}
	pthread_create(&helper_thread, NULL, philosopher_helper_thread_proc, NULL);
	philosopher_main_thread_proc(NULL);
}
static void* philosopher_main_thread_proc(void* ignore)
{
	while (1) {
		philosopher_main_thread_cycle();
	}
	return NULL;
}
static void* philosopher_helper_thread_proc(void* ignore)
{
	while (1) {
		philosopher_helper_thread_cycle();
	}
	return NULL;
}
static void philosopher_main_thread_cycle()
{
int i;
	static int msg_req  = MSG_REQ;
	static bool first_time = true;
	static double t1, t2;
	if (first_time) {
		first_time = false;
		t1 = MPI_Wtime();
	}
	printf("Philosopher %d is done eating and is preparing to think "
			"(Eaten %d times so far).\n", my_rank, times_eaten);
	t2 = MPI_Wtime();
	eating_time += (t2 - t1);
	t1 = t2;
	pthread_mutex_lock(&mutex);
	status = THINKING;
	for ( i = 0; i < num_neighbors; i++) {
		/
		if (req[i] && dirty[i]) {
			printf("Before beginning to think, Philosopher %d "
				"gave a fork to Philosopher %d.\n", 
				my_rank, neighbors[i]);
			has_fork[i] = false;
			dirty[i] = false;
			num_forks--;
			MPI_Isend(&forks_data[i], 1, MPI_INT, neighbors[i], 
				my_rank, MPI_COMM_WORLD, &out_forks[i]);
		}
	}
	pthread_mutex_unlock(&mutex);
	/
	printf("Philosopher %d is thinking.\n", my_rank);
	think();
	t2 = MPI_Wtime();
	thinking_time += (t2 - t1);
	t1 = t2;
	pthread_mutex_lock(&mutex);
	status = HUNGRY;
	if (num_forks != num_neighbors) {
		printf("Philosopher %d is hungry and only has %d forks!\n", 
							my_rank, num_forks);
		for ( i = 0; i < num_neighbors; i++) {
			if (req[i] && !has_fork[i]) {
				MPI_Isend(&msg_req, 1, MPI_INT, neighbors[i], 
						my_rank + NUM_PHILOSOPHERS,
						MPI_COMM_WORLD, &out_reqs[i]);
				printf("Philosopher %d requested a fork from "
					"Philosopher %d.\n", my_rank, neighbors[i]);
				req[i] = false;
			}
		}
		do {
			pthread_mutex_unlock(&mutex);
			printf("Philosopher %d is waiting with %d forks.\n", 
							my_rank, num_forks);
			int idx;
			MPI_Waitany(num_neighbors, in_forks, &idx, 
							MPI_STATUS_IGNORE);
			int neighbor_idx;
			for ( i = 0; i < num_neighbors; i++) {
				if (neighbors[i] == idx) {
					neighbor_idx = i;
					break;
				}
			}
			MPI_Irecv(&forks_data[neighbor_idx], sizeof(ForkResource), 
					MPI_CHAR, neighbors[idx], neighbors[idx],
					MPI_COMM_WORLD, &in_forks[idx]);

			pthread_mutex_lock(&mutex);

			printf("Philosopher %d received a fork from "
				"Philosopher %d.\n", my_rank, neighbors[idx]);
			has_fork[idx] = true;
			num_forks++;
		} while (num_forks < num_neighbors);
		printf("Philosopher %d now has %d forks and started to eat.\n", 
						my_rank, num_forks);
	} else {
		printf("Philosopher %d is hungry and already has %d forks, "
				"so he started eating.\n", my_rank, num_forks);
	}
	status = EATING;
	for ( i = 0; i < num_neighbors; i++)
		dirty[i] = true;
	pthread_mutex_unlock(&mutex);
	t2 = MPI_Wtime();
	hungry_time += (t2 - t1);
	t1 = t2;
	eat(forks_data, num_forks);
	times_eaten++;
}
static void philosopher_helper_thread_cycle()
{
	bool send_fork;
	bool request_fork;
	int msg_req  = MSG_REQ;
	int idx;
	MPI_Waitany(num_neighbors, in_reqs, &idx, MPI_STATUS_IGNORE);
	
	MPI_Irecv(&in_reqs_data[idx], 1, MPI_INT, neighbors[idx], 
			neighbors[idx] + NUM_PHILOSOPHERS, 
			MPI_COMM_WORLD, &in_reqs[idx]);
	send_fork    = false;
	request_fork = false;
	pthread_mutex_lock(&mutex);
	req[idx] = true;
	switch (status) {
	case THINKING:
		if (dirty[idx]) {
			printf(
			   "A dirty fork was taken from Philosopher %d, who is "
			   "currently thinking, and given to Philosopher %d "
			   "cleaned.\n" , my_rank, neighbors[idx]);
			fflush(stdout);
			has_fork[idx]  = false;
			dirty[idx] = false;
			send_fork  = true;
			num_forks--;
		}
		break;
	case HUNGRY:
		if (dirty[idx]) {
			printf("A dirty fork was taken from Philosopher %d, who "
				"is currently hungry, and given to Philosopher "
				"%d cleaned; then it was requested back.\n",
					my_rank, neighbors[idx]);
			fflush(stdout);
			has_fork[idx]    = false;
			dirty[idx]   = false;
			req[idx]     = false;
			send_fork    = true;
			request_fork = true;
			num_forks--;
		}
		break;
	case EATING:
		
		break;
	}
	pthread_mutex_unlock(&mutex);

	if (send_fork) {
		MPI_Isend(&forks_data[idx], 1, MPI_INT, neighbors[idx], my_rank, 
				MPI_COMM_WORLD, &out_forks[idx]);
	}
	if (request_fork) {
		MPI_Isend(&msg_req, 1, MPI_INT, neighbors[idx], 
				my_rank + NUM_PHILOSOPHERS, 
				MPI_COMM_WORLD, &out_reqs[idx]);
	}
}
static void think()
{
	sleep_rand_r(min_think_ms[my_rank], max_think_ms[my_rank], &seed);
}

static void eat(ForkResource resources[], int num_resources)
{
	sleep_rand_r(min_eat_ms[my_rank], max_eat_ms[my_rank], &seed);
}
static void millisleep(int ms)
{
	usleep(ms * 1000);
}
static void sleep_rand_r(int min_ms, int max_ms, unsigned *seed)
{
	int range = max_ms - min_ms + 1;
	int ms = rand_r(seed) % range + min_ms;
	millisleep(ms);
}
static void sigalrm_handler(int sig)
{int i;
	printf( "\nSimulation ending after %d seconds as planned.\n\n" , 
						simulation_time);
	for ( i = 1; i < NUM_PHILOSOPHERS; i++)
		kill((pid_t) pids[i], SIGUSR1);

	sigusr1_handler(0);
}
static void sigusr1_handler(int sig)
{
int i;
	double data[4] = {eating_time, thinking_time, hungry_time, (double)times_eaten};
	double results[NUM_PHILOSOPHERS * 4 * sizeof(double) + 1];
	MPI_Gather(data, 4, MPI_DOUBLE, results, 4, MPI_DOUBLE, 
					0, MPI_COMM_WORLD);
	if (my_rank == 0) {
		for ( i = 0; i < NUM_PHILOSOPHERS; i++) {
			printf("Results for Philosopher %d:\n" , i);
			printf("\t%.2f sec. spent eating\n", results[i * 4]);
			printf("\t%.2f sec. spent thinking\n", results[i * 4 + 1]);
			printf("\t%.2f sec. spent hungry\n", results[i * 4 + 2]);
			printf("\t%d times eaten\n\n", (int)results[i * 4 + 3]);
			fflush(stdout);
		}
	}

	
	millisleep(300);
	pthread_kill(helper_thread, SIGKILL);
	pthread_kill(pthread_self(), SIGKILL);
}

static void usage(const char* program_name)
{
	const char* txt = 
	"\n"
	"ERROR: This program must be run using a number of processes equal to\n"
	"the number of philosophers.  NUM_PHILOSOPHERS is currently defined as %d,\n"
	"so the program should be run with %d processes.\n"
	"\n"
	"Run with `mpirun -n NUM_PROCESSES %s [SECONDS]'\n"
	"\n"
	;
	fprintf(stderr, txt, NUM_PHILOSOPHERS, NUM_PHILOSOPHERS, program_name);
}

static void print_no_mpi_threads_msg(void)
{
	const char* txt = 
	"\n"
	"ERROR: MPI_Init_thread() failed to request support for MPI_THREAD_MULTIPLE.\n"
	"This OpenMPI compilation does not support multiple threads executing in the\n"
	"MPI library concurrently.  OpenMPI must be recompiled with the\n"
	"--enable-mpi-threads configure flag to run this program.\n"
	"\n"
	;
	fputs(txt, stderr);
