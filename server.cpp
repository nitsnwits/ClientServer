/*##########################################################################################
#  Description: Client Server for OS CMPE 180-94 Project
#  Author: A-Team (Neeraj Sharma, Vineet Bhatkoti, Manish Sharma, Annie Zhang)
#  Email: neerajsharma20@gmail.com
#  Version: 1.4
#  Date: 11/11/2013
############################################################################################
OS Project
Server's features
	1. Read an arbitrary size text file
	2. Convert upper case to lower case and vice versa
	3. Count number of total characters
	4. Count number of total words
	5. Count number of total sentences
	6. Count number of times each alphabet occurs
	7. Count number of times each word occurs
Revision: 1.3
	1. Receive absolute path of input file from client
	2. Log statistics to logServer.txt in current directory
	3. Open Socket connection on port number given by user
	4. Wait for connections from client on socket
Revision: 1.4
	1. Multi-threads functionality added
	2. Detachable threads for stats logging
	3. CPU and Virtual memory utilization
Usage
./server.out
*/
//List of Include Files

#define NUM_THREADS 1000

#include <iostream>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h> //for inet_addr function to convert IP Address
#include <cstring>
#include <string>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <sstream>
#include <iterator>
#include <map>
#include <ctime>
#include <iomanip>
#include <sys/times.h>
#include <sys/vtimes.h>
#include <sys/time.h>

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

//Function Declarations
void log(string message, int mode = 0); //log function to update message strings to log file
void* t_requestHandler(void *params); //thread function to process files in separate thread

//Functions Body

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
	cout << right << setw(50) << "Usage: Server Implementation" << endl;
	cout << endl;
	cout << right << setw(5) << "<filename> <port number>" << endl;
	cout << right << setw(5) << "Logs generated in current dir file: logServer.txt" << endl;	
	log("Usage printed.");
}

