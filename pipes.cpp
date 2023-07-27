// pipes.cpp sends structs with flexible array members through a pipe
// M. Williamsen, 4 November 2022
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <cstring>
#include <iomanip>

using namespace std;

// global variables, duplicated in the child process
FILE *rFile, *wFile;
char buff[256] = {0};
pid_t pid;
int firstPipe[2], secondPipe[2];

// WARNING This struct ends with a flexible array member,
// so it must never be instantiated!  The intended use
// is to point to a region of memory which contains
// data laid out according to the member list.
struct what
{
    // instance methods
    void populate(float a, double b, const string &c);
    void show(ostream &os);

    // member list for memory layout, without trailing null
    int len;        // 4 bytes, must contain actual size
    float one;      // 4 bytes
    double two;     // 8 bytes
    char three[];   // flexible array member (must be last)
};

// implementation of initialization method
void what::populate(float a, double b, const string &c)
{
    // check for plausible input
    int cSize = c.size();
    if (cSize < 0 || cSize > 250)
    {
        cerr << "Bad string size: " << cSize << endl;
        return;
    }
    
    // copy data members into memory, without trailing null
    len = sizeof(what) + cSize;
    one = a;
    two = b;
    strncpy(three, c.c_str(), cSize);
}

// implementation of report method
void what::show(ostream &os)
{
    // check for plausible input
    if (len < 0 || len > 256)
    {
        os << "Bad struct size: " << len << ", pid: " << pid << endl;
        return;
    }
    
    // show struct data members first
    int cSize = len - sizeof(what);
    string cStr(three, cSize);
    os << "len: " << len
        << ", one: " << one
        << ", two: " << two
        << ", three: '" << cStr << "'";
    
    // then show buffer contents as hex bytes
    unsigned char *p = reinterpret_cast<unsigned char *>(this);
    os << hex << setfill('0');
    for (int n = 0; n < len; n++)
    {
        os << ((n % 16)? ' ': '\n');
        os << setw(2) << short(*p++);
    }
    os << dec << setfill(' ') << endl;
}

void doParentStuff()
{
    // parent process
    close(firstPipe[0]);
    close(secondPipe[1]);
    wFile = fdopen(firstPipe[1], "w");
    rFile = fdopen(secondPipe[0], "r");
    
    // iterate over lines of user input
    string theString;
    do  {
        // parent gets user input
        theString.clear();
        cout << "\nType a text string: ";
        getline(cin, theString);
        if (!theString.size()) {break;}
        
        // allocate and populate an instance
        what *myWhat = reinterpret_cast<what *>(buff);
        myWhat->populate(1.234e5, 2.345e67, theString);
        
        // write out to child
        fwrite(buff, 1, myWhat->len, wFile);
        fflush(wFile);
        myWhat->show(cout);
        memset(buff, 0, sizeof(buff));
        
        // read back from child
        fread(buff, 1, 4, rFile);
        fread(buff + 4, 1, myWhat->len - 4, rFile);
        myWhat->show(cout);
    }   while (true);
    
    // clean up and exit
    fclose(wFile);
    fclose(rFile);
}

void doChildStuff()
{
    // child process
    close(firstPipe[1]);
    close(secondPipe[0]);
    rFile = fdopen(firstPipe[0], "r");
    wFile = fdopen(secondPipe[1], "w");
    
    // iterate over structs sent from parent
    do  {
        what *myWhat = reinterpret_cast<what *>(buff);
        fread(buff, 1, 4, rFile);
        fread(buff + 4, 1, myWhat->len - 4, rFile);
        // modify values in struct
        myWhat->one *= 2;
        myWhat->two *= 2;
        strncpy(buff + myWhat->len, "]=+", 3);
        myWhat->len += 3;
        // write the instance back out
        fwrite(buff, 1, myWhat->len, wFile);
        fflush(wFile);
        memset(buff, 0, sizeof(buff));
    }   while (true);
    
    // clean up and exit
    fclose(rFile);
    fclose(wFile);
}

int main()
{
    // open two anonymous pipes
    if (pipe(firstPipe))
    {
        cout << "Failed to open first (outbound) pipe." << endl;
        return -1;
    }
    if (pipe(secondPipe))
    {
        cout << "Failed to open second (inbound) pipe." << endl;
        return -1;
    }
    
    // fork into two processes
    pid = fork();
    if (!pid) {doChildStuff();}
    else if (pid < 0)
    {
        cout << "Fork failed: " << pid << endl;
        return -2;
    }
    else {doParentStuff();}
    return 0;
}
