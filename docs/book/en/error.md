## Error Code Management

This module is used to generate error return values. This module can generate an `int` type error code to locate the error file, error line number and error type.

Among them, in the 32-bit `int`, 8 bits represents the error code, 14 bits represents the number of lines, and 9 bits represents the file name subscript. The restrictions of this `int` value are:

- Support 255 error codes (0xff is reserved)
- Supports 16383 lines per file (0x3ffff is reserved)
- Supports 511 files (0x1ff is reserved)
- Only for file names, not file path names, so have to avoid files with the same name in different directories

When this limit is exceeded, the program will not generate any exception or report an error. It will generate error message with an hint like `Unknown ...`. You can refer to the example in the last section of this chapter.



### Header File

```c
#include "mln_error.h"
```



### Module

`error`



### Functions/Macros



#### mln_error_init

```c
void mln_error_init(mln_string_t *filenames, mln_string_t *errmsgs, mln_size_t nfile, mln_size_t nmsg);
```

Description: Initialize the error code module. It should be noted that the order of files and error messages is very important, because the subsequent generated return values will depend on the subscripts in this order.

- `filenames` is an array of type `mln_string_t`, each element in the array is a filename
- `errmsgs` is an array of type `mln_string_t`, each element in the array is an error message
- `nfile` is the number of filenames
- `nmsg` is the number of error messages

**Note**: the data of each element in `filenames` and `errmsgs` should be ended by `\0`.

Return value: none



#### RET

```c
RET(code)
```

Description: Generate a return value based on the given error code `code`. No error if `code` is 0. An invalid error value if `code` is less than 0. If `code` is greater than 0, it indicates a valid error code. If mln_error_callback_set was used to set a callback function before this, the callback function will be invoked after the return value is composed.

Return value: 0 or a negative value, 0 means no error, negative value means error. In the MSVC environment, the return value will be directly assigned to the `code`.



#### CODE

```c
CODE(r)
```

Description: According to the return value `r`, get the corresponding error code. where `r` is the return value generated by the `RET` macro.

Return value: error code greater than or equal to 0



#### mln_error_string

```c
char *mln_error_string(int err, void *buf, mln_size_t len);
```

Description: Translate the return value `err` into a string into the buffer specified by `buf` and `len`.

Return value: error message, the first address of `buf`.



#### mln_error_callback_set

```c
void mln_error_callback_set(mln_error_cb_t cb, void *udata);
```

Description: Set a callback function and user-defined data to the global variable of this module. This callback function will be invoked in the RET macro after the return value is composed.

Return value: None



### Example

```c
#include "mln_error.h"

#define OK    0
#define NMEM  1

int main(void)
{
    char msg[1024];
    mln_string_t files[] = {
        mln_string("a.c"),
    };
    mln_string_t errs[] = {
        mln_string("Success"),
        mln_string("No memory"),
    };
    mln_error_init(files, errs, sizeof(files)/sizeof(mln_string_t), sizeof(errs)/sizeof(mln_string_t));
    printf("%x %d [%s]\n", RET(OK), CODE(RET(OK)), mln_error_string(RET(OK), msg, sizeof(msg)));
    printf("%x %d [%s]\n", RET(NMEM), CODE(RET(NMEM)), mln_error_string(RET(NMEM), msg, sizeof(msg)));
    printf("%x %d [%s]\n", RET(2), CODE(RET(2)), mln_error_string(RET(2), msg, sizeof(msg)));
    printf("%x %d [%s]\n", RET(0xff), CODE(RET(0xff)), mln_error_string(RET(0xff), msg, sizeof(msg)));
    return 0;
}
```

The output:

```
0 0 [Success]
ffffedff 1 [a.c:18:No memory]
ffffec01 255 [a.c:19:Unknown Code]
ffffeb01 255 [a.c:20:Unknown Code]
```