void log(string message, int mode) {
	/*Used for logging INFO, WARN and ERROR in logServer.txt in current directory
	Mode 0 = INFO
	Mode 1 = WARN
	Mode 2 = EROR*/
	//Open log file for updating
	streambuf *backup = cout.rdbuf(); //for storing rdbuf of cout
	logFile.open("logServer.txt", ios::out | ios::app);	//open log file in output and append mode
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

void* t_requestHandler(void* params) {
	/* This function is the thread handler function.
	The server's features to process files and convert case etc are all housed by this function.
	Server creates a separate thread for each client request and provides this function to run for the thread.
	Server also provides the absolute path received from client as input to this function.
	This function opens the received file and process the file operations.
	This function updates the statistics log file as it carries out the operations.
	After completing operations, it updates the output file with the results, which server can read later.
	*/
	
	//Variables for File Manipulation
	string oneLine; //One sentence read from the input file
	ifstream inputFile;	//File stream of input file	
	vector<string> lines; //lines of file
	vector<string> strings; //all strings in one line of file
	vector<string> output; //lines of output file
	map<string, int> wordCount; //map keeps count of each word
	map<char, int> charCount; //map keeps count of alphabets
	int numOfWords = 0; //counts the total number of words
	int numOfChars = 0; //counts the total number of characters	
	char *inputFileName; //Pointer for storing input file name received from parent thread
	unsigned int requestNumber = numberOfRequests;
	
	timeval t1, t2;
	double elapsedTime;
	gettimeofday(&t1, NULL);

	//Typecast void parameters
	inputFileName = (char *) params;
	
	//Open the received file for reading.
	inputFile.open(inputFileName);
	
	streambuf *backup = cout.rdbuf(); //for storing rdbuf of cout

	while (!inputFile.eof()) {
		getline(inputFile, oneLine); //Get one sentence of text file
		if (oneLine.empty()) continue; //Skip empty lines
		lines.push_back(oneLine); //Insert every sentence into vector
	}		
	cout << endl;
	inputFile.close(); //close file		
	
	pthread_mutex_lock(&statsLock);
	message << "New request thread spinned. Thread ID: " << pthread_self();
	log(message.str());
	message.str("");
	pthread_mutex_unlock(&statsLock);
	
	//Start File operations given in Server's features
	for (int i=0; i < lines.size(); i++) { //Read the lines stored in lines vector one by one
		istringstream s (lines[i]); //set string stream for lines vector for each line read
		strings.insert(strings.end(), istream_iterator<string>(s), istream_iterator<string>()); // update strings vector with strings
		//iterate through strings vector to access each word
		for (int j=0; j < strings.size(); j++) { //read strings vector one by one for each string
			for (int k = 0; k < strings[j].length(); k++) { //read each character of each string read from strings vector
				if (islower(strings[j][k]))  //convert to uppercase if found in lowercase, store it in vector
					strings[j][k] = toupper(strings[j][k]);
				else if (isupper(strings[j][k]))  //convert to lowercase if found in uppercase, store it in vector
					strings[j][k] = tolower(strings[j][k]);
				//update char map name charCount, for keeping count of characters occurring in file
				char mapKey = strings[j][k]; //map for storing each character count found in the file
				if (islower(mapKey)) //convert to upper for storing key value of map
					mapKey = toupper(mapKey);
				if (charCount.count(mapKey) > 0) { //update value by 1, if the key(uppercase) is already stored
					charCount[mapKey] += 1;
				}
				else { //if key does not exist, update the map with new key and value as 1
					charCount.insert(pair<char,int>(mapKey, 1));
				}							
			}
			numOfChars += strings[j].length(); //retrive number of characters from strings vector iteration for each string as length of str
			//update the output with changed cases
			output.push_back(strings[j]);//output is a new vector, sply created for storing changed cases strings
			output.push_back(" "); //separate each string with a space
			//update map name wordCount, for keeping count of words in the file
			string mapKey = strings[j]; //store the word in map with word as string
			for (int x=0; x < mapKey.length(); x++) {
				if (islower(mapKey[x])) //convert each word to uppercase for uniqueness of map keys
					mapKey[x] = toupper(mapKey[x]);
			}
			if (wordCount.count(mapKey) > 0) {//if the word has occurred more than once, increment value by 1
				wordCount[mapKey] += 1;
			}
			else {//else otherwise, insert the new word in map, and store value as 1
				wordCount.insert(pair<string,int>(mapKey, 1));
			}					
		}
		output.push_back("\n"); //add newline to output vector
		numOfWords += strings.size(); //update number of words
		//keep track of line number and words counted in it
		//cout << "Line no: " << i << " No. of Words: " << strings.size() << endl;
		//empty strings array to update with next sentence
		while (!strings.empty()) {
			strings.pop_back();
		}
	}
	//Open output file for updating under outputFileLock for this thread execution
	pthread_mutex_lock(&outputFileLock);
	outputFile.open("serverOutput.txt", ios::out | ios::app);	//open output file in output and append mode
	cout.rdbuf(outputFile.rdbuf()); //change rdbuf of cout for output file
	cout << "======================================================================================================" << endl;
	cout << "File processing: " << inputFileName << " Thread ID: " << pthread_self() << endl;
	cout << endl;
	cout << "Total no. of lines: " << lines.size() << endl;
	cout << "Total no. of words: " << numOfWords << endl;
	cout << "Total no. of characters: " << numOfChars << endl;
	cout << "=============Words Repetition==============" << endl;
	for (map<string,int>::iterator it=wordCount.begin(); it!=wordCount.end(); ++it)
		cout << "Word Count: " << it->first << " => " << it->second << endl;
	cout << endl;
	cout << "===========Character Repetition============" << endl;
	for (map<char,int>::iterator it=charCount.begin(); it!=charCount.end(); ++it)
		cout << "Char Count: " << it->first << " => " << it->second << endl;			
	cout << endl;
/* 	cout << "		File output with changed cases: " << endl;
	cout << endl;
	//Print out output array and clean it
	while (!output.empty())	{
		cout << output.front();
		output.erase(output.begin());
	} */
	cout.rdbuf(backup);
	outputFile.close();
	pthread_mutex_unlock(&outputFileLock);
	
    // stop timer
    gettimeofday(&t2, NULL);

    // compute and print the elapsed time in millisec
    elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
    elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms
    //cout << elapsedTime << " ms.\n";		
	
	//Update statistics for request completion
	pthread_mutex_lock(&statsLock);
	message << "Request Number: " << requestNumber << " completed successfully in " << elapsedTime << " ms.";
	log(message.str());
	message.str("");
	pthread_mutex_unlock(&statsLock);
	
	pthread_exit(0);
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
				message << "Virtual Memory Used by process: " << oneLine.substr(8,19);
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
				message << "Resident Memory Used by process: " << oneLine.substr(7, 19);
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
		message << "CPU Used by the server: " << percentCPU;
		log(message.str());
		message.str("");
		pthread_mutex_unlock(&statsLock);
		sleep(10);
	}
}

//Main Function

