#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    const char *record_path = getenv("SOLAR_VIEWER_RECORD");
    FILE *record;

    if (argc != 2 || record_path == NULL) return 64;
    record = fopen(record_path, "w");
    if (record == NULL) return 74;
    if (fprintf(record, "%s\n%s\n", argv[0], argv[1]) < 0 ||
        fclose(record) != 0) {
        return 74;
    }
    return 0;
}
