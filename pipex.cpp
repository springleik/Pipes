// pipex.cpp sends structs with flexible array members through a pipe
// M. Williamsen, 7 November 2022
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

// WARNING These classes end with a flexible array member,
// so they must never be instantiated!
// The intended use is to point to a region of memory which
// contains data laid out according to the member list.
// NOTE There are no virtual methods in the base class.
// suppress padding in subclasses

#pragma pack(2)

// base class for all packet types
struct whatBase
{
    // instance method
    void showHex(ostream &os);

    // member list for memory layout (8 bytes total)
    int length;         // 4 bytes, size including subclass
    enum typeEnum{typeA = 10,
        typeB} type;    // 4 bytes, type of subclass
};

void whatBase::showHex(ostream &os)
{
    unsigned char *p = reinterpret_cast<unsigned char *>(this);
    os << hex << setfill('0') << ",\"hex\":[";
    for (int n = 0; n < length; n++)
    {
        if (n) {os << ((n % 8)? ",": ",\n ");} else {os << "\n ";}
        os << '"' << setw(2) << short(*p++) << '"';
    }
    os << "\n]" << dec << setfill(' ');
}

// subclass with real-value members (20 bytes plus string)
struct whatA: public whatBase
{
    // instance methods
    void populate(float a, double b, const string &c);
    void serialize(ostream &os);

    // member list for memory layout, without trailing null
    float theFlt;   // 4 bytes
    double theDbl;  // 8 bytes
    char theStr[];  // flexible array member (must be last)
};

// subclass with integer-value members (14 bytes plus string)
struct whatB: public whatBase
{
    // instance methods
    void populate(short a, int b, const string &c);
    void serialize(ostream &os);

    // member list for memory layout, without trailing null
    short theShort; // 2 bytes
    int theInt;     // 4 bytes
    char theStr[];  // flexible array member (must be last)
};

// initialization method for type A
void whatA::populate(float a, double b, const string &c)
{
    // check for plausible input
    int cSize = c.size();
    if (cSize < 0 || cSize > 250)
    {
        cerr << "Bad string size: " << cSize << endl;
        return;
    }
    
    // copy data members into memory, without trailing null
    length = sizeof(whatA) + cSize;
    type = typeA;
    theFlt = a;
    theDbl = b;
    c.copy(theStr, cSize);
}

// initialization method for type B
void whatB::populate(short a, int b, const string &c)
{
    // check for plausible input
    int cSize = c.size();
    if (cSize < 0 || cSize > 250)
    {
        cerr << "Bad string size: " << cSize << endl;
        return;
    }
    
    // copy data members into memory, without trailing null
    length = sizeof(whatB) + cSize;
    type = typeB;
    theShort = a;
    theInt = b;
    c.copy(theStr, cSize);
}

// report method for type A
void whatA::serialize(ostream &os)
{
    // check for plausible input
    if (length < 0 || length > 256)
    {
        os << "Bad struct size: " << length << ", pid: " << pid << endl;
        return;
    }
    
    // show struct data members first
    int cSize = length - sizeof(whatA);
    string cStr(theStr, cSize);
    os << dec << setprecision(8)
        << "{\"length\":" << length
        << ",\"type\":" << type
        << ",\"theFlt\":" << theFlt
        << ",\"theDbl\":" << theDbl
        << ",\"theStr\":\"" << cStr << "\"";
    
    // then show buffer contents as hex bytes
    showHex(os);
    os << '}' << endl;
}

// report method for type B
void whatB::serialize(ostream &os)
{
    // check for plausible input
    if (length < 0 || length > 256)
    {
        os << "Bad struct size: " << length << ", pid: " << pid << endl;
        return;
    }
    
    // show struct data members first
    int cSize = length - sizeof(whatB);
    string cStr(theStr, cSize);
    os << dec << setprecision(8)
        << "{\"length\":" << length
        << ",\"type\":" << type
        << ",\"theShort\":" << theShort
        << ",\"theInt\":" << theInt
        << ",\"theStr\":\"" << cStr << "\"";
    
    // then show buffer contents as hex bytes
    showHex(os);
    os << '}' << endl;
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
        
        // allocate and populate a type A instance
        whatA *myWhatA = reinterpret_cast<whatA *>(buff);
        myWhatA->populate(1.234e5, 2.345e67, theString);
        
        // write out to child
        fwrite(buff, 1, myWhatA->length, wFile);
        fflush(wFile);
        myWhatA->serialize(cout);
        memset(buff, 0, sizeof(buff));
        
        // allocate and populate a type B instance
        whatB *myWhatB = reinterpret_cast<whatB *>(buff);
        myWhatB->populate(0x1234, 0x123456, theString);
        
        // write out to child
        fwrite(buff, 1, myWhatB->length, wFile);
        fflush(wFile);
        myWhatB->serialize(cout);
        memset(buff, 0, sizeof(buff));
        
        // read back from child, reuse same buffer
        fread(buff, 1, 4, rFile);
        fread(buff + 4, 1, myWhatA->length - 4, rFile);
        myWhatA->serialize(cout);
        fread(buff, 1, 4, rFile);
        fread(buff + 4, 1, myWhatB->length - 4, rFile);
        myWhatB->serialize(cout);
    }   while (true);
    // clean up and exit
    cout << "All done." << endl;
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
    
    // iterate over packets sent from parent
    // fread() blocks until parent closes the pipe
    do  {
        // common code for all packet types
        whatBase *myWhat = reinterpret_cast<whatBase *>(buff);
        if (!fread(buff, 1, 4, rFile)) {break;}
        if (!fread(buff + 4, 1, myWhat->length - 4, rFile)) {break;}
        
        // check packet type, modify values accordingly
        switch (myWhat->type)
        {
            case whatBase::typeA:
            {
                whatA *myWhatA = reinterpret_cast<whatA *>(buff);
                myWhatA->theFlt *= 2.0;
                myWhatA->theDbl *= 32.0;
                strncpy(buff + myWhatA->length, ")>-", 3);
                myWhatA->length += 3;
                break;
            }
            case whatBase::typeB:
            {
                whatB *myWhatB = reinterpret_cast<whatB *>(buff);
                myWhatB->theShort *= 2;
                myWhatB->theInt  *= 32;
                strncpy(buff + myWhatB->length, "-<(", 3);
                myWhatB->length += 3;
                break;
            }
            default:
                cout << "Unknown type: " << myWhat->type << endl;
        }
        
        // write the instance back out, common to all packet types
        fwrite(buff, 1, myWhat->length, wFile);
        fflush(wFile);
        memset(buff, 0, sizeof(buff));
    }   while (true);
    
    // clean up and exit
    fclose(rFile);
    fclose(wFile);
}

int main()
{
    // check struct packing
    cout << "Type A is 20 bytes without string: " << sizeof(whatA)
        << "\nType B is 14 bytes without string: " << sizeof(whatB) << endl;
    
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
