/*##########################################################################################
#  Descripcion: Client Server for OS CMPE 180-94 Project
#  Author: Neeraj Sharma
#  Email: neerajsharma20@gmail.com
#  Version: 1.3
#  Date: 11/01/2013
############################################################################################
OS Project Midterm Project
Client's Features
	1. Create socket on same port as server
	2. Send input file name received from user to server
	3. Fork different processes for sending requests to client

Usage
./client.out <port number> <input file name>
*/

//List of include files
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <cstdlib>
#include <arpa/inet.h> //for inet_addr function to convert IP Address
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/times.h>
#include <sys/vtimes.h>
using namespace std;

//Global Variables
ofstream logFile; //statistics log file
ofstream outputFile; //output file for storing processed files and outputs
ostringstream message; //message string to log to statistics log file
pthread_mutex_t statsLock; //lock for updating statistics log file
pthread_mutex_t outputFileLock; //lock for updating output file
static short numOfProcessors; //store number of virtual processors in system
static clock_t cpuReal, cpuSys, cpuUser; //store cpu usage snapshots at any given time
unsigned int numberOfRequests = 0; //store number of requests processed
char *serverPid = (char *)malloc(sizeof(char));

//Function Declaration
void log(string message, int mode = 0);

//Function Body

int calculateNumOfCPU() {
	/*This function reads the /proc/cpuinfo file and records
	the number of logical processors present in the CPU. This is needed to calculate
	the CPU percentage utilization correctly.
	*/
	ifstream cpuInfoFile;
	string oneLine;
	string processor = "processor";
	string splitLine;
	short result = 0;
	struct tms timeSnap;
	
	cpuInfoFile.open("/proc/cpuinfo", ios::in);
	
	while(!cpuInfoFile.eof()) {
		getline(cpuInfoFile, oneLine);
		splitLine  = oneLine.substr(0,9);
		if (processor.compare(splitLine) == 0) {
			result++;
		}
	}
	return result;
}

void printUsage() {
	/*Print the usage of the application, whenever a scenario occurs when program can't start
	with given number of arguments */
	cout << right << setw(50) << "Usage: Client Implementation" << endl;
	cout << endl;
	cout << right << setw(5) << "<filename> <port number> <input file absolute path>" << endl;
	cout << right << setw(5) << "Logs generated in current dir file: logClient.txt" << endl;
	log("Usage printed.");
}

void log(string message, int mode) {
	/*Used for logging INFO, WARN and ERROR in logServer.txt in current directory
	Mode 0 = INFO
	Mode 1 = WARN
	Mode 2 = EROR*/
	//Open log file for updating
	streambuf *backup = cout.rdbuf(); //for storing rdbuf of cout
	ofstream logFile; //output file stream for log file
	logFile.open("logClient.txt", ios::out | ios::app);	//open log file in output and append mode
	cout.rdbuf(logFile.rdbuf()); //change rdbuf of cout to log file
	
	//Get system time
    time_t now = time(NULL);
    struct tm tstruct;
    char currTime[80];
    tstruct = *localtime(&now);
    strftime(currTime, sizeof(currTime), "%m/%d/%Y-%X", &tstruct);
	
	//Check mode and write to file
	if (mode == 0) {
		cout << currTime << " INFO: " << message << endl;
	} else if (mode == 1) {
		cout << currTime << " WARN: " << message << endl;
	} else if (mode == 2) {
		cout << currTime << " EROR: " << message << endl;
	}
	
	//Restore cout rdbuf and close logfile
	cout.rdbuf(backup);
	logFile.close();
}

void* t_logVirtualMemory(void* params) {
	/* This function reads the /proc/self/status and calculate the Virtual and Resident Memory 
	being used up the server. The function updates the details every 10 seconds in stats logs.
	*/
	ifstream procFile;
	string oneLine;
	string vmSize = "VmSize:";
	string vmRss = "VmRSS:";
	string splitLine;
	
	while (true) {
		procFile.open("/proc/self/status", ios::in);
		while(!procFile.eof()) {
			getline(procFile, oneLine);
			splitLine  = oneLine.substr(0,7);
			if (vmSize.compare(splitLine) == 0) {
				pthread_mutex_lock(&statsLock);
				message << "Virtual Memory Used by process: " << oneLine.substr(12,19);
				log(message.str());
				message.str("");
				pthread_mutex_unlock(&statsLock);
				break;
			}
		}
		while(!procFile.eof()) {
			getline(procFile, oneLine);
			splitLine  = oneLine.substr(0,6);
			if (vmRss.compare(splitLine) == 0) {
				pthread_mutex_lock(&statsLock);
				message << "Resident Memory Used by process: " << oneLine.substr(12, 19);
				log(message.str());
				message.str("");
				pthread_mutex_unlock(&statsLock);
				break;
			}
		}
		procFile.close();
	sleep(10);
	}
}