int main(int argc, char *argv[]) {
	//Truncate logfile
	logFile.open("logServer.txt", ios::out);
	logFile.close();
	
	//Truncate output file
	outputFile.open("serverOutput.txt", ios::out);
	outputFile.close();

	//Variables and structures for Socket Connection
	int socketID = -1, connectionID; //Socket ID is server created socket, Connection ID is received and accepted connection
	struct sockaddr_in serverAddress; //Socket parameters for bind
	struct sockaddr_in clientAddress; //Socket parameters for accept
	socklen_t clientAddrLen; // Socket parameters for accept
	int port = 3000; //Port number default 3000
	int rc = 0; //Used for storing return codes from system calls
	
	//Variables for Receiving input from Client
	char buffer[512]; //Receiviing buffer for input file name from client
	char *inputFileName; //Pointer for storing input file name from client
	int bytes = 0; //Received number of bytes in one iteration
	
	//Variables for collecting statistics
	pid_t serverPid = getpid();
	
	//Variables for handling threads
	pthread_t threads[NUM_THREADS];
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
	
	sockaddr &serverAddressCast = (sockaddr &) serverAddress; //Typecast server address for socket binding
	sockaddr &clientAddressCast = (sockaddr &) clientAddress;

	//Create a socket
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
	
	//bind server to socket
	rc = bind(socketID, &serverAddressCast, sizeof(serverAddress));
	if (rc == -1) {
		pthread_mutex_lock(&statsLock);
		log("Error in binding to socket", 2);
		pthread_mutex_unlock(&statsLock);
		exit(1);
	}	
	
	//listen to this socket
	rc = listen(socketID, 10);
	if (rc == -1) {
		pthread_mutex_lock(&statsLock);
		log("Error in listening to socket", 2);
		pthread_mutex_unlock(&statsLock);
		exit(1);
	}
	
	pthread_mutex_lock(&statsLock);
	message << "Server started. Server PID: " << serverPid << " Socket ID: " << socketID;
	log(message.str());
	message.str("");
	pthread_mutex_unlock(&statsLock);
	
	//create detachable thread for logging virtual memory
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_create(&t_virtualMem,&attributes,t_logVirtualMemory,NULL);		

	//create detachable thread for CPU utilization
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	pthread_create(&t_cpu,&attributes,t_cpuUsed,NULL);		

	while (true) {
		//Wait for client requests
		cout << "Server waiting for requests..." << endl;
		connectionID = accept(socketID, NULL, &clientAddrLen);
		if (connectionID == -1) {
			pthread_mutex_lock(&statsLock);
			log("Error in accepting client request", 2);
			pthread_mutex_unlock(&statsLock);
			cout << "Error in accepting client request. Exiting..." << endl;
			exit(1);
		}	
		
		//Update statistics logs with mutex
		pthread_mutex_lock(&statsLock);
		message << "Request received. Connection created successfully.";
		log(message.str());
		message.str("");
		pthread_mutex_unlock(&statsLock);
		cout << "Connection created successfully."  << endl;
		
		//Receive absolute path file name from client
		bytes = recv(connectionID, buffer, sizeof(buffer), 0);
		buffer[bytes] = '\0'; 
		char *inputFileName = (char *)malloc(sizeof(char) * 1024);
		strcpy(inputFileName, buffer);
		
		//Check if it's a call for server's PID
		char *pidCall = (char *)"getServerPid";
		char pid[100];
		snprintf(pid, sizeof(pid),"%d", serverPid);
		if (strcmp(inputFileName, pidCall) == 0) {
			send(connectionID, pid, strlen(pid), 0);
			pthread_mutex_lock(&statsLock);
			message << "PIDCall received. PID successfully sent to client.";
			log(message.str());
			message.str("");
			pthread_mutex_unlock(&statsLock);
			//Close the socket
			shutdown(connectionID, 2);
			close(connectionID);			
			continue;
		}
		
		//Update statistics logs with mutex
		pthread_mutex_lock(&statsLock);
		message << "Data bytes received from client. Bytes: " << bytes;
		log(message.str());
		log("Received input file name from client: "+(string)inputFileName);
		message.str("");
		pthread_mutex_unlock(&statsLock);
		
		//Set attributes for thread, all are joinable, same attributes are used for all three threads
		pthread_attr_init(&attributes);
		pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE);	
		
		//update number of requests handled
		numberOfRequests++;
		pthread_mutex_lock(&statsLock);
		message << "Total number of requests handled till now: " << numberOfRequests;
		log(message.str());
		message.str("");
		pthread_mutex_unlock(&statsLock);

		//create the thread
		pthread_create(&threads[threadCount],&attributes,t_requestHandler,(void*) (inputFileName));	
		if (threadCount == NUM_THREADS) {
			pthread_mutex_lock(&statsLock);
			log("Number of threads reached maximum. Exiting...");
			pthread_mutex_unlock(&statsLock);
			cout << "Reached maximum threads. Exiting." << endl;
			exit(1);
		} else {
			threadCount++;
		}
		
		//Close the socket
		shutdown(connectionID, 2);
		close(connectionID);
	}
	//Join all the threads
	for (int i=0; i < NUM_THREADS; i++) {
		pthread_join(threads[i],NULL);
		pthread_mutex_lock(&statsLock);
		message << "Thread " << i << " Joined back. Closed connection.";
		log(message.str());
		message.str("");
		pthread_mutex_unlock(&statsLock);
	}	
	return 0;
}