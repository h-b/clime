# clime
**C**++ **LI**ght **ME**ssage passing library

This platform independent library provides basic helper functions to send messages between std::threads in a C++ application
(no inter process communication). The basic idea is to provide a lightweight helper framework using pure C++
(no dependency to MPI or boost).

It wraps the C++11 thread support functions to clearly separate tasks in an application,
not only regarding the class design but the runtime behaviour. This way no needless blocking of class instances happens that
should work independent from each other (like one uses std::future for a temporary worker). 
In UI libraries this is a common requirement to avoid blocking the UI. 

Generally there is no reason 
other than C++ language design why one class usually waits for another class to do its job
(e. g. when calling a function of a member class),
although it could happily take care of other things meanwhile. That's why the go programming language
has its [own statement](https://golang.org/ref/spec#Go_statements) for this scenario.
