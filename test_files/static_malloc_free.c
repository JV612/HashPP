static int globalCounter = 0;

static void incrementGlobal() {
    static int localStatic = 0;
    localStatic++;
    globalCounter++;
}

int main() {
    static int mainStatic = 100;
    
    incrementGlobal();
    incrementGlobal();
    
    int *ptr = (int*) malloc(sizeof(int) * 10);
    char *buffer = (char*) malloc(100);
    
    if (ptr != nullptr) {
        for (int i = 0; i < 10; i++) {
            ptr[i] = i * i;
        }
    }
    
    free(ptr);
    free(buffer);
    
    return 0;
}