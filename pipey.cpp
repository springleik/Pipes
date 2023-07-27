// --------------------------------------------------------------
// pipey.cpp sends structs with zero-length arrays through a pipe
// M. Williamsen, Quantum Design, 14 November 2022

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
char xBuff[256] = {0};
pid_t pid;
int firstPipe[2], secondPipe[2];

// suppress padding in the following classes
#pragma pack(2)

// WARNING These classes use zero-length arrays,
// so they must never be instantiated!  The intended
// use is to point to a region of memory that contains
// data laid out according to the member list.  There
// are no virtual methods in these classes.

// --------------------------------------------------------------
// base class for all packet types
class whatBase
{
public:
    // enumerator identifies packet types
    enum typeEnum
    {
        typeNone,
        typeA = 10,
        typeB
    };

    // instance methods
    void showHex(ostream &os);
    void writeOut(FILE *file);
    enum typeEnum readIn(FILE *file);

protected:
    // member list for memory layout (8 bytes total)
    union
    {
        struct
        {
            int length;             // 4 bytes, size including subclass
            enum typeEnum type;     // 4 bytes, type of subclass
        };
        unsigned char buffer[0];    // zero-length array
    };
};

// show all packet types as hex bytes in Json
void whatBase::showHex(ostream &os)
{
    os << hex << setfill('0') << ",\"hex\":[";
    for (int n = 0; n < length; n++)
    {
        if (n) {os << ((n % 8)? ",": ",\n ");} else {os << "\n ";}
        os << "\"0x" << setw(2) << short(buffer[n]) << '"';
    }
    os << "\n]" << dec << setfill(' ') << flush;
}

// read a packet from anonymous pipe, return type enum
enum whatBase::typeEnum whatBase::readIn(FILE *file)
{
    if (!fread(buffer, 1, 4, file)) {return typeNone;}
    if (!fread(buffer + 4, 1, length - 4, file)) {return typeNone;}
    return type;
}

// write a packet to anonymous pipe
void whatBase::writeOut(FILE *file)
{
    fwrite(buffer, 1, length, file);
    fflush(file);
}

// --------------------------------------------------------------
// subclass with real-value members (20 bytes plus string)
class whatA: public whatBase
{
public:
    // instance methods
    void populate(float a, double b, const string &c);
    void serialize(ostream &os);
    void modify(double d);

private:
    // member list for memory layout, without trailing null
    float theFlt;   // 4 bytes
    double theDbl;  // 8 bytes
    char theStr[0]; // zero-length array (must be last)
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

    // copy data members into memory, no trailing null
    length = sizeof(whatA) + cSize;
    type = typeA;
    theFlt = a;
    theDbl = b;
    c.copy(theStr, cSize);
}

// report method for type A
void whatA::serialize(ostream &os)
{
    // check for plausible input
    if (length < 0 || length > 256)
    {
        os << "Bad length: " << length << ", pid: " << pid << endl;
        return;
    }

    // show struct data members first
    int cSize = length - sizeof(whatA);
    string cStr(theStr, cSize);
    os << dec << setprecision(8)
        << ",{\"length\":" << length
        << ",\"type\":" << type
        << ",\"theFlt\":" << theFlt
        << ",\"theDbl\":" << theDbl
        << ",\"theStr\":\"" << cStr << "\"";

    // then show buffer contents as hex bytes
    showHex(os);
    os << '}' << endl;
}

// modify values stored in data members, no trailing null
void whatA::modify(double d)
{
    theFlt *= d;
    theDbl *= (d*d);
    memcpy(buffer + length, ")>-", 3);
    length += 3;
}

// --------------------------------------------------------------
// subclass with integer-value members (14 bytes plus string)
class whatB: public whatBase
{
public:
    // instance methods
    void populate(short a, int b, const string &c);
    void serialize(ostream &os);
    void modify(int d);

private:
    // member list for memory layout, without trailing null
    short theShort; // 2 bytes
    int theInt;     // 4 bytes
    char theStr[0]; // zero-length array (must be last)
};

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

    // copy data members into memory, no trailing null
    length = sizeof(whatB) + cSize;
    type = typeB;
    theShort = a;
    theInt = b;
    c.copy(theStr, cSize);
}

