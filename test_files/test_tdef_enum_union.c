// Define an enum for test purposes
typedef enum {
    RED,
    GREEN,
    BLUE
} Color;

// Define a union for test purposes
typedef union {
    int intValue;
    float floatValue;
    char charValue;
} TestUnion;

int main() {
    // Test enum
    Color c = GREEN;
    
    // Test union
    TestUnion u;
    u.intValue = 42;
    u.floatValue = 3.14f;
    u.charValue = 'A';

}