#include <stdio.h>
#include "version.h"

int main(void)
{
    printf("========================================\n");
    printf("Application Version Information:\n");
    printf("  Git Hash:     %s\n", app_get_version_hash());
    printf("  Tag Version:  %s\n", app_get_version_tag());
    printf("  Commit Date:  %s\n", app_get_version_date());
    printf("  Full Version: %s\n", app_get_version_full());
    printf("========================================\n");
    return 0;
}