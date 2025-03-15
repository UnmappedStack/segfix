# segfix

A custom segfault handler library for C, written in C.

segfix can detect the source of the error for some common cases such as null pointers, stack overflow/underflow, and writes to read-only data sections, and then explain the issue to you. It can also get the line number and file that the fault occured in.

## Usage
To use segfix in your project, you'll need to first clone the repository and build the library.
```
~$ git clone https://github.com/UnmappedStack/segfix
Cloning into 'segfix'...
[...]
~$ cd segfix
~$ make build
```
This will build an object file, `segfix.o`. You can now link your program with this object.

To use `segfix`, you'll need to have PIE off and have debug symbols on, using the flags `-no-pie -fno-pie -g`.

Next, you need to simply include `segfix.h` in your main entry file of your project and call the `SAFEC_INIT()` macro immediately:
```C
#include "segfix.h"

int main(int argc, char **argv) {
    SAFEC_INIT(argc, argv);
    // do everything else
}
```

Congratulations! Your project is now set up to use segfix.

## License
segfix is under the Mozilla Public License 2.0. Please see the `LICENSE` file for more information.