void* t_cpuUsed(void* params) {
	/* This function reads uses the times function and calculates CPU utilization
	based on previous snapshot of CPU time and current parameters of CPU time. Further
	logs it into the stats logs file.
	*/
	struct tms timeSnap;
	clock_t currSnap;
	double percentCPU;
	
	while (true) {
		currSnap = times(&timeSnap);
		percentCPU = (timeSnap.tms_stime - cpuSys) + (timeSnap.tms_utime - cpuUser);
		percentCPU = percentCPU/(currSnap - cpuReal);
		percentCPU = (percentCPU/numOfProcessors)*100;
		
		cpuReal = currSnap;
		cpuSys = timeSnap.tms_stime;
		cpuUser = timeSnap.tms_utime;
		
		pthread_mutex_lock(&statsLock);
		message << "CPU Used by the client: " << percentCPU;
		log(message.str());
		message.str("");
		pthread_mutex_unlock(&statsLock);
		sleep(10);
	}
}

//Main Function

int main(int argc, char *argv[]) {
	//Trunctte logfile
	logFile.open("logClient.txt", ios::out);
	logFile.close();
	
	//Truncate output file
	outputFile.open("clientOutput.txt", ios::out);
	outputFile.close();	

	//Variables and structures for Socket Connection
	int socketID = -1, connectionID; //Client created socket ID, Connection ID for request generated
	struct sockaddr_in serverAddress; //Socket parameters for Connection request
	struct sockaddr_in clientAddress;
	int port = 3000; //Port number default 3000
	int rc = 0; //Used for storing return codes from system calls
	
	//Variables for multi-process
	pid_t childPidCall, childReqHandler, childStatCollector, childIOCollector;
	
	//Variables for sending input file name to server
	char buffer[512];
	strcpy(buffer, argv[1]);	
	
	//Variables for File Manipulation
	string oneLine; //One sentence read from the input file
	ifstream inputFile;	//File stream of input file		
	
	//Variables for handling threads
	pthread_t t_virtualMem;
	pthread_t t_cpu;
	pthread_attr_t attributes;	
	short threadCount = 0;
	
	//For CPU Utilization
	struct tms timeSnap;
	cpuReal = times(&timeSnap);
	cpuSys = timeSnap.tms_stime;
	cpuUser = timeSnap.tms_utime;	
	numOfProcessors = calculateNumOfCPU();
	
	//variables for calling server pid
	char *pidCall= (char *)"getServerPid";
	char pidBuf[100];
	int bytes = 0;
	
	//Variables for PIPE IPC between parent and child
	int pipeFD[2];
	
	//Ask user for number of requests to send to server
	int numClients = 1; //for number of requests to be sent to server
	cout << "Please enter number of requests to send to server: ";
	cin >> numClients;
	
	sockaddr &serverAddressCast = (sockaddr &) serverAddress; //Typecast server address
	
	//create detachable thread for logging virtual memory
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_create(&t_virtualMem,&attributes,t_logVirtualMemory,NULL);		

	//create detachable thread for CPU utilization
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_create(&t_cpu,&attributes,t_cpuUsed,NULL);
	
	//Create pipe for parent and childPidCall
	if (pipe(pipeFD) < 0) {
		pthread_mutex_lock(&statsLock);
		log("Unable to create pipe.", 2);
		pthread_mutex_unlock(&statsLock);
		exit(1);
	}
	
	childPidCall = fork();
	if (childPidCall == 0) {
		//create a socket
		socketID = socket(AF_INET, SOCK_STREAM, 0);
		if (socketID < 0) {
			pthread_mutex_lock(&statsLock);
			log("Error creating socket", 2);
			pthread_mutex_unlock(&statsLock);
			exit(1);
		}
		
		//Configure server socket
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = 3000;
		serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
		
		//call for serverPID for testing purpose
		connectionID = connect(socketID, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
		if (connectionID == -1) {
			pthread_mutex_lock(&statsLock);
			log("Unable to connect to server for PID call.", 2);
			pthread_mutex_unlock(&statsLock);
			exit(1);
		}
		
		pthread_mutex_lock(&statsLock);
		log("Requesting server for PID..");
		pthread_mutex_unlock(&statsLock);
		
		send(socketID, pidCall, strlen(pidCall), 0);
		bytes = recv(socketID, pidBuf, sizeof(pidBuf), 0);
		pidBuf[bytes] = '\0';
		strcpy(serverPid, pidBuf);
		
		pthread_mutex_lock(&statsLock);
		message << "Received PID of server: " << serverPid;
		log(message.str());
		message.str("");
		pthread_mutex_unlock(&statsLock);
		
		//Communicate the PID received to parent process
		close(pipeFD[0]); //close read end
		write(pipeFD[1], serverPid, sizeof(serverPid)); //write to write end
		
		//Shutdown Socket created by client
		shutdown(socketID, 2);
		close(socketID);
		//exit(EXIT_SUCCESS);
	} else {
		for (short i=0; i < numClients; i++) {
			childReqHandler = fork(); //Fork a child for creating new request
			if (childReqHandler == 0) {
				pthread_mutex_lock(&statsLock);
				message << "Created Child Process for Server. Process Nummber: " << i+1;
				log(message.str());
				message.str("");
				pthread_mutex_unlock(&statsLock);
				//create a socket
				socketID = socket(AF_INET, SOCK_STREAM, 0);
				if (socketID < 0) {
					pthread_mutex_lock(&statsLock);
					log("Error creating socket", 2);
					pthread_mutex_unlock(&statsLock);
					exit(1);
				}
				
				//Configure server socket
				serverAddress.sin_family = AF_INET;
				serverAddress.sin_port = 3000;
				serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");	
				
				//Create new connection with child process newly created
				connectionID = connect(socketID, (struct sockaddr *)&serverAddress, sizeof(serverAddress));
				if (connectionID == -1) {
					log("Unable to connect to server", 2);
					exit(1);
				}
				
				pthread_mutex_lock(&statsLock);
				log("Connection successful with server.");
				pthread_mutex_unlock(&statsLock);

				send(socketID, buffer, strlen(buffer), 0);
				pthread_mutex_lock(&statsLock);
				log("File name of input file sent to server.");
				pthread_mutex_unlock(&statsLock);
				
				exit(EXIT_SUCCESS);
			} else {
				wait(NULL); //Wait for child process completion
			}
		}
		wait(NULL);
		//Receive server pid from childPidCall on pipe
		close(pipeFD[1]); //close write end
		int rc = read(pipeFD[0], serverPid, sizeof(serverPid)); //read from read end
 		if (rc == 0) {
			pthread_mutex_lock(&statsLock);
			log("Unable to read from pipe.", 2);
			pthread_mutex_unlock(&statsLock);
			exit(1);
		}
		childStatCollector = fork();
		if (childStatCollector == 0) {
			//Process's variable declaration
			ifstream procFile, procIOFile, procStatFile; // server pid status file stream name
			ostringstream procFileName, procIOFileName, procStatFileName; //string stream for server pid file name
			string oneLine; // string to get one line of server status file
			string splitLine; // holds sub string of one line of server status
			
			// proc/status file variables
			procFileName << "/proc/" << serverPid << "/status"; // to set filename of proc with server pid
			string procFileStr = procFileName.str(); //extract string from string stream of proc file name
			const char* procFileChar = procFileStr.c_str(); // convert string to char for file opening
			
			// proc/io file variables
			procIOFileName << "/proc/" << serverPid << "/io"; // to set filename of proc with server pid
			string procIOFileStr = procIOFileName.str(); //extract string from string stream of proc file name
			const char* procIOFileChar = procIOFileStr.c_str(); // convert string to char for file opening	

			// proc/stat file variables
			procStatFileName << "/proc/" << serverPid << "/stat"; // to set filename of proc with server pid
			string procStatFileStr = procStatFileName.str(); //extract string from string stream of proc file name
			const char* procStatFileChar = procStatFileStr.c_str(); // convert string to char for file opening			
			
			streambuf *backup = cout.rdbuf(); //for storing rdbuf of cout
			
			//strings for stat file comparison
			string vmSize = "VmSize:", vmRss = "VmRSS:", name = "Name:", state = "State:", threads = "Threads:";
			string cpusAllowed = "Cpus_allowed:", volCtxtSw = "voluntary_ctxt_switches:", nonVolCtxtSw = "nonvoluntary_ctxt_switches:";
			
			//strings for IO file comparison
			string readBytes = "read_bytes:", writeBytes = "write_bytes:", numRead = "syscr:", numWrite = "syscw:";
			
			
			//Open and read proc status file of server
			cout << "Started collection of CPU stats every 10 seconds..." << endl;
 			while (true) {
				procFile.open(procFileChar, ios::in);
				procIOFile.open(procIOFileChar, ios::in);
				procStatFile.open(procStatFileChar, ios::in);
				pthread_mutex_lock(&outputFileLock);
				outputFile.open("clientOutput.txt", ios::out | ios::app);	//open output file in output and append mode
				cout.rdbuf(outputFile.rdbuf()); //change rdbuf of cout for output file		
				cout << "======================================================================================================" << endl;
				cout << "Stats Collection of server program with PID: " << serverPid << endl;
				cout << endl;
				// Status file comparisons
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,5);
					if (name.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Program file name of server running: " << oneLine.substr(6) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}	
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,6);
					if (state.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "State of server process: " << oneLine.substr(7) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}	
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,7);
					if (vmSize.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Virtual Memory Used by server: " << oneLine.substr(8) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,6);
					if (vmRss.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Resident Memory Used by server: " << oneLine.substr(7) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,8);
					if (threads.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Number of threads running by server: " << oneLine.substr(9) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,13);
					if (cpusAllowed.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Number of CPU's used by server: " << oneLine.substr(14) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,24);
					if (volCtxtSw.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Voluntary Context switches by server: " << oneLine.substr(25) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				while(!procFile.eof()) {
					getline(procFile, oneLine);
					splitLine  = oneLine.substr(0,27);
					if (nonVolCtxtSw.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Non-voluntary Context switches by server: " << oneLine.substr(28) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				//IO file comparison
				while(!procIOFile.eof()) {
					getline(procIOFile, oneLine);
					splitLine  = oneLine.substr(0,6);
					if (numRead.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Number of read system calls by server: " << oneLine.substr(7) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
				while(!procIOFile.eof()) {
					getline(procIOFile, oneLine);
					splitLine  = oneLine.substr(0,6);
					if (numWrite.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Number of write system calls by server: " << oneLine.substr(7) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}				
				while(!procIOFile.eof()) {
					getline(procIOFile, oneLine);
					splitLine  = oneLine.substr(0,11);
					if (readBytes.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Number of bytes read from disk: " << oneLine.substr(12) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				}
/* 				while(!procIOFile.eof()) {
					getline(procIOFile, oneLine);
					splitLine  = oneLine.substr(0,12);
					if (writeBytes.compare(splitLine) == 0) {
						pthread_mutex_lock(&statsLock);
						cout << "Number of bytes written to disk: " << oneLine.substr(13) << endl;
						pthread_mutex_unlock(&statsLock);
						break;
					}
				} */
				// proc/stat file parsing
				int statVal = 0;
				while(!procStatFile.eof()) {
					statVal += 1;
					getline(procStatFile, oneLine, ' ');
					if (statVal == 14) {
						cout << "Amount of time server is scheduled in User Mode: " << oneLine << endl;
					}
					if (statVal == 15) {
						cout << "Amount of time server is scheduled in Kernel Mode: " << oneLine << endl;
					}
					if (statVal == 18) {
						cout << "Priority of server process is: " << oneLine << endl;
					}
					if (statVal == 19) {
						cout << "Nice value of server process is: " << oneLine << endl;
					}
					if (statVal == 26) {
						cout << "Number of pages swapped: " << oneLine << endl;
					}
					if (statVal == 41) {
						cout << "CPU Scheduling policy used for process: " << oneLine << endl;
					} 	/*SCHED_OTHER can be used at only static priority 0.  SCHED_OTHER is
						the standard Linux time-sharing scheduler that is intended for all
						threads that do not require the special real-time mechanisms.
						root@Ubuntu:~# chrt -m
						SCHED_OTHER min/max priority	: 0/0
						SCHED_FIFO min/max priority	: 1/99
						SCHED_RR min/max priority	: 1/99
						SCHED_BATCH min/max priority	: 0/0
						SCHED_IDLE min/max priority	: 0/0
						*/
				}				
				//close for both files
				cout << endl;
				cout.rdbuf(backup);
				outputFile.close();
				pthread_mutex_unlock(&outputFileLock);
				procFile.close();
				procIOFile.close();
				procStatFile.close();
			sleep(10);
			}	
			exit(EXIT_SUCCESS);
		} else {
			wait(NULL); //wait for child stat collector to finish
		}
		//Shutdown Socket created by client
		shutdown(socketID, 2);
		close(socketID);
		exit(EXIT_SUCCESS);
	}
	return 0;
}