// report method for type B
void whatB::serialize(ostream &os)
{
    // check for plausible input
    if (length < 0 || length > 256)
    {
        os << "Bad length: " << length << ", pid: " << pid << endl;
        return;
    }

    // show struct data members first
    int cSize = length - sizeof(whatB);
    string cStr(theStr, cSize);
    os << dec << setprecision(8)
        << ",{\"length\":" << length
        << ",\"type\":" << type
        << ",\"theShort\":" << theShort
        << ",\"theInt\":" << theInt
        << ",\"theStr\":\"" << cStr << "\"";

    // then show buffer contents as hex bytes
    showHex(os);
    os << '}' << endl;
}

// modify values stored in data members, no trailing null
void whatB::modify(int d)
{
    theShort *= d;
    theInt  *= (d*d);
    memcpy(buffer + length, "-<(0", 4);
    length += 4;
}

// --------------------------------------------------------------
// this code runs only in the parent process
void doParentStuff()
{
    // parent process pipes for input and output
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

        // populate a type A instance
        whatA *myWhatA = reinterpret_cast<whatA *>(xBuff);
        myWhatA->populate(1.234e5, 2.345e67, theString);

        // write out to child
        myWhatA->writeOut(wFile);
        cout << "[\"pipey\"";
        myWhatA->serialize(cout);
        memset(xBuff, 0, sizeof(xBuff));

        // populate a type B instance
        whatB *myWhatB = reinterpret_cast<whatB *>(xBuff);
        myWhatB->populate(0x1234, 0x123456, theString);

        // write out to child
        myWhatB->writeOut(wFile);
        myWhatB->serialize(cout);
        memset(xBuff, 0, sizeof(xBuff));

        // read two packets back from child
        for (int n = 0; n < 2; n++)
        {
            switch(myWhatA->readIn(rFile))
            {
                case whatBase::typeA:
                    myWhatA->serialize(cout);
                    break;

                case whatBase::typeB:
                    myWhatB->serialize(cout);
                    break;

                case whatBase::typeNone:
                    cerr << "Type not set." << endl;
                    break;
            }
        }
        cout << ']';
    }   while (true);

    // clean up and exit
    fclose(wFile);
    fclose(rFile);
}

// --------------------------------------------------------------
// this code runs only in the child process
void doChildStuff()
{
    // child process pipes for input and output
    close(firstPipe[1]);
    close(secondPipe[0]);
    rFile = fdopen(firstPipe[0], "r");
    wFile = fdopen(secondPipe[1], "w");

    // iterate over packets sent from parent
    // fread() blocks until parent closes the pipe
    do  {
        // check packet type, modify values accordingly
        whatBase *myWhat = reinterpret_cast<whatBase *>(xBuff);
        switch (myWhat->readIn(rFile))
        {
            case whatBase::typeA:
                static_cast<whatA *>(myWhat)->modify(2.0);
                break;

            case whatBase::typeB:
                static_cast<whatB *>(myWhat)->modify(3);
                break;

            case whatBase::typeNone:
                cout << "Child done." << endl;
                fclose(rFile);
                fclose(wFile);
                return;
        }

        // write the instance back out, common to all packet types
        myWhat->writeOut(wFile);
        memset(xBuff, 0, sizeof(xBuff));
    }   while (true);
}

// --------------------------------------------------------------
// main entry point
int main()
{
    // check struct packing
    cout << "Type A is 20 bytes without string: " << sizeof(whatA)
        << "\nType B is 14 bytes without string: " << sizeof(whatB) << endl;

    // open two anonymous pipes
    if (pipe(firstPipe))
    {
        cerr << "Failed to open first (outbound) pipe." << endl;
        return -1;
    }
    if (pipe(secondPipe))
    {
        cerr << "Failed to open second (inbound) pipe." << endl;
        return -1;
    }

    // fork into two processes
    pid = fork();
    if (!pid) {doChildStuff();}
    else if (pid < 0)
    {
        cerr << "Fork failed: " << pid << endl;
        return -2;
    }
    else {doParentStuff();}
    return 0;
}
