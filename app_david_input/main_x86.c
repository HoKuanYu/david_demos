#include <utils/app_init/include/app_init.h>

int app_david_input_main(int argc, char* argv[]);

int main(int argc, char* argv[])
{
    int status;

    appInit();
    status = app_david_input_main(argc, argv);
    appDeInit();

    return status;
}