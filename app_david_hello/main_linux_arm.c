#include <utils/app_init/include/app_init.h>

int main(int argc, char *argv[])
{
    int status = 0;
    
    status = appInit();
    
    if(status==0)
    {
        int app_david_hello_main(int argc, char* argv[]);
        
        status = app_david_hello_main(argc, argv);
        appDeInit();
    }
    
    return status;
}