#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint64_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char **argv)
{
    // Ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;

    // Read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    // Store configuration parameters in shared data object
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // Create processes
    uint64_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        // If process should be launched immediately, add to ready queue
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
        }
    }

    // Free configuration data from memory
    deleteConfig(config);

    // Launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // Main thread work goes here
    int num_lines = 0;
    while (!(shared_data->all_terminated))
    {
        // Clear output from previous iteration
        clearOutput(num_lines);

        // Do the following:
        //   - Get current time
        uint64_t current_time = currentTime();


        //   - *Check if any processes need to move from NotStarted to Ready (based on elapsed time), and if so put that process in the ready queue
        int i;
        for(i = 0; i <processes.size(); i++){
            if(processes[i]->getStartTime() < start - current_time){
                if(processes[i]->getState() == Process::State::NotStarted){
                    //after the process is past its start time, and not started we need to set it to ready with set state,
                    //then set its cpu core to -1 so it has the dashes
                    //should mutex lock here first i think
                    //push back the ready cue? putting it on
                    //
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    //processes[i]->start waiting i need to add a wait time starter so i can get the correct wait time 

                    processes[i]->setState(Process::State::Ready, current_time);
                    processes[i]->setCpuCore(-1);

                }//checks state
            }//checks burst start time 
            {//   - *Check if any processes have finished their I/O burst, and if so put that process back in the ready queue
               std::lock_guard<std::mutex> lock(shared_data->mutex);
               if(processes[i]->getBurstStartTime() < processes[i]->getRemainingTime() - current_time){
                   //need burst time counter
                    processes[i]->setState(Process::State::Ready, current_time);

               }
              
               
            }
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                 //   - *Check if any running process need to be interrupted (RR time slice expires or newly ready process has higher priority)
                //if(processes[i]->getPriority() < shared_data->ready_queue.)
            }

        }//for loop
        
        
        
        
        
        //   - *Sort the ready queue (if needed - based on scheduling algorithm)
        //   - Determine if all processes are in the terminated state
        //   - * = accesses shared data (ready queue), so be sure to use proper synchronization

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 50 ms
        usleep(50000);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }
    uint64_t start_time = currentTime();
    double CPU_utilization=0;
    double first_half=0;
    double second_half=0;
    double average=0;
    double turnaround=0;
    double waiting =0;
    double size =  processes.size()/2;
    for(i = 0; i<processes.size(); i++)
    {
        waiting = waiting + processes[i]->getWaitTime();
        turnaround = turnaround + processes[i]->getTurnaroundTime();
        CPU_utilization = CPU_utilization + processes[i]->getCpuTime();
        if(i < size){
            first_half = first_half + processes[i]->getTurnaroundTime();
        }else{
            second_half = second_half + processes[i]->getTurnaroundTime();
        }
    }
    turnaround = turnaround/processes.size();
    waiting = waiting/processes.size();
    std::cout << "CPU utilization: " << CPU_utilization << std::endl;
    std::cout << "average throughput first half: " << first_half << std::endl;
    std::cout << "average throughput second half: " << second_half << std::endl;
    std::cout << "average overall throughput: " << (first_half + second_half)/2 << std::endl;
    std::cout << "average turnaround time: " << turnaround << std::endl;
    std::cout << "average waiting time: " << waiting << std::endl;
    //process[1]->getWaitTime();
    // print final statistics
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
    //  - Average waiting time


    // Clean up before quitting program
    processes.clear();

    return 0;
}

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    
    // Work to be done by each core idependent of the other cores
    // Repeat until all processes in terminated state:
    while(!shared_data->all_terminated){
        int readyQ_size;
        {
            std::lock_guard<std::mutex> lock(shared_data->mutex);
            readyQ_size = (int)shared_data->ready_queue.size();
        }
        
        uint64_t current_time = currentTime();
        if(readyQ_size > 0){
            Process* current_process;
            //this is where we are gonna do some work
            //*Get process at front of ready queue
            //- 
    //   - Simulate the processes running until one of the following:
    //     - CPU burst time has elapsed
    //     - Interrupted (RR time slice has elapsed or process preempted by higher priority process)
    //  - Place the process back in the appropriate queue
    //     - I/O queue if CPU burst finished (and process not finished) -- no actual queue, simply set state to IO
    //     - Terminated if CPU burst finished and no more bursts remain -- no actual queue, simply set state to Terminated
    //     - *Ready queue if interrupted (be sure to modify the CPU burst time to now reflect the remaining time)
    //  - Wait context switching time
    //  - * = accesses shared data (ready queue), so be sure to use proper synchronization
            {
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                current_process = shared_data->ready_queue.front();
                shared_data->ready_queue.pop_front();
            }
            {
                current_process->setState(Process::State::Running, current_time);
                current_process->setCpuCore(core_id);
                

            }
            while(current_process->getState() == Process::State::Running){
                uint64_t runTime = currentTime();
                if(current_process->isInterrupted()){
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    //need to check if the 
                    current_process->setState(Process::State::Ready, current_time);

                }
                if(current_process->getRemainingTime() <= 0){
                    current_process->setState(Process::State::Terminated, current_time);
                }
                if(current_process->getRemainingTime() > 0){
                    current_process->setState(Process::State::Ready, current_time);
                    std::lock_guard<std::mutex> lock(shared_data->mutex);
                    shared_data->ready_queue.push_back(current_process);

                }


            }//we check for interupts 
            /*{
                std::lock_guard<std::mutex> lock(shared_data->mutex);
                if(shared_data->ready_queue.size() == 0 && current_process->getState() == Process::State::Terminated){
                    shared_data->all_terminated = true;
                }
            }*/
        }//if(readyQ_size > 0){
        

    }//while
    //   
}

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;
    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }
    rewind(stdout);
    fflush(stdout);
}

uint64_t currentTime()
{
    uint64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
